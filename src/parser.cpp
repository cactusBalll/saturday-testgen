#include "parser.hpp"
#include "utils.hpp"
#include "z3++.h"

#include <optional>

namespace ststgen {

    static const char *CONSTRAINT_FUNC_NAME = "_CONSTRAINT";

    void array_idx_generator(const std::vector<int> &dims,
                             int depth,
                             std::vector<int> &idx,
                             std::vector<std::vector<int>> &idxes) {
        if (depth == dims.size()) {
            idxes.push_back(idx);
            return;
        }
        for (int i = 0; i < dims[depth]; i++) {
            idx.push_back(i);
            array_idx_generator(dims, depth + 1, idx, idxes);
            idx.pop_back();
        }
    }
    std::any CConstraintVisitor::visitExpressionStatement(c11parser::CParser::ExpressionStatementContext *ctx) {
        m_process_constraint_statement = true;
        auto expr = std::any_cast<z3::expr>(visitChildren(ctx));
        m_smt_solver.add(expr);
        m_process_constraint_statement = false;
        return 0;
    }
    std::any CConstraintVisitor::visitFunctionDefinition(c11parser::CParser::FunctionDefinitionContext *ctx) {
        bool process_constraints = false;
        info("process function definition", ctx->getText());
        if (const auto func_declarator = ctx->declarator()) {
            // func(p1,p2,...)
            if (const auto func_direct_declarator = func_declarator->directDeclarator()) {
                // func
                if (const auto func_direct_declarator2 = func_direct_declarator->directDeclarator()) {
                    if (const auto identifier = func_direct_declarator2->Identifier()) {
                        if (identifier->getText() == CONSTRAINT_FUNC_NAME) {
                            process_constraints = true;
                        }
                    }
                }
            }
        }
        if (process_constraints) {
            m_symbol_table.push_scope();
            visit(ctx->compoundStatement());
            m_symbol_table.pop_scope();
        }
        return 0;
    }
    std::any CConstraintVisitor::visitDeclaration(c11parser::CParser::DeclarationContext *ctx) {
        if (m_symbol_table.get_scope_level() > 1) {
            info("declarations not in top level are not constraints:", ctx->getText());
            return 0;
        }
        SymbolTableEntryType base_type = SymbolTableEntryType::None;
        std::string custom_struct_name{};
        auto decl_list = ctx->declarationSpecifiers();
        bool struct_typedef_ctx{false};
        StructBlueprint blueprint{};
        std::vector<z3::sort> member_sorts{};
        std::vector<const char *> member_names{};

        for (auto typ: decl_list->declarationSpecifier()) {
            if (auto typ_s = typ->typeSpecifier()) {
                if (typ_s->Char() || typ_s->Short() || typ_s->Int() || typ_s->Long() || typ_s->Signed()) {
                    base_type = SymbolTableEntryType::Int64;
                    break;
                } else if (typ_s->Unsigned()) {
                    base_type = SymbolTableEntryType::UInt64;
                    break;
                } else if (typ_s->Float() || typ_s->Double()) {
                    base_type = SymbolTableEntryType::Float64;
                    break;
                } else if (auto p_st = typ_s->structOrUnionSpecifier()) {
                    // 结构体
                    if (m_symbol_table.get_scope_level() > 1) {
                        panic("struct definition is only allowed in toplevel.");
                    }
                    if (p_st->structOrUnion()->Union()) {
                        panic("union is not supported.");
                    } else {
                        for (auto p_st_member: p_st->structDeclarationList()->structDeclaration()) {
                            // specifierQualifierList是没展平的列表结构
                            // 处理int a[5];的int部分
                            auto p = p_st_member->specifierQualifierList();
                            auto base_typ_member = SymbolTableEntryType::None;
                            while (p != nullptr) {
                                if (auto typ_s_member = p->typeSpecifier()) {
                                    if (typ_s->Char() || typ_s->Short() || typ_s->Int() || typ_s->Long() || typ_s->Signed()) {
                                        base_typ_member = SymbolTableEntryType::Int64;
                                        break;
                                    } else if (typ_s->Unsigned()) {
                                        base_typ_member = SymbolTableEntryType::UInt64;
                                        break;
                                    } else if (typ_s->Float() || typ_s->Double()) {
                                        base_typ_member = SymbolTableEntryType::Float64;
                                        break;
                                    }
                                }
                                p = p->specifierQualifierList();
                            }
                            if (base_typ_member == SymbolTableEntryType::None) {
                                panic("struct member base type not supported.");
                            }
                            //  处理declarator，即int a[5];的a[5]部分
                            for (auto p_declarator_member: p_st_member->structDeclaratorList()->structDeclarator()) {
                                SymbolTableEntry member{};
                                member.type = base_typ_member;
                                auto p_declarator_member1 = p_declarator_member->declarator();
                                if (p_declarator_member->constantExpression()) {
                                    // 不支持位域
                                    panic("bitfield is not supported.");
                                }
                                if (!p_declarator_member1->gccDeclaratorExtension().empty()) {
                                    panic("gcc declarator extension is not supported.");
                                }
                                if (auto p_pctx_member = p_declarator_member1->pointer()) {
                                    if (p_pctx_member->Star().size() > 1) {
                                        panic("only single pointer is supported.");
                                    }
                                    member.qualifer = SymbolTableEntryQualifer::Pointer;
                                    if (auto identifier = p_declarator_member1->directDeclarator()->Identifier()) {
                                        blueprint.push_member(identifier->getText(), std::move(member));
                                    } else {
                                        panic("only pointers to primary types are supported.");
                                    }
                                } else {
                                    // 没有展平的列表结构
                                    std::string name{};
                                    auto p_drct_declarator = p_declarator_member1->directDeclarator();
                                    if (auto p_identifier = p_drct_declarator->Identifier()) {
                                        member.qualifer = SymbolTableEntryQualifer::Primary;
                                        blueprint.push_member(p_identifier->getText(), std::move(member));
                                    } else {
                                        member.qualifer = SymbolTableEntryQualifer::Array;
                                        while (p_drct_declarator != nullptr) {
                                            if (auto p_dim = p_drct_declarator->assignmentExpression()) {
                                                // 这里应该是表达式，但是表达式求值实现过于复杂
                                                // 只允许常量
                                                int dim_num = atoi(p_dim->getText().c_str());
                                                if (dim_num == 0) {
                                                    panic("array dim is not constant.");
                                                }
                                                member.dims.push_back(dim_num);
                                            }
                                            p_drct_declarator = p_drct_declarator->directDeclarator();
                                        }
                                        if (auto p_identifier = p_drct_declarator->Identifier()) {
                                            name = p_identifier->getText();
                                        } else {
                                            panic("decl form not supported. identifier for Array not found.");
                                        }
                                        blueprint.push_member(name, std::move(member));
                                    }
                                }
                            }
                        }
                    }
                    // construct Tuple Sort in z3 with struct declaration
                    base_type = SymbolTableEntryType::Struct;

                    for (const auto &[name, entry]: blueprint.m_members) {
                        member_names.emplace_back(name.c_str());
                        // 0 for Int, 1 for Real
                        int base_type = 0;
                        if (entry.type == SymbolTableEntryType::Int32 ||
                            entry.type == SymbolTableEntryType::Int64 ||
                            entry.type == SymbolTableEntryType::UInt32 ||
                            entry.type == SymbolTableEntryType::UInt64) {
                            base_type = 0;
                        } else if (entry.type == SymbolTableEntryType::Float32 || entry.type == SymbolTableEntryType::Float64) {
                            base_type = 1;
                        } else {
                            unreachable();
                        }
                        if (entry.qualifer == SymbolTableEntryQualifer::Primary) {
                            if (base_type == 0) {
                                member_sorts.emplace_back(m_solver_context.int_sort());
                            } else {
                                member_sorts.emplace_back(m_solver_context.real_sort());
                            }
                        } else if (entry.qualifer == SymbolTableEntryQualifer::Array) {
                            auto base_sort = base_type ? m_solver_context.real_sort() : m_solver_context.int_sort();
                            base_sort = m_solver_context.seq_sort(base_sort);
                            member_sorts.emplace_back(base_sort);
                        } else if (entry.qualifer == SymbolTableEntryQualifer::Pointer) {
                            // seq sort without length constraint
                            auto base_sort = base_type ? m_solver_context.real_sort() : m_solver_context.int_sort();
                            base_sort = m_solver_context.seq_sort(base_sort);
                            member_sorts.emplace_back(base_sort);
                        } else {
                            unreachable();
                        }
                    }

                    if (p_st->Identifier()) {
                        auto struct_name = p_st->Identifier()->getText();
                        z3::func_decl_vector member_getters(m_solver_context);
                        auto struct_constructor = m_solver_context.tuple_sort(
                                struct_name.c_str(), member_names.size(), member_names.data(), member_sorts.data(), member_getters);
                        blueprint.sym_constructor = struct_constructor;
                        blueprint.sym_getters = member_getters;
                        m_struct_blueprints.insert({p_st->Identifier()->getText(), blueprint});
                    } else {
                        // 结构体的名字在typedef struct{...}后面
                        struct_typedef_ctx = true;
                    }
                } else if (auto p_st_name = typ_s->typedefName()) {
                    base_type = SymbolTableEntryType::Struct;
                    custom_struct_name = p_st_name->Identifier()->getText();
                    break;
                }
            }
        }
        if (struct_typedef_ctx) {
            z3::func_decl_vector member_getters(m_solver_context);
            auto struct_constructor = m_solver_context.tuple_sort(
                    custom_struct_name.c_str(), member_names.size(), member_names.data(), member_sorts.data(), member_getters);
            blueprint.sym_constructor = struct_constructor;
            blueprint.sym_getters = member_getters;
            m_struct_blueprints.insert({custom_struct_name, blueprint});
            return 0;
        }

        auto declarator_list = ctx->initDeclaratorList();
        for (auto p_declarator: declarator_list->initDeclarator()) {
            if (p_declarator->initializer()) {
                panic("initializer is not supported.");
            }
            auto p_declarator2 = p_declarator->declarator();
            SymbolTableEntry entry{};
            entry.type = base_type;

            if (auto p_pctx_member = p_declarator2->pointer()) {
                if (base_type == SymbolTableEntryType::Struct) {
                    panic("pointer to struct is not supported.");
                }
                if (p_pctx_member->Star().size() > 1) {
                    panic("only single pointer is supported.");
                }
                entry.qualifer = SymbolTableEntryQualifer::Pointer;
                if (auto identifier = p_declarator2->directDeclarator()->Identifier()) {
                    insert_entry(identifier->getText(), std::move(entry));
                } else {
                    panic("only pointers to primary types are supported.");
                }
            } else {
                // 没有展平的列表结构
                std::string name{};
                auto p_drct_declarator = p_declarator2->directDeclarator();
                if (auto p_identifier = p_drct_declarator->Identifier()) {
                    entry.qualifer = SymbolTableEntryQualifer::Primary;
                    if (base_type == SymbolTableEntryType::Struct) {
                        entry.struct_name = custom_struct_name;
                        entry.type = SymbolTableEntryType::Struct;
                        insert_entry(p_identifier->getText(), std::move(entry));
                    } else {
                        insert_entry(p_identifier->getText(), std::move(entry));
                    }
                } else {
                    entry.qualifer = SymbolTableEntryQualifer::Array;
                    while (p_drct_declarator != nullptr) {
                        if (auto p_dim = p_drct_declarator->assignmentExpression()) {
                            // 这里应该是表达式，但是表达式求值实现过于复杂
                            // 只允许常量
                            int dim_num = atoi(p_dim->getText().c_str());
                            if (dim_num == 0) {
                                panic("array dim is not constant.");
                            }
                            entry.dims.push_back(dim_num);
                        }
                        p_drct_declarator = p_drct_declarator->directDeclarator();
                    }
                    if (auto p_identifier = p_drct_declarator->Identifier()) {
                        name = p_identifier->getText();
                    } else {
                        panic("decl form not supported. identifier for Array not found.");
                    }
                    if (base_type == SymbolTableEntryType::Struct) {
                        const auto &blueprint = m_struct_blueprints[custom_struct_name];
                        entry.type = SymbolTableEntryType::Struct;
                        entry.struct_name = custom_struct_name;
                        insert_entry(name, std::move(entry));
                    } else {
                        insert_entry(name, std::move(entry));
                    }
                }
            }
        }
        return 0;
    }
    std::any CConstraintVisitor::visitPrimaryExpression(c11parser::CParser::PrimaryExpressionContext *ctx) {
        if (ctx->Identifier()) {
            auto name = ctx->Identifier()->getText();
            if (auto sym = m_symbol_table.lookup_entry(name)->sym) {
                return *sym;
            }
            panic("can't find symbol \"" + name + "\".");
        }
        if (ctx->Constant()) {
            auto num = std::stod(ctx->Constant()->getText());
            // 实数使用分数表示
            return m_solver_context.real_val(num * 1000, 1000);
        }
        if (ctx->expression()) {
            return visit(ctx->expression());
        }
        panic("can't parse expression.");
    }
    std::any CConstraintVisitor::visitPostfixExpression(c11parser::CParser::PostfixExpressionContext *ctx) {
        auto prime_expr = std::any_cast<z3::expr>(visit(ctx->primaryExpression()));
        for (auto p_post_op: ctx->postfixOp()) {
            if (p_post_op->expression() != nullptr) {
                // indexing with seq theorem
                auto idx = std::any_cast<z3::expr>(visit(p_post_op->expression()));
                prime_expr = prime_expr.at(idx);
            } else if (p_post_op->argumentExpressionList() != nullptr) {
                // function form constraints
                if (!prime_expr.is_string_value()) {
                    panic("not a function");
                }
                const auto func_name = prime_expr.get_string();
                if (func_name == "_LENGTH") {
                    auto args = std::any_cast<std::vector<z3::expr>>(p_post_op->argumentExpressionList());
                    // seq theorem length primitive
                    prime_expr = args[0].length();
                } else if (func_name == "GAUSSIAN") {
                    auto args = std::any_cast<std::vector<z3::expr>>(p_post_op->argumentExpressionList());
                    stst_assert(args.size() == 3);
                    stst_assert(args[0].is_const());
                    stst_assert(args[1].is_numeral());
                    stst_assert(args[2].is_numeral());
                    auto name = args[0].to_string();
                    auto miu = args[1].as_double();
                    auto sigma = args[2].as_double();
                    auto vari = m_symbol_table.lookup_entry(name);
                    vari->cons |= GAUSSIAN;
                    vari->miu = miu;
                    vari->sigma = sigma;
                    return prime_expr;
                } else {
                    info("constraint name: ", func_name);
                    panic("unknown function constraint name");
                }
            } else if (p_post_op->Identifier() != nullptr) {
                auto field = p_post_op->Identifier()->getText();
                auto sort = prime_expr.get_sort();
                for (const auto &bp: m_struct_blueprints) {
                    // 找到对应struct/tuple的类型构造器
                    if (z3::eq(sort, bp.second.sym_constructor->range())) {
                        for (const auto &getter: *bp.second.sym_getters) {
                            if (field == getter.name().str()) {
                                // 找到对应project函数
                                prime_expr = getter(prime_expr);
                                goto field_outter;
                            }
                        }
                    }
                }
            field_outter:
                continue;
            }
        }
        return prime_expr;
    }
    std::any CConstraintVisitor::visitArgumentExpressionList(c11parser::CParser::ArgumentExpressionListContext *ctx) {
        std::vector<z3::expr> args{};
        for (auto p_expr: ctx->assignmentExpression()) {
            auto ret = std::any_cast<z3::expr>(visit(p_expr));
            args.emplace_back(ret);
        }
        return args;
    }
    std::any CConstraintVisitor::visitUnaryExpression(c11parser::CParser::UnaryExpressionContext *ctx) {
        if (!ctx->PlusPlus().empty() || !ctx->MinusMinus().empty() || !ctx->Sizeof().empty()) {
            panic("sizeof, ++, -- are not supported.");
        }
        if (ctx->postfixExpression() != nullptr) {
            return visit(ctx->postfixExpression());
        }
        if (ctx->castExpression() != nullptr) {
            auto expr = std::any_cast<z3::expr>(visit(ctx->castExpression()));
            auto uop = ctx->unaryOperator()->getText();
            if (uop == "&") {
                unimplemented();
            }
            if (uop == "*") {
                unimplemented();
            }
            if (uop == "+") {
                return expr;
            }
            if (uop == "-") {
                return -expr;
            }
            if (uop == "!") {
                return !expr;
            }
            if (uop == "~") {
                panic("bitwise operators are not supported.");
            }
        }
        unreachable();
    }

    std::any CConstraintVisitor::visitCastExpression(c11parser::CParser::CastExpressionContext *ctx) {
        if (ctx->unaryExpression() == nullptr) {
            panic("cast expression is not implemented.");
        }
        return visit(ctx->unaryExpression());
    }
    std::any CConstraintVisitor::visitMultiplicativeExpression(c11parser::CParser::MultiplicativeExpressionContext *ctx) {
        auto e = std::any_cast<z3::expr>(visit(ctx->castExpression(0)));
        for (int i = 1; i < ctx->castExpression().size(); ++i) {
            auto e2 = std::any_cast<z3::expr>(visit(ctx->castExpression(i)));
            if (ctx->mulop(i - 1)->getText() == "*") {
                e = e * e2;
            } else if (ctx->mulop(i - 1)->getText() == "/") {
                e = e / e2;
            } else {
                info("% is generated, it might not be solved by z3 effectively.");
                e = e % e2;
            }
        }
        return e;
    }
    std::any CConstraintVisitor::visitAdditiveExpression(c11parser::CParser::AdditiveExpressionContext *ctx) {
        auto e = std::any_cast<z3::expr>(visit(ctx->multiplicativeExpression(0)));
        for (int i = 1; i < ctx->multiplicativeExpression().size(); ++i) {
            auto e2 = std::any_cast<z3::expr>(visit(ctx->multiplicativeExpression(i)));
            if (ctx->addop(i - 1)->getText() == "+") {
                e = e + e2;
            } else {
                e = e - e2;
            }
        }
        return e;
    }
    std::any CConstraintVisitor::visitShiftExpression(c11parser::CParser::ShiftExpressionContext *ctx) {
        auto p_sub = ctx->additiveExpression();
        if (p_sub.size() > 1) {
            panic("bitwise operation not supported since we dont use bitvec theory.");
        }
        return visit(p_sub[0]);
    }
    std::any CConstraintVisitor::visitRelationalExpression(c11parser::CParser::RelationalExpressionContext *ctx) {
        if (ctx->shiftExpression().size() > 2) {
            panic("chain < <= > >= not work as expected most of time thus not allowed");
        }
        if (ctx->shiftExpression().size() == 1) {
            return visit(ctx->shiftExpression().front());
        }

        auto clause0 = std::any_cast<z3::expr>(visit(ctx->shiftExpression()[0]));
        auto clause1 = std::any_cast<z3::expr>(visit(ctx->shiftExpression()[1]));
        if (ctx->relop(0)->getText() == "<") {
            return clause0 < clause1;
        }
        if (ctx->relop(0)->getText() == "<=") {
            return clause0 <= clause1;
        }
        if (ctx->relop(0)->getText() == ">=") {
            return clause0 >= clause1;
        }
        if (ctx->relop(0)->getText() == ">") {
            return clause0 > clause1;
        }
        unreachable();
    }
    std::any CConstraintVisitor::visitEqualityExpression(c11parser::CParser::EqualityExpressionContext *ctx) {
        if (ctx->relationalExpression().size() > 2) {
            panic("chain == != not work as expected most of time thus not allowed");
        }
        if (ctx->relationalExpression().size() == 1) {
            return visit(ctx->relationalExpression().front());
        }
        auto clause0 = std::any_cast<z3::expr>(visit(ctx->relationalExpression()[0]));
        auto clause1 = std::any_cast<z3::expr>(visit(ctx->relationalExpression()[1]));
        if (ctx->eqop(0)->getText() == "!=") {
            return clause0 != clause1;
        }
        if (ctx->eqop(0)->getText() == "==") {
            return clause0 == clause1;
        }
        unreachable();
    }
    std::any CConstraintVisitor::visitAndExpression(c11parser::CParser::AndExpressionContext *ctx) {
        auto p_sub = ctx->equalityExpression();
        if (p_sub.size() > 1) {
            panic("bitwise operation not supported since we dont use bitvec theory.");
        }
        return visit(p_sub[0]);
    }
    std::any CConstraintVisitor::visitExclusiveOrExpression(c11parser::CParser::ExclusiveOrExpressionContext *ctx) {
        auto p_sub = ctx->andExpression();
        if (p_sub.size() > 1) {
            panic("bitwise operation not supported since we dont use bitvec theory.");
        }
        return visit(p_sub[0]);
    }
    std::any CConstraintVisitor::visitInclusiveOrExpression(c11parser::CParser::InclusiveOrExpressionContext *ctx) {
        auto p_sub = ctx->exclusiveOrExpression();
        if (p_sub.size() > 1) {
            panic("bitwise operation not supported since we dont use bitvec theory.");
        }
        return visit(p_sub[0]);
    }
    std::any CConstraintVisitor::visitLogicalAndExpression(c11parser::CParser::LogicalAndExpressionContext *ctx) {
        z3::expr expr = m_solver_context.bool_val(true);
        for (auto p_clause: ctx->inclusiveOrExpression()) {
            auto clause = std::any_cast<z3::expr>(visit(p_clause));
            expr = expr && clause;
        }
        return expr;
    }
    std::any CConstraintVisitor::visitLogicalOrExpression(c11parser::CParser::LogicalOrExpressionContext *ctx) {
        z3::expr expr = m_solver_context.bool_val(false);
        for (auto p_clause: ctx->logicalAndExpression()) {
            auto clause = std::any_cast<z3::expr>(visit(p_clause));
            expr = expr || clause;
        }
        return expr;
    }
    std::any CConstraintVisitor::visitConditionalExpression(c11parser::CParser::ConditionalExpressionContext *ctx) {
        auto cond = std::any_cast<z3::expr>(visit(ctx->logicalOrExpression()));
        if (ctx->expression() != nullptr && ctx->conditionalExpression() != nullptr) {
            z3::expr true_branch = z3::implies(cond, std::any_cast<z3::expr>(visit(ctx->expression())));
            z3::expr false_branch = z3::implies(!cond, std::any_cast<z3::expr>(visit(ctx->conditionalExpression())));
            z3::expr conj = true_branch || false_branch;
            return conj;
        } else {
            return cond;
        }
    }
    std::any CConstraintVisitor::visitAssignmentExpression(c11parser::CParser::AssignmentExpressionContext *ctx) {
        if (ctx->assignmentOperator() != nullptr) {
            panic("assignment operator is not allowed");
        }
        return visitChildren(ctx);
    }
    std::any CConstraintVisitor::visitExpression(c11parser::CParser::ExpressionContext *ctx) {
        std::optional<z3::expr> last_expr = std::nullopt;
        for (auto p_assign_expr: ctx->assignmentExpression()) {
            auto ret = std::any_cast<z3::expr>(visit(p_assign_expr));
            last_expr = std::move(ret);
        }
        if (last_expr) {
            auto t = *last_expr;
            return t;
        }
        unreachable();
    }
    void CConstraintVisitor::solve() {
        auto res = m_smt_solver.check();
        if (res == z3::unsat) {
            fmt::println("constraint unsat");
            return;
        }
        if (res == z3::unknown) {
            fmt::println("constraint unknown");
            return;
        }
        fmt::println("constraint sat");
        auto model = m_smt_solver.get_model();
        auto solve = json{};
        for (const auto &[name, entry]: m_symbol_table.get_scope(0)) {
            if (entry.qualifer == SymbolTableEntryQualifer::Primary) {
                if (entry.type == SymbolTableEntryType::Int32 ||
                    entry.type == SymbolTableEntryType::Int64 ||
                    entry.type == SymbolTableEntryType::UInt32 ||
                    entry.type == SymbolTableEntryType::UInt64) {
                    auto subst = model.get_const_interp(entry.sym->decl());
                    auto v = subst.as_int64();
                    solve[name] = v;
                } else if (entry.type == SymbolTableEntryType::Float32 || entry.type == SymbolTableEntryType::Float64) {
                    auto subst = model.get_const_interp(entry.sym->decl());
                    auto v = subst.as_double();
                    solve[name] = v;
                } else if (entry.type == SymbolTableEntryType::Struct) {
                    auto subst = model.get_const_interp(entry.sym->decl());
                    const auto &blueprint = m_struct_blueprints[entry.struct_name];
                    int idx = 0;
                    for (const auto &[member_name, member_entry]: blueprint.m_members) {
                        const auto &getter_sym = (*blueprint.sym_getters)[idx];
                        auto member_sym = model.eval(getter_sym(subst));
                        if (member_entry.qualifer == SymbolTableEntryQualifer::Primary) {
                            if (member_entry.type == SymbolTableEntryType::Int32 ||
                                member_entry.type == SymbolTableEntryType::Int64 ||
                                member_entry.type == SymbolTableEntryType::UInt32 ||
                                member_entry.type == SymbolTableEntryType::UInt64) {
                                solve[name][member_name] = member_sym.as_int64();
                            } else if (entry.type == SymbolTableEntryType::Float32 ||
                                       entry.type == SymbolTableEntryType::Float64) {
                                solve[name][member_name] = member_sym.as_double();
                            } else {
                                unreachable();
                            }
                        }
                        if (entry.qualifer == SymbolTableEntryQualifer::Array) {
                            auto member_array_json = process_z3_seq(entry.dims, member_sym, model, entry_type_2_value_type(member_entry.type));
                            solve[name][member_name] = member_array_json;
                        }
                        if (entry.qualifer == SymbolTableEntryQualifer::Pointer) {
                            // pointers are actually handled as 1-D arrays
                            auto length = model.eval(member_sym.length()).as_int64();
                            std::vector dims{static_cast<int>(length)};
                            auto member_array_json = process_z3_seq(dims, member_sym, model, entry_type_2_value_type(member_entry.type));
                            solve[name][member_name] = member_array_json;
                        }

                        idx += 1;
                    }
                } else {
                    unreachable();
                }
            } else if (entry.qualifer == SymbolTableEntryQualifer::Array) {
                auto subst = model.get_const_interp(entry.sym->decl());
                auto array_json = process_z3_seq(entry.dims, subst, model, entry_type_2_value_type(entry.type));
                solve[name] = array_json;
            } else if (entry.qualifer == SymbolTableEntryQualifer::Pointer) {
                auto subst = model.get_const_interp(entry.sym->decl());
                auto length = model.eval(subst.length()).as_int64();
                std::vector dims{static_cast<int>(length)};
                auto array_json = process_z3_seq(dims, subst, model, entry_type_2_value_type(entry.type));
                solve[name] = array_json;
            }
        }
        fmt::print("got a solve: {}\n", solve.dump(4));
        m_solves.push_back(solve);
    }

}// namespace ststgen
