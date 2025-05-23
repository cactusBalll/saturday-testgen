#include "CBaseVisitor.h"
#include "CLexer.h"
#include "cmdline.h"
#include "parser.hpp"

#include <fmt/core.h>
#include <z3++.h>
#include <nlohmann/json.hpp>

// windows平台下CRT内存分析
// #define _CRTDBG_MAP_ALLOC
// #include <stdlib.h>
// #include <crtdbg.h>
using namespace z3;

void demorgan() {
    std::cout << "de-Morgan example\n";

    context c;

    expr x = c.bool_const("x");
    expr y = c.bool_const("y");
    expr conjecture = (!(x && y)) == (!x || !y);
    auto int_sort = c.int_sort();
    expr seq = c.constant("s", c.seq_sort(int_sort));

    solver s(c);
    // adding the negation of the conjecture as a constraint.
    s.add(conjecture);
    // std::cout << s << "\n";
    // std::cout << s.to_smt2() << "\n";
    // fmt::println("{}", x.to_string());
    // fmt::println("{}", seq.get_sort().to_string());
    switch (s.check()) {
        case unsat:
            std::cout << "de-Morgan is valid\n";
            break;
        case sat:
            std::cout << "de-Morgan is not valid\n";
            break;
        case unknown:
            std::cout << "unknown\n";
            break;
    }
    model m = s.get_model();
    fmt::println("model: {}", m.to_string());

}
/**
   \brief Find x and y such that: x ^ y - 103 == x * y
*/
void bitvector_example2() {
    std::cout << "bitvector example 2\n";
    context c;
    expr x = c.bv_const("x", 32);
    expr y = c.bv_const("y", 32);
    solver s(c);
    // In C++, the operator == has higher precedence than ^.
    s.add((x ^ y) - 103 == x * y);
    std::cout << s << "\n";
    std::cout << s.check() << "\n";
    std::cout << s.get_model() << "\n";
    fmt::println("model: {}", s.get_model().get_const_interp(x.decl()).to_string());
}
int main(int argc, char** argv) try {
    // demorgan();
    // bitvector_example2();
    // return 0;
    cmdline::parser cmd_parser;
    cmd_parser.add<int>(
            "num_cases",
            'n',
            "number of testcases to be generated",
            true,
            100,
            cmdline::range(1, 65536));
    cmd_parser.add<double>(
            "pos_ratio",
            'p',
            "ratio of positive test cases",
            true,
            1,
            cmdline::range(0.0,1.0)
        );
    cmd_parser.add<std::string>(
            "cons",
            'c',
            "constraint file path",
            true
        );
    cmd_parser.parse_check(argc, argv);
    int num_cases = cmd_parser.get<int>("num_cases");
    double pos_ratio = cmd_parser.get<double>("pos_ratio");
    int pos_cases = static_cast<int>(num_cases * pos_ratio);
    int neg_cases = num_cases - pos_cases;
    auto cons = cmd_parser.get<std::string>("cons");
    std::ifstream cons_in{cons};
    std::stringstream buffer{};
    buffer << cons_in.rdbuf();
    cons_in.close();
    std::string cons_src{buffer.str()};

    // antlr parser
    antlr4::ANTLRInputStream input_stream(cons_src);
    c11parser::CLexer lexer(&input_stream);
    antlr4::CommonTokenStream token_stream(&lexer);
    c11parser::CParser parser(&token_stream);
    auto *tree = parser.compilationUnit();
    // auto p_visitor = std::make_unique<ststgen::CConstraintVisitor>();
    // p_visitor->visit(tree);
    // p_visitor->solve();
    auto visitor = ststgen::CConstraintVisitor{};
    visitor.visit(tree);
    visitor.solve();

    // windows平台下CRT内存分析
    // _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    // _CrtDumpMemoryLeaks();
    return 0;
} catch (const std::exception &e) {
    fmt::println("main catch exception: {}", e.what());
}