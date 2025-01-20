#pragma once

#include <utility>      // std::forward, std::move
#include <algorithm>    // std::swap
#include <type_traits>  // std::decay

namespace ipc {

////////////////////////////////////////////////////////////////
/// Execute guard function when the enclosing scope exits
////////////////////////////////////////////////////////////////

// �����������������࣬���������ڽ���ʱ��������������
// �����dismiss �������Ա�֤�������Ƿ���Ҫ���������������ڲ���ʧ��ʱ����������
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

    // ʹ��swap �ķ�ʽ����dismiss ��ֵ����ֻ֤����һ��
    scope_guard(scope_guard&& rhs)
        : destructor_(std::move(rhs.destructor_))
        , dismiss_(true) /* dismiss rhs */ {
        std::swap(dismiss_, rhs.dismiss_);
    }

    // �������ʧ�ܣ�û�а취�����κ�������Ĵ�����õķ�ʽ���Ǻ��ԣ���֤����Ľ�׳��
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

    // �����������Ƿ���Ҫ����
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
	// ��Ϊscope_guard ����ֵ���ã������ʹ��decay_t ģ�������Ƶ����ܻ����
    return scope_guard<std::decay_t<D>> {
        std::forward<D>(destructor)
    };
}

} // namespace ipc