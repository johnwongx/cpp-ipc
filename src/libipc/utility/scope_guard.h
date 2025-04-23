#pragma once

#include <utility>      // std::forward, std::move
#include <algorithm>    // std::swap
#include <type_traits>  // std::decay

namespace ipc {

////////////////////////////////////////////////////////////////
/// Execute guard function when the enclosing scope exits
////////////////////////////////////////////////////////////////

// 将析构函数传给本类，在声明周期结束时将调用析构函数
// 本类的dismiss 函数可以保证灵活控制是否需要析构，这往往用于操作失败时的数据清理
template <typename F>
class scope_guard {
    F destructor_;
    mutable bool dismiss_;

public:
    template <typename D>
    scope_guard(D && destructor)
        : destructor_(std::forward<D>(destructor))
        , dismiss_(false) {
    }

    // 使用swap 的方式交换dismiss 的值，保证只析构一次
    scope_guard(scope_guard&& rhs)
        : destructor_(std::move(rhs.destructor_))
        , dismiss_(true) /* dismiss rhs */ {
        std::swap(dismiss_, rhs.dismiss_);
    }

    // 如果析构失败，没有办法进行任何有意义的处理。最好的方式就是忽略，保证程序的健壮。
    ~scope_guard() {
        try { do_exit(); }
        /**
         * In the realm of exceptions, it is fundamental that you can do nothing
         * if your "undo/recover" action fails.
        */
        catch (...) { /* Do nothing */ }
    }

    void swap(scope_guard & rhs) {
        std::swap(destructor_, rhs.destructor_);
        std::swap(dismiss_   , rhs.dismiss_);
    }

    // 不需要自动析构
    void dismiss() const noexcept {
        dismiss_ = true;
    }

    void do_exit() {
        if (!dismiss_) {
            dismiss_ = true;
            destructor_();
        }
    }
};

template <typename D>
constexpr auto guard(D && destructor) noexcept {
	// 因为scope_guard 是右值引用，如果不使用decay_t 模版类型推导可能会错误
    return scope_guard<std::decay_t<D>> {
        std::forward<D>(destructor)
    };
}

} // namespace ipc