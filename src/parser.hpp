#pragma once

#include <bitset>
#include <random>
#include <string>
#include <unordered_map>
#include <vector>
#include <thread>

#include "CBaseVisitor.h"
#include "utils.hpp"

#include "quickjs.h"
#include <any>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <z3++.h>

namespace ststgen {

    using json = nlohmann::json;
    /// @brief 为展开的struct各成员命名
    std::string make_member_name(const std::string &var, const std::string &member, const std::vector<int> &idx);

    enum class SymbolTableEntryQualifer {
        Pointer,
        Array,
        Primary,
        Free,
    };
    enum class SymbolTableEntryType {
        None,
        Int32,
        Int64,
        UInt32,
        UInt64,
        Float32,
        Float64,
        Struct,
    };


    using ConsType = uint32_t;
    constexpr ConsType GAUSSIAN = 1;
    constexpr ConsType LENGTH = 1 << 1;
    constexpr ConsType BITVEC = 1 << 2;

    struct SymbolTableEntry {
        SymbolTableEntryQualifer qualifer = SymbolTableEntryQualifer::Free;
        SymbolTableEntryType type = SymbolTableEntryType::Int32;
        // constraints
        std::bitset<32> cons = 0b0;
        double miu = 0.0;
        double sigma = 0.0;
        size_t length = 0;
        std::string struct_name{};
        std::vector<int> dims{};
        std::optional<z3::expr> sym = std::nullopt;
    };
    struct GaussianCons {
        z3::expr val;
        std::normal_distribution<> normal_gen;
        GaussianCons(const z3::expr &v, double mu, double sigma) : val(v), normal_gen(mu, sigma) {}
    };
    struct StructBlueprint {
        void push_member(const std::string &name, SymbolTableEntry &&member) {
            m_members.insert({name, std::move(member)});
        }
        // ordered to find getters easier
        std::map<std::string, SymbolTableEntry> m_members{};
        std::optional<z3::func_decl> sym_constructor = std::nullopt;
        std::optional<z3::func_decl_vector> sym_getters = std::nullopt;
    };
    class SymbolTable {
    public:
        using ScopeTable = std::unordered_map<std::string, SymbolTableEntry>;

        void push_scope() {
            m_table.push_back(ScopeTable{});
        }

        void pop_scope() {
            m_table.pop_back();
        }

        ScopeTable &get_scope(const int level) {
            return m_table[level];
        }
        void add_entry(const std::string &name, const SymbolTableEntry &entry) {
            m_table.back().insert({name, entry});
        }

        int get_scope_level() const {
            return m_table.size();
        }

        SymbolTableEntry query_entry(const std::string &name) {
            for (auto scope = m_table.crbegin(); scope != m_table.crend(); ++scope) {
                if (scope->count(name) != 0) {
                    return SymbolTableEntry{scope->at(name)};
                }
            }
            // not found, free var
            return SymbolTableEntry{};
        }

        SymbolTableEntry *lookup_entry(const std::string &name) {
            for (auto scope = m_table.rbegin(); scope != m_table.rend(); ++scope) {
                if (scope->count(name) != 0) {
                    return &scope->at(name);
                }
            }
            return nullptr;
        }
        SymbolTable() {
            // global table
            m_table.push_back(ScopeTable{});
        }

    private:
        std::vector<ScopeTable> m_table{};
    };
    class CConstraintVisitor : public c11parser::CBaseVisitor {
    public:
        CConstraintVisitor(int case_number, bool is_positive, int case_number_start) : total_gen_cases(case_number), case_number_start(case_number_start) {
            positive = is_positive ? 'P' : 'N';
        }
        std::any visitFunctionDefinition(c11parser::CParser::FunctionDefinitionContext *ctx) override;
        std::any visitDeclaration(c11parser::CParser::DeclarationContext *ctx) override;
        // virtual std::any visitCompoundStatement(c11parser::CParser::CompoundStatementContext *ctx) override;
        // virtual std::any visitBlockItemList(c11parser::CParser::BlockItemListContext *ctx) override;
        // virtual std::any visitBlockItem(c11parser::CParser::BlockItemContext *ctx) override;
        std::any visitExpressionStatement(c11parser::CParser::ExpressionStatementContext *ctx) override;
        std::any visitPrimaryExpression(c11parser::CParser::PrimaryExpressionContext *ctx) override;

        std::any visitPostfixExpression(c11parser::CParser::PostfixExpressionContext *ctx) override;
        std::any visitArgumentExpressionList(c11parser::CParser::ArgumentExpressionListContext *ctx) override;
        std::any visitUnaryExpression(c11parser::CParser::UnaryExpressionContext *ctx) override;

        std::any visitCastExpression(c11parser::CParser::CastExpressionContext *ctx) override;
        std::any visitMultiplicativeExpression(c11parser::CParser::MultiplicativeExpressionContext *ctx) override;
        std::any visitAdditiveExpression(c11parser::CParser::AdditiveExpressionContext *ctx) override;
        std::any visitShiftExpression(c11parser::CParser::ShiftExpressionContext *ctx) override;
        std::any visitRelationalExpression(c11parser::CParser::RelationalExpressionContext *ctx) override;
        std::any visitEqualityExpression(c11parser::CParser::EqualityExpressionContext *ctx) override;
        std::any visitAndExpression(c11parser::CParser::AndExpressionContext *ctx) override;
        std::any visitExclusiveOrExpression(c11parser::CParser::ExclusiveOrExpressionContext *ctx) override;
        std::any visitInclusiveOrExpression(c11parser::CParser::InclusiveOrExpressionContext *ctx) override;
        std::any visitLogicalAndExpression(c11parser::CParser::LogicalAndExpressionContext *ctx) override;
        std::any visitLogicalOrExpression(c11parser::CParser::LogicalOrExpressionContext *ctx) override;
        std::any visitConditionalExpression(c11parser::CParser::ConditionalExpressionContext *ctx) override;
        std::any visitAssignmentExpression(c11parser::CParser::AssignmentExpressionContext *ctx) override;
        std::any visitExpression(c11parser::CParser::ExpressionContext *ctx) override;
        using expr_iter = std::vector<z3::expr>::iterator;
        bool solve();
        void update_constraint_val_map(z3::expr &clause, unsigned expr_id);
        void mutateVar(expr_iter var_i);
        void setRandomSeed(unsigned s) {
            random_g = std::mt19937_64(s);
        }
        void mutateEntrance(const std::string &outpath);
        void writeCases();
        void generate_gaussian();
        z3::expr replaceKnownVar(z3::expr inp, int &unknown_count);
        void random_flip_expr(z3::expr_vector &original_exprs);
        void print() {
            fmt::print("{}", fmt::to_string(local_log));
        }


    private:
        // 析构顺序相关，因为是反方向依次析构，所以必须保证求解器和求解器上下文在最前面
        z3::context m_solver_context{};
        z3::solver m_smt_solver{m_solver_context};
        const std::vector<std::string> m_primitive{"_LENGTH", "GAUSSIAN"};
        SymbolTable m_symbol_table{};
        std::unordered_map<std::string, StructBlueprint> m_struct_blueprints{};
        std::vector<GaussianCons> m_gaussian_cons{};
        bool m_process_constraint_statement = false;
        std::mt19937_64 random_g;
        unsigned total_gen_cases, cur_case;
        char positive;
        std::filesystem::path output_path;
        std::vector<z3::expr> constraint_val_list{};
        std::vector<z3::expr> all_expr_vector{};
        std::map<unsigned, int> or_expr_idmap;
        std::map<std::string, std::vector<unsigned>> constraint_val_expr_idmap;
        std::map<std::string, int> constraint_val_cur_value;

        std::string m_cons_src{};
        std::vector<std::string> m_cons_expressions{};
        std::unordered_set<json> m_cases{};

        fmt::memory_buffer local_log{};

    private:
        int case_number_start = 0;

        template<typename... T>
        void println_local(fmt::format_string<T...> fmt, T &&...args) {
            auto iter = fmt::format_to(std::back_inserter(local_log), fmt, args...);
            fmt::format_to(iter, "\n");
        }
        /// @deprecated
        void set_length_constraint(const z3::expr &seq, const std::vector<int> &dims) {
            auto t = std::vector(dims);
            set_length_constraint_rec(seq, t);
        }
        void set_length_constraint_rec(const z3::expr &seq, std::vector<int> &dims) {
            if (dims.empty()) {
                return;
            }
            m_smt_solver.add(seq.length() == dims.at(0));
            int dim = dims.at(0);
            dims.erase(dims.cbegin());
            for (int i = 0; i < dim; i++) {
                auto idx = m_solver_context.int_val(i);
                auto sub_seq = seq.nth(idx);
                set_length_constraint_rec(sub_seq, dims);
            }
            dims.insert(dims.cbegin(), dim);
        }
        void insert_entry(const std::string &name, SymbolTableEntry entry) {
            // 0 for Int, 1 for Real
            int base_type = 0;
            if (entry.type == SymbolTableEntryType::Int32 ||
                entry.type == SymbolTableEntryType::Int64 ||
                entry.type == SymbolTableEntryType::UInt32 ||
                entry.type == SymbolTableEntryType::UInt64) {
                base_type = 0;
            } else if (entry.type == SymbolTableEntryType::Float32 || entry.type == SymbolTableEntryType::Float64) {
                base_type = 1;
            } else if (entry.type == SymbolTableEntryType::Struct) {
                base_type = 2;
            } else {
                unreachable();
            }
            z3::sort base_sort{m_solver_context};
            if (base_type == 0) {
                base_sort = m_solver_context.int_sort();
            } else if (base_type == 1) {
                base_sort = m_solver_context.real_sort();
            } else {
                const auto &tup_constructor = m_struct_blueprints.at(entry.struct_name).sym_constructor;
                auto tup_sort = tup_constructor->range();
                base_sort = tup_sort;
            }
            if (entry.qualifer == SymbolTableEntryQualifer::Primary) {
                entry.sym = m_solver_context.constant(name.c_str(), base_sort);
            } else if (entry.qualifer == SymbolTableEntryQualifer::Array) {
                int all_l = 1;
                int outermost_l = 1;
                for (auto iter = entry.dims.crbegin(); iter != entry.dims.crend(); ++iter) {
                    // base_sort = m_solver_context.seq_sort(base_sort);
                    base_sort = m_solver_context.array_sort(m_solver_context.int_sort(), base_sort);
                    all_l *= *iter;
                    outermost_l = *iter;
                }
                entry.sym = m_solver_context.constant(name.c_str(), base_sort);
                // array size constraint
                // m_smt_solver.add(entry.sym->length() == outermost_l);
                // set_length_constraint(*entry.sym, entry.dims);

            } else if (entry.qualifer == SymbolTableEntryQualifer::Pointer) {
                // seq sort without length constraint
                base_sort = m_solver_context.seq_sort(base_sort);
                entry.sym = m_solver_context.constant(name.c_str(), base_sort);
            } else {
                unreachable();
            }
            m_symbol_table.add_entry(name, entry);
        }
        enum class ValueType {
            Int,
            Int64,
            Real,
            Struct,
        };
        json process_z3_seq(const std::vector<int> &dims, const z3::expr &seq, const z3::model &model, const SymbolTableEntry &entry, ValueType value_type = ValueType::Int) {
            return process_z3_seq_rec(dims, seq, model, entry, value_type, 0);
        }

        json process_z3_seq_rec(const std::vector<int> &dims, const z3::expr &seq, const z3::model &model, const SymbolTableEntry &entry, ValueType value_type, int depth) {
            auto ret = json::array();
            if (depth == dims.size() - 1) {
                for (int i = 0; i < dims[depth]; ++i) {
                    auto idx = m_solver_context.int_val(i);
                    auto v_sym = model.eval(seq[idx]);
                    if (value_type == ValueType::Int) {
                        auto r = v_sym.as_int64();
                        if (r > INT_MAX || r < INT_MIN) {
                            throw std::exception();
                        }
                        ret.push_back(r);
                    }
                    if (value_type == ValueType::Int64) {
                        auto r = v_sym.as_int64();
                        ret.push_back(r);
                    }
                    if (value_type == ValueType::Real) {
                        ret.push_back(v_sym.as_double());
                    }
                    if (value_type == ValueType::Struct) {
                        ret.push_back(process_z3_tuple(entry, model, v_sym));
                    }
                }
                return ret;
            }
            for (int i = 0; i < dims[depth]; ++i) {
                auto idx = m_solver_context.int_val(i);
                auto v_sym = seq[idx];
                auto t_ret = process_z3_seq_rec(dims, v_sym, model, entry, value_type, depth + 1);
                ret.push_back(t_ret);
            }
            return ret;
        }

        json process_z3_tuple(const SymbolTableEntry &entry, const z3::model &model, const z3::expr &subst) {
            auto ret = json::object();
            const auto &blueprint = m_struct_blueprints[entry.struct_name];
            int idx = 0;
            dbg(blueprint.sym_getters->size());
            for (const auto &[member_name, member_entry]: blueprint.m_members) {
                auto getter_sym = (*blueprint.sym_getters)[idx];
                auto t = getter_sym(subst);
                auto member_sym = model.eval(t);
                if (member_entry.qualifer == SymbolTableEntryQualifer::Primary) {
                    if (member_entry.type == SymbolTableEntryType::Int32 ||
                        member_entry.type == SymbolTableEntryType::Int64 ||
                        member_entry.type == SymbolTableEntryType::UInt32 ||
                        member_entry.type == SymbolTableEntryType::UInt64) {
                        auto v = member_sym.as_int64();
                        if (entry.type == SymbolTableEntryType::Int32 || entry.type == SymbolTableEntryType::UInt32) {
                            if (v > INT_MAX || v < INT_MIN) {
                                throw std::exception();
                            }
                        }
                        ret[member_name] = v;
                    } else if (member_entry.type == SymbolTableEntryType::Float32 ||
                               member_entry.type == SymbolTableEntryType::Float64) {
                        ret[member_name] = member_sym.as_double();
                    } else {
                        unreachable();
                    }
                }
                if (member_entry.qualifer == SymbolTableEntryQualifer::Array) {
                    auto member_array_json = process_z3_seq(entry.dims, member_sym, model, entry, entry_type_2_value_type(member_entry.type));
                    ret[member_name] = member_array_json;
                }
                if (member_entry.qualifer == SymbolTableEntryQualifer::Pointer) {
                    // pointers are actually handled as 1-D arrays
                    auto length = model.eval(member_sym.length()).as_int64();
                    std::vector dims{static_cast<int>(length)};
                    auto member_array_json = process_z3_seq(dims, member_sym, model, entry, entry_type_2_value_type(member_entry.type));
                    ret[member_name] = member_array_json;
                }

                idx += 1;
            }
            return ret;
        }

        static ValueType entry_type_2_value_type(SymbolTableEntryType type) {
            if (type == SymbolTableEntryType::Int32 ||
                type == SymbolTableEntryType::UInt32) {
                return ValueType::Int;
            }
            if (type == SymbolTableEntryType::Int64 ||
                type == SymbolTableEntryType::UInt64) {
                return ValueType::Int64;
            }
            if (type == SymbolTableEntryType::Float32 ||
                type == SymbolTableEntryType::Float64) {
                return ValueType::Real;
            }
            if (type == SymbolTableEntryType::Struct) {
                return ValueType::Struct;
            }
            panic("not supported");
        }
    };


    inline std::string make_member_name(const std::string &var, const std::string &member, const std::vector<int> &idx) {
        auto ret = std::string{var};
        ret += "__m__";
        ret += member;
        for (auto i: idx) {
            ret += "__i";
            ret += std::to_string(i);
        }
        return ret;
    }


}// namespace ststgen