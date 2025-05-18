#pragma once

#include <exception>
#include <fmt/core.h>
#include <stdexcept>

namespace ststgen {
    extern int g_log_level;
    template<typename T1, typename... T2>
    void _log1(const T1 &x, const T2 &...xs) {
        fmt::print(stderr, "{} ", x);
        if constexpr (sizeof...(xs) > 0) {
            _log1(xs...);
        }
    }

    template<typename T1, typename... T2>
    void _log(const char *file, int line, const T1 &x, const T2 &...xs) {
        if (g_log_level > 0) {
            fmt::print(stderr, "{}:{}", file, line);
            if constexpr (sizeof...(xs) > 0) {
                _log1(xs...);
            }
            fmt::println(stderr, "\n");
        }
    }

#define info(...) ststgen::_log(__FILE__, __LINE__, __VA_ARGS__)
#define dbg(var) info(#var ": ", var)

#define panic(hint)                           \
    do {                                      \
        info("panic:", hint);                 \
        throw std::logic_error("user panic"); \
    } while (0)
#define stst_assert(predicate)                    \
    do {                                          \
        if (!(predicate)) {                       \
            info("assert failed: ", #predicate);  \
            throw std::logic_error("user assert"); \
        }                                         \
    } while (0)
#define unimplemented() panic("unimplemented")
#define unreachable() panic("unreachable")
#define todo() panic("todo")

}// namespace ststgen
