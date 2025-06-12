#include "parser.hpp"
#include "utils.hpp"
#include "z3++.h"


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
        auto expr = std::any_cast<z3::expr>(visit(ctx->expression()));
        // guard for gaussian or other function-like constraints
        if (!expr.is_string_value()) {
            info("add cons: ", expr.to_string());
            m_smt_solver.add(expr);
            m_cons_expressions.push_back(ctx->expression()->getText());
        }
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
                            info("var decl: ", m_smt_solver.to_smt2());
                            process_constraints = true;
                        }
                    }
                }
            }
        }
        if (process_constraints) {
            m_symbol_table.push_scope();
            m_cons_src = ctx->compoundStatement()->getText();
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
        info("proccess declaration", ctx->getText());
        SymbolTableEntryType base_type = SymbolTableEntryType::None;
        std::string custom_struct_name{};
        auto decl_list = ctx->declarationSpecifiers();
        bool struct_typedef_ctx{false};
        StructBlueprint blueprint{};
        std::vector<z3::sort> member_sorts{};
        std::vector<const char *> member_names{};

        for (auto typ: decl_list->declarationSpecifier()) {
            if (auto typ_s = typ->typeSpecifier()) {
                if (typ_s->Char() || typ_s->Short() || typ_s->Int() || typ_s->Signed()) {
                    base_type = SymbolTableEntryType::Int32;
                } else if (typ_s->Long()) {
                    base_type = SymbolTableEntryType::Int64;
                } else if (typ_s->Unsigned()) {
                    base_type = SymbolTableEntryType::UInt64;
                } else if (typ_s->Float() || typ_s->Double()) {
                    base_type = SymbolTableEntryType::Float64;
                } else if (auto p_st = typ_s->structOrUnionSpecifier()) {
                    // 结构体
                    if (m_symbol_table.get_scope_level() > 1) {
                        panic("struct definition is only allowed in toplevel.");
                    }
                    if (p_st->structOrUnion()->Union()) {
                        panic("union is not supported.");
                    }
                    if (p_st->structDeclarationList() == nullptr) {
                        // struct S var;
                        stst_assert(p_st->Identifier() != nullptr);
                        custom_struct_name = p_st->Identifier()->getText();
                        base_type = SymbolTableEntryType::Struct;
                        continue;
                    }
                    for (auto p_st_member: p_st->structDeclarationList()->structDeclaration()) {
                        // specifierQualifierList是没展平的列表结构
                        // 处理int a[5];的int部分
                        auto p = p_st_member->specifierQualifierList();
                        auto base_typ_member = SymbolTableEntryType::None;
                        while (p != nullptr) {
                            if (auto typ_s_member = p->typeSpecifier()) {
                                if (typ_s_member->Char() ||
                                    typ_s_member->Short() ||
                                    typ_s_member->Int() ||
                                    typ_s_member->Signed()) {
                                    base_typ_member = SymbolTableEntryType::Int32;
                                    break;
                                } else if (typ_s_member->Long()) {
                                    base_typ_member = SymbolTableEntryType::Int64;
                                    break;
                                }
                                else if (typ_s_member->Unsigned()) {
                                    base_typ_member = SymbolTableEntryType::UInt64;
                                    break;
                                } else if (typ_s_member->Float() || typ_s_member->Double()) {
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
                                            // 根据树结构，应该插入到前面
                                            member.dims.insert(member.dims.cbegin(), dim_num);
                                        }
                                        if (p_drct_declarator->Identifier() != nullptr) {
                                            break;
                                        }
                                        p_drct_declarator = p_drct_declarator->directDeclarator();
                                    }
                                    if (p_drct_declarator == nullptr) {
                                        panic("decl form not supported. identifier for Array not found.");
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
                        info("member getter: ", member_getters.to_string());
                        blueprint.sym_getters = member_getters;
                        m_struct_blueprints.insert({p_st->Identifier()->getText(), blueprint});
                    } else {
                        // 结构体的名字在typedef struct{...}后面
                        struct_typedef_ctx = true;
                    }
                } else if (auto p_st_name = typ_s->typedefName()) {
                    if (base_type == SymbolTableEntryType::None) {
                        custom_struct_name = p_st_name->getText();
                        base_type = SymbolTableEntryType::Struct;
                    } else {
                        if (struct_typedef_ctx) {
                            custom_struct_name = p_st_name->getText();
                        } else {
                            SymbolTableEntry entry{};
                            entry.type = base_type;
                            entry.qualifer = SymbolTableEntryQualifer::Primary;
                            auto name = p_st_name->getText();
                            insert_entry(name, entry);
                        }
                        break;
                    }
                }
            }
            if (auto sto = typ->storageClassSpecifier()) {
                if (sto->Typedef() != nullptr) {
                    struct_typedef_ctx = true;
                }
            }
        }
        if (struct_typedef_ctx) {
            z3::func_decl_vector member_getters(m_solver_context);
            auto struct_constructor = m_solver_context.tuple_sort(
                    custom_struct_name.c_str(), member_names.size(), member_names.data(), member_sorts.data(), member_getters);

            blueprint.sym_constructor = struct_constructor;
            blueprint.sym_getters = member_getters;

            info("member getter: ", member_getters.to_string());
            m_struct_blueprints.insert({custom_struct_name, blueprint});
            return 0;
        }

        auto declarator_list = ctx->initDeclaratorList();
        if (declarator_list == nullptr) {
            // simple decl e.g. int a;
            return 0;
        }
        for (auto p_declarator: declarator_list->initDeclarator()) {
            bool is_func_decl = false;
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
                            entry.dims.insert(entry.dims.cbegin(), dim_num);
                        }
                        if (p_drct_declarator->parameterTypeList()) {
                            p_drct_declarator = p_drct_declarator->directDeclarator();
                            info("constraint primitive decl: ", p_drct_declarator->Identifier()->getText());
                            is_func_decl = true;
                            break;
                        }
                        if (p_drct_declarator->Identifier()) {
                            break;
                        }
                        p_drct_declarator = p_drct_declarator->directDeclarator();
                    }
                    if (p_drct_declarator == nullptr) {
                        panic("decl form not supported. identifier for Array not found.");
                    }
                    if (auto p_identifier = p_drct_declarator->Identifier()) {
                        name = p_identifier->getText();
                    } else {
                        panic("decl form not supported. identifier for Array not found.");
                    }
                    if (!is_func_decl) {
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
        }
        return 0;
    }
    std::any CConstraintVisitor::visitPrimaryExpression(c11parser::CParser::PrimaryExpressionContext *ctx) {
        if (ctx->Identifier()) {
            auto name = ctx->Identifier()->getText();
            if (std::find(m_primitive.cbegin(), m_primitive.cend(), name) != m_primitive.cend()) {
                // constraint primitives
                return m_solver_context.string_val(name);
            }
            if (auto sym = m_symbol_table.lookup_entry(name)->sym) {
                return *sym;
            }
            panic("can't find symbol \"" + name + "\".");
        }
        if (ctx->Constant()) {
            auto str = ctx->Constant()->getText();
            if (std::find(str.begin(), str.end(), '.') != str.end()) {
                auto num = std::stod(str);
                // 实数使用分数表示
                return m_solver_context.real_val(num * 1000, 1000);
            } else {
                // 整数
                auto num = std::stoll(str);
                return m_solver_context.int_val(num);
            }
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
                info("idx: ", idx.to_string());
                info("seq: ", prime_expr.to_string());
                // prime_expr = prime_expr.nth(idx);
                prime_expr = prime_expr[idx];
            } else if (p_post_op->argumentExpressionList() != nullptr) {
                // function form constraints
                if (!prime_expr.is_string_value()) {
                    panic("not a function");
                }
                const auto func_name = prime_expr.get_string();
                if (func_name == "_LENGTH") {
                    auto t = visit(p_post_op->argumentExpressionList());
                    auto args = std::any_cast<std::vector<z3::expr>>(t);
                    // seq theorem length primitive
                    prime_expr = args[0].length();
                } else if (func_name == "GAUSSIAN") {
                    auto t = visit(p_post_op->argumentExpressionList());
                    auto args = std::any_cast<std::vector<z3::expr>>(t);
                    stst_assert(args.size() == 3);
                    // stst_assert(args[0].is_const());
                    stst_assert(args[1].is_numeral());
                    stst_assert(args[2].is_numeral());
                    // auto name = args[0].to_string();
                    // C语法的字段访问似乎和json相似
                    auto name = p_post_op->argumentExpressionList()->assignmentExpression(0)->getText();
                    auto miu = args[1].as_double();
                    auto sigma = args[2].as_double();
                    // auto vari = m_symbol_table.lookup_entry(name);
                    // vari->cons |= GAUSSIAN;
                    // vari->miu = miu;
                    // vari->sigma = sigma;
                    m_gaussian_cons.emplace_back(args[0], miu, sigma);
                    info("Found GAUSSIAN for var: ", args[0].to_string());
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
                        for (auto getter: *bp.second.sym_getters) {
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
        // info(clause0.to_string());
        // info(clause1.to_string());

        std::any res;

        if (ctx->relop(0)->getText() == "<") {
            res = clause0 < clause1;
        } else if (ctx->relop(0)->getText() == "<=") {
            res = clause0 <= clause1;
        } else if (ctx->relop(0)->getText() == ">=") {
            res = clause0 >= clause1;
        } else if (ctx->relop(0)->getText() == ">") {
            res = clause0 > clause1;
        } else {
            unreachable();
        }

        all_expr_vector.push_back(std::any_cast<z3::expr>(res));
        unsigned id = all_expr_vector.size() - 1;

        update_constraint_val_map(clause0, id);
        update_constraint_val_map(clause1, id);

        return res;
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

        std::any res;
        if (ctx->eqop(0)->getText() == "!=") {
            res = clause0 != clause1;
        } else if (ctx->eqop(0)->getText() == "==") {
            res = clause0 == clause1;
        } else {
            unreachable();
        }
        all_expr_vector.push_back(std::any_cast<z3::expr>(res));
        unsigned id = all_expr_vector.size() - 1;
        update_constraint_val_map(clause0, id);
        update_constraint_val_map(clause1, id);
        return res;
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
        if (ctx->inclusiveOrExpression().size() == 1) {
            return visit(ctx->inclusiveOrExpression(0));
        }
        z3::expr expr = m_solver_context.bool_val(true);
        for (auto p_clause: ctx->inclusiveOrExpression()) {
            auto clause = std::any_cast<z3::expr>(visit(p_clause));
            // info("&& clause", clause.to_string());
            expr = expr && clause;
        }
        return expr;
    }
    std::any CConstraintVisitor::visitLogicalOrExpression(c11parser::CParser::LogicalOrExpressionContext *ctx) {
        if (ctx->logicalAndExpression().size() == 1) {
            return visit(ctx->logicalAndExpression(0));
        }
        z3::expr expr = m_solver_context.bool_val(false);
        static int or_class_id = 0;// 标识在一个或表达式中的所有子句
        for (auto p_clause: ctx->logicalAndExpression()) {
            auto clause = std::any_cast<z3::expr>(visit(p_clause));
            if (clause.is_bool()) {
                unsigned expr_id = all_expr_vector.size() - 1;
                assert(all_expr_vector[expr_id].to_string() == clause.to_string());// Debug
                info("add clause into or_expr_idmap: ", or_class_id, expr_id, clause.to_string());
                or_expr_idmap.emplace(expr_id, or_class_id);
            }
            expr = expr || clause;
        }
        or_class_id += 2;
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

    void CConstraintVisitor::update_constraint_val_map(z3::expr &clause, unsigned expr_id) {
        if (clause.is_numeral()) {
            return;
        }
        // fmt::println("update_constraint_val_map: clause {}, func kind {}", clause.to_string(), (int)clause.decl().decl_kind());
        auto clause_op = clause.decl().decl_kind();
        if (clause.is_arith() && clause.num_args() > 1 && clause_op != Z3_OP_SELECT && clause_op != Z3_OP_SEQ_NTH) {
            for (auto child: clause.args()) {
                update_constraint_val_map(child, expr_id);
            }
            return;
        }
        auto val_name = clause.to_string();
        if (constraint_val_expr_idmap.count(val_name) == 0) {
            info("found new val: ", val_name);
            constraint_val_list.push_back(clause);
            constraint_val_expr_idmap.emplace(val_name, std::vector<unsigned>());
        }
        constraint_val_expr_idmap.at(val_name).push_back(expr_id);
    }

    bool CConstraintVisitor::solve() {
        m_smt_solver.push();
        generate_gaussian();
        // info("checking sat: ", m_smt_solver.to_smt2());

        auto res = m_smt_solver.check();
        if (res == z3::unsat) {
            is_verbose println_local("constraint unsat");
            m_smt_solver.pop();
            return false;
        }
        if (res == z3::unknown) {
            is_verbose println_local("constraint unknown");
            is_verbose println_local("reason: {}", m_smt_solver.reason_unknown());
            m_smt_solver.pop();
            return false;
        }
        auto model = m_smt_solver.get_model();
        // is_verbose println_local("solver: {}\n", m_smt_solver.to_smt2());
        // is_verbose println_local("model: {}\n", model.to_string());
        m_smt_solver.pop();
        auto solve = json{};
        for (const auto &[name, entry]: m_symbol_table.get_scope(0)) {
            if (entry.qualifer == SymbolTableEntryQualifer::Primary) {
                if (entry.type == SymbolTableEntryType::Int32 ||
                    entry.type == SymbolTableEntryType::Int64 ||
                    entry.type == SymbolTableEntryType::UInt32 ||
                    entry.type == SymbolTableEntryType::UInt64) {
                    auto subst = model.get_const_interp(entry.sym->decl());
                    auto v = subst.as_int64();
                    if (entry.type == SymbolTableEntryType::Int32 || entry.type == SymbolTableEntryType::UInt32) {
                        if (v > INT_MAX || v < INT_MIN) {
                            throw std::exception();
                        }
                    }
                    solve[name] = v;
                } else if (entry.type == SymbolTableEntryType::Float32 || entry.type == SymbolTableEntryType::Float64) {
                    auto subst = model.get_const_interp(entry.sym->decl());
                    auto v = subst.as_double();
                    solve[name] = v;
                } else if (entry.type == SymbolTableEntryType::Struct) {
                    auto subst = model.get_const_interp(entry.sym->decl());
                    auto obj_json = process_z3_tuple(entry, model, subst);
                    solve[name] = obj_json;
                } else {
                    unreachable();
                }
            } else if (entry.qualifer == SymbolTableEntryQualifer::Array) {
                auto subst = model.get_const_interp(entry.sym->decl());
                auto array_json = process_z3_seq(entry.dims, subst, model, entry, entry_type_2_value_type(entry.type));
                solve[name] = array_json;
            } else if (entry.qualifer == SymbolTableEntryQualifer::Pointer) {
                auto subst = model.get_const_interp(entry.sym->decl());
                auto length = model.eval(subst.length()).as_int64();
                std::vector dims{static_cast<int>(length)};
                auto array_json = process_z3_seq(dims, subst, model, entry, entry_type_2_value_type(entry.type));
                solve[name] = array_json;
            }
        }

        m_cases.emplace(std::move(solve));
        cur_case = m_cases.size();
        return true;
    }

    void CConstraintVisitor::random_flip_expr(z3::expr_vector &original_exprs) {
        std::uniform_int_distribution<> random_bool(0,5);
        auto status = z3::unknown;
        while (status != z3::sat) {
            m_smt_solver.reset();
            for(auto exp : original_exprs) {
                if (random_bool(random_g)) {
                    m_smt_solver.add(!exp);
                } else {
                    m_smt_solver.add(exp);
                }
            }
            status = m_smt_solver.check();
        }
    }

    void CConstraintVisitor::mutateEntrance(const std::string &outpath) {
        cur_case = 0;
        output_path = outpath;
        info("after parse: ", m_smt_solver.to_smt2());
        auto original_exprs = m_smt_solver.assertions();
        if (positive == 'P' && m_smt_solver.check() != z3::sat) {
            info("The original constraint can not solve!");
            return;
        }

        for (int mutate_cycle = 1; cur_case < total_gen_cases; mutate_cycle++) {
            int this_cycle_begin_cases = cur_case;
            assert(constraint_val_cur_value.empty());
            // Rotate the constraint variable order to generate various cases.
            std::shuffle(constraint_val_list.begin(), constraint_val_list.end(), random_g);

            if (positive == 'N') {
                random_flip_expr(original_exprs);
                // In negative mode, we do not need to proceed or expr.
                or_expr_idmap.clear();
                constraint_val_expr_idmap.clear();
            }

            if (or_expr_idmap.empty()) {
                mutateVar(constraint_val_list.begin());
            } else {// 从若干或语句中任意激活一条
                m_smt_solver.push();
                int last_or_class = 0;
                std::vector<std::map<unsigned, int>::iterator> cur_or_exprs;
                or_expr_idmap.emplace(all_expr_vector.size(), -2);
                for (auto it = or_expr_idmap.begin(); it != or_expr_idmap.end(); it++) {
                    int cur_class = it->second >> 1;
                    if (it->second & 1) {
                        it->second--;
                    }
                    if (cur_class == last_or_class) {
                        cur_or_exprs.push_back(it);
                        continue;
                    }
                    std::uniform_int_distribution<size_t> rf{0, cur_or_exprs.size() - 1};
                    size_t choose = rf(random_g);
                    auto choosed_it = cur_or_exprs[choose];
                    choosed_it->second++;
                    info("Add or expr into solver: ", all_expr_vector[choosed_it->first].to_string());
                    m_smt_solver.add(all_expr_vector[choosed_it->first]);
                    last_or_class = cur_class;
                    cur_or_exprs.clear();
                    // Push the new class's first or_expr
                    cur_or_exprs.push_back(it);
                }
                if (m_smt_solver.check() == z3::sat) {
                    mutateVar(constraint_val_list.begin());
                }
                m_smt_solver.pop();
            }
            println_local("In mutate cycle {}, generated {} cases.", mutate_cycle, cur_case - this_cycle_begin_cases);
        }
    }
    void CConstraintVisitor::writeCases() {
        // 验证
        auto templ = R"(var f = () => {{
    var _LENGTH = (e) => {{
        return e.length;
    }};
    var GAUSSIAN = (v, miu, va) => {{
        return true;
    }};
    Object.assign(this, {});
    return {};
    }};
    f();
    )";
        std::string constraint_set{};
        bool first = true;
        for (const auto &con: m_cons_expressions) {
            if (!first) {
                constraint_set += " && ";
            } else {
                first = false;
            }
            constraint_set += "(";
            constraint_set += con;
            constraint_set += ")";
        }
        auto js_runtime = JS_NewRuntime();
        auto js_ctx = JS_NewContext(js_runtime);
        int cnt = 0;
        for (auto &single_case : m_cases) {
            auto js_src = fmt::format(templ, single_case.dump(), constraint_set);
            auto ret = JS_Eval(js_ctx, js_src.c_str(), js_src.size(), nullptr, 0);
            bool is_positive = JS_VALUE_GET_TAG(ret) == JS_TAG_BOOL && JS_VALUE_GET_BOOL(ret);
            if (positive == 'P' && !is_positive) {
                println_local("constraint NOT positive but required positive: {}", cnt + case_number_start);
                println_local("{}", single_case.dump(4));
                continue;
            }
            if (positive == 'N' && is_positive) {
                println_local("constraint NOT negative but required negative: {}", cnt + case_number_start);
                println_local("{}", single_case.dump(4));
                continue;
            }
            // Output
            std::filesystem::path outfile = output_path / fmt::format("{}{:05d}.json", positive, cnt + case_number_start);
            std::ofstream ofs(outfile);
            if (ofs.is_open()) {
                ofs << std::setw(4) << single_case;
                ofs.close();
            } else {
                info("Error: can not open", outfile.string(), "for output!");
                std::cout << std::setw(4) << single_case;
            }
            cnt++;
        }
        println_local("finish validating generated cases with QJS.");
    }

    z3::expr CConstraintVisitor::replaceKnownVar(z3::expr inp, int &unknown_count) {
        auto str = inp.to_string();
        if (inp.is_numeral()) {
            return inp;
        }
        if (constraint_val_expr_idmap.count(str)) {
            // 属于原子变量，终止递归
            if (constraint_val_cur_value.count(str)) {
                return m_solver_context.int_val(constraint_val_cur_value[str]);
            } else {
                unknown_count++;
                return inp;
            }
        }
        auto inp_args = inp.args();
        for (unsigned i = 0; i < inp_args.size(); i++) {
            z3::expr new_expr = replaceKnownVar(inp_args[i], unknown_count);
            inp_args.set(i, new_expr);
        }
        return inp.decl()(inp_args);
    }

    void CConstraintVisitor::mutateVar(expr_iter var_i) {
        if (var_i == constraint_val_list.end()) {
            solve();
            return;
        }
        auto val_name = (*var_i).to_string();
        info("Now mutate variable:", val_name);
        auto next_var_i = var_i;
        next_var_i++;

        // 更新变量可取范围
        int64_t val_min = INT_MIN, val_max = INT_MAX;
        if (val_name.find("seq.len") != std::string::npos) {
            val_min = 1;
            val_max = 100;
        }
        for (auto expr_id: constraint_val_expr_idmap[val_name]) {
            auto or_map_find_it = or_expr_idmap.find(expr_id);
            if (or_map_find_it != or_expr_idmap.end() && (or_map_find_it->second & 1) == 0) {
                continue;
            }
            int unknown_count = 0;
            auto expr = replaceKnownVar(all_expr_vector[expr_id], unknown_count).simplify();
            if (unknown_count > 1) {
                continue;
            }
            bool is_not_set = false;
            if (expr.decl().decl_kind() == Z3_OP_NOT) {
                is_not_set = true;
                expr = expr.arg(0);
            }

            assert(expr.num_args() == 2);
            auto clause0 = expr.arg(0);
            auto clause1 = expr.arg(1);
            bool val_is_clause0;
            if (clause0.to_string().find(val_name) != std::string::npos) {
                val_is_clause0 = true;
            } else if(clause1.to_string().find(val_name) != std::string::npos) {
                val_is_clause0 = false;
                clause1 = clause0;
                auto op = expr.decl().decl_kind();
                if (op == Z3_OP_LE) {
                    expr = clause1 >= clause0;
                } else if (op == Z3_OP_GE) {
                    expr = clause1 <= clause0;
                }
            }

            assert(clause1.is_numeral());
            int64_t right_value = clause1.get_numeral_int64();
            switch (expr.decl().decl_kind()) {
                case Z3_OP_EQ:
                    if (!is_not_set) {
                        val_max = right_value;
                        val_min = val_max;
                    }
                    break;
                case Z3_OP_LE:
                    if (is_not_set) // >
                        val_min = std::max(val_min, right_value + 1);
                    else // <=
                        val_max = std::min(val_max, right_value);
                    break;
                case Z3_OP_GE:
                    if (is_not_set) // <
                        val_max = std::min(val_max, right_value - 1);
                    else // >=
                        val_min = std::max(val_min, right_value);
                    break;
                default:
                    info("Unhandled expr: ", expr.to_string());
                    break;
            }
        }
        if (val_min > INT_MAX || val_max < INT_MIN || val_max < val_min) {
            info("Overflow, skip!");
            return;
        }
        // Random choose a number in [val_min, val_max]
        std::uniform_int_distribution<int> rf(val_min, val_max);
        uint64_t length = val_max - val_min + 1;
        constexpr uint64_t DEFAULT_VARIABLE_MUTATE_TIMES = 3;
        for (unsigned i = 0; i < std::min(length, DEFAULT_VARIABLE_MUTATE_TIMES) && cur_case < total_gen_cases; i++) {
            int assigned_value = rf(random_g);
            constraint_val_cur_value[val_name] = assigned_value;
            z3::expr cons = (*var_i) == assigned_value;
            m_smt_solver.push();
            m_smt_solver.add(cons);
            info(m_smt_solver.to_smt2());
            try {
                if (m_smt_solver.check() == z3::sat) {
                    solve();
                    mutateVar(next_var_i);
                }
            } catch(std::exception) {
                m_smt_solver.pop();
                break;
            }
            m_smt_solver.pop();
        }
        constraint_val_cur_value.erase(val_name);
    }

    void CConstraintVisitor::generate_gaussian() {
        constexpr int64_t precision = 1e10;
        for (auto &gauss_cons: m_gaussian_cons) {
            auto expr = gauss_cons.val;
            info(expr.to_string(), (int)expr.get_sort().sort_kind());
            auto rand_value = gauss_cons.normal_gen(random_g);
            m_smt_solver.add(expr == m_solver_context.real_val(rand_value * precision, precision));
        }
    }
}// namespace ststgen
