#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>

#include "libipc/def.h"
#include "libipc/rw_lock.h"

#include "libipc/platform/detail.h"

namespace ipc {
namespace circ {

// 使用using 可以减少后续修改数据类型时涉及的修改位置
using u1_t = ipc::uint_t<8>;
using u2_t = ipc::uint_t<32>;

/** only supports max 32 connections in broadcast mode */
using cc_t = u2_t;

// 低八位保存index 信息
constexpr u1_t index_of(u2_t c) noexcept {
    return static_cast<u1_t>(c);
}

// 用来记录连接的情况，比如个数和id（只在广播模式下有）
class conn_head_base {
protected:
    std::atomic<cc_t> cc_{0}; // connections
    ipc::spin_lock lc_;
    std::atomic<bool> constructed_{false};

public:
    // 内存擦除
    void init() {
        /* DCLP(双重检查锁模式) */
        if (!constructed_.load(std::memory_order_acquire)) {
            IPC_UNUSED_ auto guard = ipc::detail::unique_lock(lc_);
            // 因为有锁的存在，保证了代码顺序。所以此处可以使用松散顺序
            if (!constructed_.load(std::memory_order_relaxed)) {
                // 在当前内存位置上重新构造一个对象
                ::new (this) conn_head_base;
                // 使用release 顺序保证了内存申请操作不会重排到本句之后
                constructed_.store(true, std::memory_order_release);
            }
        }
    }

    conn_head_base() = default;
    conn_head_base(conn_head_base const &) = delete;
    conn_head_base &operator=(conn_head_base const &) = delete;

    // 获取连接的数量
    cc_t connections(std::memory_order order = std::memory_order_acquire) const noexcept {
        return this->cc_.load(order);
    }
};

template <typename P, bool = relat_trait<P>::is_broadcast>
class conn_head;

// 每个位代表一个连接对象，4字节32位最多有32个连接
template <typename P>
class conn_head<P, true> : public conn_head_base {
public:
    // 注册一个连接标记位，并返回该标记位
    cc_t connect() noexcept {
        for (unsigned k = 0;; ipc::yield(k)) {
            cc_t curr = this->cc_.load(std::memory_order_acquire);
            // eg. 0b01 => 0b11
            cc_t next = curr | (curr + 1); // find the first 0, and set it to 1.
            if (next == curr) {
                // connection-slot is full.
                return 0;
            }
            if (this->cc_.compare_exchange_weak(curr, next, std::memory_order_release)) {
                return next ^ curr; // return connected id
            }
        }
    }

    cc_t disconnect(cc_t cc_id) noexcept {
        return this->cc_.fetch_and(~cc_id, std::memory_order_acq_rel) & ~cc_id;
    }

    std::size_t conn_count(std::memory_order order = std::memory_order_acquire) const noexcept {
        cc_t cur = this->cc_.load(order);
        cc_t cnt; // accumulates the total bits set in cc
        for (cnt = 0; cur; ++cnt) cur &= cur - 1;
        return cnt;
    }
};

template <typename P>
class conn_head<P, false> : public conn_head_base {
public:
    cc_t connect() noexcept {
        return this->cc_.fetch_add(1, std::memory_order_relaxed) + 1;
    }

    cc_t disconnect(cc_t cc_id) noexcept {
        if (cc_id == ~static_cast<circ::cc_t>(0u)) {
            // clear all connections
            this->cc_.store(0, std::memory_order_relaxed);
            return 0u;
        }
        else {
            return this->cc_.fetch_sub(1, std::memory_order_relaxed) - 1;
        }
    }

    std::size_t conn_count(std::memory_order order = std::memory_order_acquire) const noexcept {
        return this->connections(order);
    }
};

} // namespace circ
} // namespace ipc
