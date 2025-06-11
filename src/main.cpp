#include "CBaseVisitor.h"
#include "CLexer.h"
#include "cmdline.h"
#include "parser.hpp"

#include "utils.hpp"
#include <filesystem>
#include <fmt/core.h>
#include <nlohmann/json.hpp>
#include <z3++.h>

#include "quickjs.h"

// windows平台下CRT内存分析
// #define _CRTDBG_MAP_ALLOC
// #include <stdlib.h>
// #include <crtdbg.h>

std::mutex output_buffer_mutex{};

void core_runner(const std::string &cons_src, const std::string &output, int pos_cases, int start_case_num, int thread_i) {
    antlr4::ANTLRInputStream input_stream(cons_src);
    c11parser::CLexer lexer(&input_stream);
    antlr4::CommonTokenStream token_stream(&lexer);
    c11parser::CParser parser(&token_stream);
    auto *tree = parser.compilationUnit();
    auto visitor_positive = ststgen::CConstraintVisitor{pos_cases, true, start_case_num};
    visitor_positive.visit(tree);

    std::random_device rd;
    auto seed = rd();
    visitor_positive.setRandomSeed(seed);
    visitor_positive.mutateEntrance(output);
    visitor_positive.writeCases();

    {
        std::lock_guard<std::mutex> lock(output_buffer_mutex);
        fmt::println("\033[1;32mThread {} positive generator(seed: {}) output: \033[0m\n", thread_i, seed);
        visitor_positive.print();
    }
}

int main(int argc, char **argv) try {
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
            cmdline::range(0.0, 1.0));
    cmd_parser.add<std::string>(
            "cons",
            'c',
            "constraint file path",
            true);
    cmd_parser.add<std::string>(
            "output",
            'o',
            "output cases store path",
            false,
            "out");
    cmd_parser.add(
            "verbose",
            'v',
            "verbose output");
    cmd_parser.add<int>(
            "thread",
            'j',
            "number of threads",
            false,
            1);
    cmd_parser.parse_check(argc, argv);
    int num_cases = cmd_parser.get<int>("num_cases");
    double pos_ratio = cmd_parser.get<double>("pos_ratio");
    int pos_cases = static_cast<int>(num_cases * pos_ratio);
    int neg_cases = num_cases - pos_cases;
    auto cons = cmd_parser.get<std::string>("cons");
    std::ifstream cons_in{cons};
    std::string cons_src{std::istreambuf_iterator<char>(cons_in), std::istreambuf_iterator<char>()};
    cons_in.close();
    auto output = cmd_parser.get<std::string>("output");
    if (!std::filesystem::exists(output)) {
        std::filesystem::create_directories(output);
    }

    if (cmd_parser.exist("verbose")) {
        ststgen::g_log_level = 1;
    } else {
        ststgen::g_log_level = 0;
    }
    int thread_num = cmd_parser.get<int>("thread");
    thread_num = std::min(thread_num, 65535);

    // schedule threads
    auto case_per_thread = pos_cases / thread_num;
    const auto remained_cases = pos_cases - case_per_thread * (thread_num - 1);
    auto start_case_num = 0;
    std::vector<std::thread> threads;
        
    for (auto i = 1; i <= thread_num; i++) {
        if (i == thread_num) {
            // the last thread may not generate the same number of cases
            case_per_thread = remained_cases;
        }
        threads.emplace_back(std::thread(core_runner, cons_src, output, pos_cases, start_case_num, i));
        start_case_num += case_per_thread;
    }
    // wait for all thread finish their work
    for (auto &t: threads) {
        t.join();
    }
    fmt::println("ALL DONE");


    // windows平台下CRT内存分析
    // _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_DEBUG);
    // _CrtDumpMemoryLeaks();
    return 0;
} catch (const std::exception &e) {
    fmt::println("main catch exception: {}", e.what());
}