#pragma once

#include <cstdio>
#include <utility>

namespace ipc {
namespace detail {

template <typename O>
void print(O out, char const * str) {
    std::fprintf(out, "%s", str);
}

// 单独加一个P1表示至少有一个参数，用于和不可变参的函数做区分
template <typename O, typename P1, typename... P>
void print(O out, char const * fmt, P1&& p1, P&&... params) {
    std::fprintf(out, fmt, std::forward<P1>(p1), std::forward<P>(params)...);
}

} // namespace detail

inline void log(char const * fmt) {
    ipc::detail::print(stdout, fmt);
}

template <typename P1, typename... P>
void log(char const * fmt, P1&& p1, P&&... params) {
    ipc::detail::print(stdout, fmt, std::forward<P1>(p1), std::forward<P>(params)...);
}

// 将错误信息输出到标准错误中
inline void error(char const * str) {
    ipc::detail::print(stderr, str);
}

template <typename P1, typename... P>
void error(char const * fmt, P1&& p1, P&&... params) {
    ipc::detail::print(stderr, fmt, std::forward<P1>(p1), std::forward<P>(params)...);
}

} // namespace ipc
