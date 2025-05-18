#pragma once

#include <bitset>
#include <string>
#include <unordered_map>
#include <vector>

#include "CBaseVisitor.h"
#include "utils.hpp"

#include <any>
#include <optional>
#include <string_view>
#include <z3++.h>

namespace ststgen {
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
        std::vector<int> dims{};
        std::optional<z3::expr> sym = std::nullopt;
    };
    struct StructBlueprint {
        void push_member(const std::string &name, SymbolTableEntry &&member) {
            m_members.insert({name, std::move(member)});
        }
        std::unordered_map<std::string, SymbolTableEntry> m_members{};
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
        virtual std::any visitFunctionDefinition(c11parser::CParser::FunctionDefinitionContext *ctx) override;
        virtual std::any visitDeclaration(c11parser::CParser::DeclarationContext *ctx) override;
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

    private:
        SymbolTable m_symbol_table{};
        std::unordered_map<std::string, StructBlueprint> m_struct_blueprints{};
        bool m_process_constraint_statement = false;
        z3::context m_solver_context{};
        z3::solver m_smt_solver{m_solver_context};
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
            } else {
                unreachable();
            }
            if (entry.qualifer == SymbolTableEntryQualifer::Primary) {
                if (base_type == 0) {
                    entry.sym = m_solver_context.int_const(name.c_str());
                } else {
                    entry.sym = m_solver_context.real_const(name.c_str());
                }
            } else if (entry.qualifer == SymbolTableEntryQualifer::Array) {
                int size = 1;
                auto base_sort = base_type ? m_solver_context.real_sort() : m_solver_context.int_sort();
                for (auto iter = entry.dims.crbegin(); iter != entry.dims.crend(); ++iter) {
                    size *= *iter;
                }
                base_sort = m_solver_context.seq_sort(base_sort);
                entry.sym = m_solver_context.constant(name.c_str(), base_sort);
                // array size constraint
                m_smt_solver.add(entry.sym->length() == size);
            } else if (entry.qualifer == SymbolTableEntryQualifer::Pointer) {
                todo();
            } else {
                unreachable();
            }
            m_symbol_table.add_entry(name, entry);
        }
    };


    std::string make_member_name(const std::string &var, const std::string &member, const std::vector<int> &idx) {
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