#pragma once

#include <exception>
#include <fmt/core.h>
#include <mutex>
#include <stdexcept>

namespace ststgen {
    extern int g_log_level;
    extern std::mutex g_log_mutex;
    template<typename T1, typename... T2>
    void _log1(const T1 &x, const T2 &...xs) {
        fmt::print(stderr, "{} ", x);
        if constexpr (sizeof...(xs) > 0) {
            _log1(xs...);
        }
    }

    template<typename T1, typename... T2>
    void _log(const char *file, int line, const T1 &x, const T2 &...xs) {
        std::lock_guard<std::mutex> lock(g_log_mutex);
        if (g_log_level > 0) {
            fmt::print(stderr, "{}:{} ", file, line);
            fmt::print(stderr, "{} ", x);
            if constexpr (sizeof...(xs) > 0) {
                _log1(xs...);
            }
            fmt::println(stderr, "\n");
        }
    }

#define info(...) ststgen::_log(__FILE__, __LINE__, __VA_ARGS__)
#define dbg(var) ststgen::_log(__FILE__, __LINE__, #var ": ", var)

#define panic(hint)                                         \
    do {                                                    \
        ststgen::_log(__FILE__, __LINE__, "panic: ", hint); \
        throw std::logic_error("user panic");               \
    } while (0)
#define stst_assert(predicate)                                                \
    do {                                                                      \
        if (!(predicate)) {                                                   \
            ststgen::_log(__FILE__, __LINE__, "assert failed: ", #predicate); \
            throw std::logic_error("user assert");                            \
        }                                                                     \
    } while (0)
#define unimplemented() panic("unimplemented")
#define unreachable() panic("unreachable")
#define todo() panic("todo")
#define is_verbose if(g_log_level > 0)

}// namespace ststgen
