#include "CBaseVisitor.h"
#include <fmt/core.h>
#include <z3++.h>

using namespace z3;

void demorgan() {
    std::cout << "de-Morgan example\n";
    
    context c;

    expr x = c.bool_const("x");
    expr y = c.bool_const("y");
    expr conjecture = (!(x && y)) == (!x || !y);
    
    solver s(c);
    // adding the negation of the conjecture as a constraint.
    s.add(!conjecture);
    std::cout << s << "\n";
    std::cout << s.to_smt2() << "\n";
    fmt::println("{}", x.to_string());
    switch (s.check()) {
    case unsat:   std::cout << "de-Morgan is valid\n"; break;
    case sat:     std::cout << "de-Morgan is not valid\n"; break;
    case unknown: std::cout << "unknown\n"; break;
    }
}
int main() try {
    fmt::print("Hello World!\n");
    demorgan();
    return 0;
} catch (const std::exception& e) {
    fmt::println("main catch exception: {}", e.what());
}