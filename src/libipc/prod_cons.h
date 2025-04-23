#pragma once

#include <atomic>
#include <utility>
#include <cstring>
#include <type_traits>
#include <cstdint>

#include "libipc/def.h"

#include "libipc/platform/detail.h"
#include "libipc/circ/elem_def.h"
#include "libipc/utility/log.h"
#include "libipc/utility/utility.h"

namespace ipc {

////////////////////////////////////////////////////////////////
/// producer-consumer implementation
////////////////////////////////////////////////////////////////

template <typename Flag>
struct prod_cons_impl;

template <>
struct prod_cons_impl<wr<relat::single, relat::single, trans::unicast>> {

    template <std::size_t DataSize, std::size_t AlignSize>
    struct elem_t {
        std::aligned_storage_t<DataSize, AlignSize> data_ {};
    };

    alignas(cache_line_size) std::atomic<circ::u2_t> rd_; // read index
    alignas(cache_line_size) std::atomic<circ::u2_t> wt_; // write index

    constexpr circ::u2_t cursor() const noexcept {
        return 0;
    }

    template <typename W, typename F, typename E>
    bool push(W* /*wrapper*/, F&& f, E* elems) {
        auto cur_wt = circ::index_of(wt_.load(std::memory_order_relaxed));
        if (cur_wt == circ::index_of(rd_.load(std::memory_order_acquire) - 1)) {
            return false; // full
        }
        std::forward<F>(f)(&(elems[cur_wt].data_));
        wt_.fetch_add(1, std::memory_order_release);
        return true;
    }

    /**
     * In single-single-unicast, 'force_push' means 'no reader' or 'the only one reader is dead'.
     * So we could just disconnect all connections of receiver, and return false.
    */
    template <typename W, typename F, typename E>
    bool force_push(W* wrapper, F&&, E*) {
        wrapper->elems()->disconnect_receiver(~static_cast<circ::cc_t>(0u));
        return false;
    }

    template <typename W, typename F, typename R, typename E>
    bool pop(W* /*wrapper*/, circ::u2_t& /*cur*/, F&& f, R&& out, E* elems) {
        auto cur_rd = circ::index_of(rd_.load(std::memory_order_relaxed));
        if (cur_rd == circ::index_of(wt_.load(std::memory_order_acquire))) {
            return false; // empty
        }
        std::forward<F>(f)(&(elems[cur_rd].data_));
        std::forward<R>(out)(true);
        rd_.fetch_add(1, std::memory_order_release);
        return true;
    }
};

template <>
struct prod_cons_impl<wr<relat::single, relat::multi , trans::unicast>>
     : prod_cons_impl<wr<relat::single, relat::single, trans::unicast>> {

    template <typename W, typename F, typename E>
    bool force_push(W* wrapper, F&&, E*) {
        wrapper->elems()->disconnect_receiver(1);
        return false;
    }

    template <typename W, typename F, typename R, 
              template <std::size_t, std::size_t> class E, std::size_t DS, std::size_t AS>
    bool pop(W* /*wrapper*/, circ::u2_t& /*cur*/, F&& f, R&& out, E<DS, AS>* elems) {
        byte_t buff[DS];
        for (unsigned k = 0;;) {
            auto cur_rd = rd_.load(std::memory_order_relaxed);
            if (circ::index_of(cur_rd) ==
                circ::index_of(wt_.load(std::memory_order_acquire))) {
                return false; // empty
            }
            std::memcpy(buff, &(elems[circ::index_of(cur_rd)].data_), sizeof(buff));
            if (rd_.compare_exchange_weak(cur_rd, cur_rd + 1, std::memory_order_release)) {
                std::forward<F>(f)(buff);
                std::forward<R>(out)(true);
                return true;
            }
            ipc::yield(k);
        }
    }
};

template <>
struct prod_cons_impl<wr<relat::multi , relat::multi, trans::unicast>>
     : prod_cons_impl<wr<relat::single, relat::multi, trans::unicast>> {

    using flag_t = std::uint64_t;

    template <std::size_t DataSize, std::size_t AlignSize>
    struct elem_t {
        std::aligned_storage_t<DataSize, AlignSize> data_ {};
        std::atomic<flag_t> f_ct_ { 0 }; // commit flag
    };

    alignas(cache_line_size) std::atomic<circ::u2_t> ct_; // commit index

    template <typename W, typename F, typename E>
    bool push(W* /*wrapper*/, F&& f, E* elems) {
        circ::u2_t cur_ct, nxt_ct;
        for (unsigned k = 0;;) {
            cur_ct = ct_.load(std::memory_order_relaxed);
            if (circ::index_of(nxt_ct = cur_ct + 1) ==
                circ::index_of(rd_.load(std::memory_order_acquire))) {
                return false; // full
            }
            if (ct_.compare_exchange_weak(cur_ct, nxt_ct, std::memory_order_acq_rel)) {
                break;
            }
            ipc::yield(k);
        }
        auto* el = elems + circ::index_of(cur_ct);
        std::forward<F>(f)(&(el->data_));
        // set flag & try update wt
        el->f_ct_.store(~static_cast<flag_t>(cur_ct), std::memory_order_release);
        while (1) {
            auto cac_ct = el->f_ct_.load(std::memory_order_acquire);
            if (cur_ct != wt_.load(std::memory_order_relaxed)) {
                return true;
            }
            if ((~cac_ct) != cur_ct) {
                return true;
            }
            if (!el->f_ct_.compare_exchange_strong(cac_ct, 0, std::memory_order_relaxed)) {
                return true;
            }
            wt_.store(nxt_ct, std::memory_order_release);
            cur_ct = nxt_ct;
            nxt_ct = cur_ct + 1;
            el = elems + circ::index_of(cur_ct);
        }
        return true;
    }

    template <typename W, typename F, typename E>
    bool force_push(W* wrapper, F&&, E*) {
        wrapper->elems()->disconnect_receiver(1);
        return false;
    }

    template <typename W, typename F, typename R, 
              template <std::size_t, std::size_t> class E, std::size_t DS, std::size_t AS>
    bool pop(W* /*wrapper*/, circ::u2_t& /*cur*/, F&& f, R&& out, E<DS, AS>* elems) {
        byte_t buff[DS];
        for (unsigned k = 0;;) {
            auto cur_rd = rd_.load(std::memory_order_relaxed);
            auto cur_wt = wt_.load(std::memory_order_acquire);
            auto id_rd  = circ::index_of(cur_rd);
            auto id_wt  = circ::index_of(cur_wt);
            if (id_rd == id_wt) {
                auto* el = elems + id_wt;
                auto cac_ct = el->f_ct_.load(std::memory_order_acquire);
                if ((~cac_ct) != cur_wt) {
                    return false; // empty
                }
                if (el->f_ct_.compare_exchange_weak(cac_ct, 0, std::memory_order_relaxed)) {
                    wt_.store(cur_wt + 1, std::memory_order_release);
                }
                k = 0;
            }
            else {
                std::memcpy(buff, &(elems[circ::index_of(cur_rd)].data_), sizeof(buff));
                if (rd_.compare_exchange_weak(cur_rd, cur_rd + 1, std::memory_order_release)) {
                    std::forward<F>(f)(buff);
                    std::forward<R>(out)(true);
                    return true;
                }
                ipc::yield(k);
            }
        }
    }
};

template <>
struct prod_cons_impl<wr<relat::single, relat::multi, trans::broadcast>> {

    using rc_t = std::uint64_t;

    enum : rc_t {
        ep_mask = 0x00000000ffffffffull,
        ep_incr = 0x0000000100000000ull
    };

    template <std::size_t DataSize, std::size_t AlignSize>
    struct elem_t {
        std::aligned_storage_t<DataSize, AlignSize> data_ {};
        std::atomic<rc_t> rc_ { 0 }; // read-counter
    };

    alignas(cache_line_size) std::atomic<circ::u2_t> wt_;   // write index
    alignas(cache_line_size) rc_t epoch_ { 0 };             // only one writer

    circ::u2_t cursor() const noexcept {
        return wt_.load(std::memory_order_acquire);
    }

    template <typename W, typename F, typename E>
    bool push(W* wrapper, F&& f, E* elems) {
        E* el;
        for (unsigned k = 0;;) {
            circ::cc_t cc = wrapper->elems()->connections(std::memory_order_relaxed);
            if (cc == 0) return false; // no reader
            el = elems + circ::index_of(wt_.load(std::memory_order_relaxed));
            // check all consumers have finished reading this element
            auto cur_rc = el->rc_.load(std::memory_order_acquire);
            circ::cc_t rem_cc = cur_rc & ep_mask;
            if ((cc & rem_cc) && ((cur_rc & ~ep_mask) == epoch_)) {
                return false; // has not finished yet
            }
            // consider rem_cc to be 0 here
            if (el->rc_.compare_exchange_weak(
                        cur_rc, epoch_ | static_cast<rc_t>(cc), std::memory_order_release)) {
                break;
            }
            ipc::yield(k);
        }
        std::forward<F>(f)(&(el->data_));
        wt_.fetch_add(1, std::memory_order_release);
        return true;
    }

    template <typename W, typename F, typename E>
    bool force_push(W* wrapper, F&& f, E* elems) {
        E* el;
        epoch_ += ep_incr;
        for (unsigned k = 0;;) {
            circ::cc_t cc = wrapper->elems()->connections(std::memory_order_relaxed);
            if (cc == 0) return false; // no reader
            el = elems + circ::index_of(wt_.load(std::memory_order_relaxed));
            // check all consumers have finished reading this element
            auto cur_rc = el->rc_.load(std::memory_order_acquire);
            circ::cc_t rem_cc = cur_rc & ep_mask;
            if (cc & rem_cc) {
                ipc::log("force_push: k = %u, cc = %u, rem_cc = %u\n", k, cc, rem_cc);
                cc = wrapper->elems()->disconnect_receiver(rem_cc); // disconnect all invalid readers
                if (cc == 0) return false; // no reader
            }
            // just compare & exchange
            if (el->rc_.compare_exchange_weak(
                        cur_rc, epoch_ | static_cast<rc_t>(cc), std::memory_order_release)) {
                break;
            }
            ipc::yield(k);
        }
        std::forward<F>(f)(&(el->data_));
        wt_.fetch_add(1, std::memory_order_release);
        return true;
    }

    template <typename W, typename F, typename R, typename E>
    bool pop(W* wrapper, circ::u2_t& cur, F&& f, R&& out, E* elems) {
        if (cur == cursor()) return false; // acquire
        auto* el = elems + circ::index_of(cur++);
        std::forward<F>(f)(&(el->data_));
        for (unsigned k = 0;;) {
            auto cur_rc = el->rc_.load(std::memory_order_acquire);
            if ((cur_rc & ep_mask) == 0) {
                std::forward<R>(out)(true);
                return true;
            }
            auto nxt_rc = cur_rc & ~static_cast<rc_t>(wrapper->connected_id());
            if (el->rc_.compare_exchange_weak(cur_rc, nxt_rc, std::memory_order_release)) {
                std::forward<R>(out)((nxt_rc & ep_mask) == 0);
                return true;
            }
            ipc::yield(k);
        }
    }
};

// 多对多的偏特化实现
template <>
struct prod_cons_impl<wr<relat::multi, relat::multi, trans::broadcast>> {

    using rc_t   = std::uint64_t;
    using flag_t = std::uint64_t;

    // 8个字节分为三个部分, 下边的宏都是这些部分的掩码和单位增加值
    // rc(read counter): 低位四字节, 用于表示数据的读取情况。一个数据未被读取时，其值等同于connections,
    //                   每个消费者读取后将自己的位置为0
    // ic(index counter): 中间三字节, 数据的唯一索引, 增量计数器
    // ep(epoch): 高位一字节,用于区分不同时间段，防止在并发环境下被重复处理
    enum : rc_t {
        rc_mask = 0x00000000ffffffffull, // read count?
        ep_mask = 0x00ffffffffffffffull,
        ep_incr = 0x0100000000000000ull,
        ic_mask = 0xff000000ffffffffull,
        ic_incr = 0x0000000100000000ull // ic 每次增加的值
    };

    template <std::size_t DataSize, std::size_t AlignSize>
    struct elem_t {
        // std::aligned_storage_t: 申请了一块指定大小和对其的内存，但没有初始化。
        // 结合placement new 使用
        // 实际类型是：msg_t
        std::aligned_storage_t<DataSize, AlignSize> data_ {};
		// 一个综合的（rc,ep,ic)的读取情况信息
        std::atomic<rc_t  > rc_   { 0 }; // read-counter
        std::atomic<flag_t> f_ct_ { 0 }; // commit flag
    };

    // alignas :指定对齐长度
    // commit index: 每放入一个数据自增1，使用低八位表示实际在数组中的位置（循环队列）
    alignas(cache_line_size) std::atomic<circ::u2_t> ct_;
    alignas(cache_line_size) std::atomic<rc_t> epoch_ { 0 }; // 时间段标识，用于并发环境, 防止数据重入

    // 返回提交序号
    circ::u2_t cursor() const noexcept {
        return ct_.load(std::memory_order_acquire);
    }

    // ic 部分增加ic_incr, 保留ep 和rc 部分
    constexpr static rc_t inc_rc(rc_t rc) noexcept {
        return (rc & ic_mask) | ((rc + ic_incr) & ~ic_mask);
    }

    // 增加ic 部分，返回值保留ep和ic 部分，清除rc 部分（置为0）
    constexpr static rc_t inc_mask(rc_t rc) noexcept {
        return inc_rc(rc) & ~rc_mask;
    }

    /*
    * 发送模式为多对多广播时的具体类型（wr<1,1,1>）：
    * W: detail::queue_base<circ::elem_array<prod_cons_impl<wr<1,1,1>>,80,8>>
    * F: void <lambda>(void *)
    * E: prod_cons_impl<wr<1,1,1>>::elem_t<80,8>[] 数组
    */
    // 使用环形队列存储元素，每次将数据放入数组中时，都要先判断之前在本位置的元素是否被消费了。
    // 如果已经被消费了，就在该位置初始化数据。
    template <typename W, typename F, typename E>
    bool push(W* wrapper, F&& f, E* elems) {
        E* el;
        circ::u2_t cur_ct;
        rc_t epoch = epoch_.load(std::memory_order_acquire);
        for (unsigned k = 0;;) {
            // 连接的reader 的连接情况，每一位代表一个连接者
            circ::cc_t cc = wrapper->elems()->connections(std::memory_order_relaxed);
            if (cc == 0) return false; // no reader
            // index_of 通过截断达到唤醒队列的效果
            el = elems + circ::index_of(cur_ct = ct_.load(std::memory_order_relaxed));
            // cur_rc 实际由rc,ic,ep 三部分组成。rem_cc 表示rc 部分，也就是读取情况。
            // check all consumers have finished reading this element
            auto cur_rc = el->rc_.load(std::memory_order_relaxed);
            circ::cc_t rem_cc = cur_rc & rc_mask;
            if ((cc & rem_cc) && ((cur_rc & ~ep_mask) == epoch)) {
                return false; // has not finished yet
            }
            else if (!rem_cc) {
                // ?
                auto cur_fl = el->f_ct_.load(std::memory_order_acquire);
                if ((cur_fl != cur_ct) && cur_fl) {
                    return false; // full
                }
            }
            // 1. rc_ 更新: 分别需要更新三个部分的数据:
            //  1). 将新的epoch 放入
            //  2). 将ic 部分增加1
            //  3). 将connections 放入rc 部分（为1表示该消费者还未读取）
            // 2. epoch 判断是否改变，未改变时认为更新成功。改变了，就需要重新进行一次判断。
            // consider rem_cc to be 0 here
            if (el->rc_.compare_exchange_weak(
                        cur_rc, inc_mask(epoch | (cur_rc & ep_mask)) | static_cast<rc_t>(cc), std::memory_order_relaxed) &&
                epoch_.compare_exchange_weak(epoch, epoch, std::memory_order_acq_rel)) {
                break;
            }
            ipc::yield(k);
        }
        // only one thread/process would touch here at one time
        // 上边的rc_和epoch_ 的CAS保证了只有一个线程会走到当前代码位置
        // 增加commit index,确保下一个元素放入时能放到下一个位置
        ct_.store(cur_ct + 1, std::memory_order_release);
        // 将要发送的数据放入到data_ 中
        std::forward<F>(f)(&(el->data_));
        // set flag & try update wt
        el->f_ct_.store(~static_cast<flag_t>(cur_ct), std::memory_order_release);
        return true;
    }

    template <typename W, typename F, typename E>
    bool force_push(W* wrapper, F&& f, E* elems) {
        E* el;
        circ::u2_t cur_ct;
        rc_t epoch = epoch_.fetch_add(ep_incr, std::memory_order_release) + ep_incr;
        for (unsigned k = 0;;) {
            circ::cc_t cc = wrapper->elems()->connections(std::memory_order_relaxed);
            if (cc == 0) return false; // no reader
            el = elems + circ::index_of(cur_ct = ct_.load(std::memory_order_relaxed));
            // check all consumers have finished reading this element
            auto cur_rc = el->rc_.load(std::memory_order_acquire);
            circ::cc_t rem_cc = cur_rc & rc_mask;
            if (cc & rem_cc) {
                ipc::log("force_push: k = %u, cc = %u, rem_cc = %u\n", k, cc, rem_cc);
                cc = wrapper->elems()->disconnect_receiver(rem_cc); // disconnect all invalid readers
                if (cc == 0) return false; // no reader
            }
            // just compare & exchange
            if (el->rc_.compare_exchange_weak(
                        cur_rc, inc_mask(epoch | (cur_rc & ep_mask)) | static_cast<rc_t>(cc), std::memory_order_relaxed)) {
                if (epoch == epoch_.load(std::memory_order_acquire)) {
                    break;
                }
                else if (push(wrapper, std::forward<F>(f), elems)) {
                    return true;
                }
                epoch = epoch_.fetch_add(ep_incr, std::memory_order_release) + ep_incr;
            }
            ipc::yield(k);
        }
        // only one thread/process would touch here at one time
        ct_.store(cur_ct + 1, std::memory_order_release);
        std::forward<F>(f)(&(el->data_));
        // set flag & try update wt
        el->f_ct_.store(~static_cast<flag_t>(cur_ct), std::memory_order_release);
        return true;
    }

    template <typename W, typename F, typename R, typename E, std::size_t N>
    bool pop(W* wrapper, circ::u2_t& cur, F&& f, R&& out, E(& elems)[N]) {
        auto* el = elems + circ::index_of(cur);
        auto cur_fl = el->f_ct_.load(std::memory_order_acquire);
        if (cur_fl != ~static_cast<flag_t>(cur)) {
            return false; // empty
        }
        ++cur;
        std::forward<F>(f)(&(el->data_));
        for (unsigned k = 0;;) {
            auto cur_rc = el->rc_.load(std::memory_order_acquire);
            if ((cur_rc & rc_mask) == 0) {
                std::forward<R>(out)(true);
                el->f_ct_.store(cur + N - 1, std::memory_order_release);
                return true;
            }
            auto nxt_rc = inc_rc(cur_rc) & ~static_cast<rc_t>(wrapper->connected_id());
            bool last_one = false;
            if ((last_one = (nxt_rc & rc_mask) == 0)) {
                el->f_ct_.store(cur + N - 1, std::memory_order_release);
            }
            if (el->rc_.compare_exchange_weak(cur_rc, nxt_rc, std::memory_order_release)) {
                std::forward<R>(out)(last_one);
                return true;
            }
            ipc::yield(k);
        }
    }
};

} // namespace ipc
