#pragma once

#include <atomic>
#include <thread>
#include <chrono>
#include <limits>
#include <type_traits>
#include <utility>

////////////////////////////////////////////////////////////////
/// Gives hint to processor that improves performance of spin-wait loops.
////////////////////////////////////////////////////////////////

#pragma push_macro("IPC_LOCK_PAUSE_")
#undef  IPC_LOCK_PAUSE_

#if defined(_MSC_VER)
#include <windows.h>    // YieldProcessor
/*
    See: http://msdn.microsoft.com/en-us/library/windows/desktop/ms687419(v=vs.85).aspx
    Not for intel c++ compiler, so ignore http://software.intel.com/en-us/forums/topic/296168
*/
#   define IPC_LOCK_PAUSE_() YieldProcessor()
#elif defined(__GNUC__)
#if defined(__i386__) || defined(__x86_64__)
/*
    See: Intel(R) 64 and IA-32 Architectures Software Developer's Manual V2
         PAUSE-Spin Loop Hint, 4-57
         http://www.intel.com/content/www/us/en/architecture-and-technology/64-ia-32-architectures-software-developer-instruction-set-reference-manual-325383.html?wapkw=instruction+set+reference
*/
#   define IPC_LOCK_PAUSE_() __asm__ __volatile__("pause")
#elif defined(__ia64__) || defined(__ia64)
/*
    See: Intel(R) Itanium(R) Architecture Developer's Manual, Vol.3
         hint - Performance Hint, 3:145
         http://www.intel.com/content/www/us/en/processors/itanium/itanium-architecture-vol-3-manual.html
*/
#   define IPC_LOCK_PAUSE_() __asm__ __volatile__ ("hint @pause")
#elif defined(__arm__)
/*
    See: ARM Architecture Reference Manuals (YIELD)
         http://infocenter.arm.com/help/index.jsp?topic=/com.arm.doc.subset.architecture.reference/index.html
*/
#   define IPC_LOCK_PAUSE_() __asm__ __volatile__ ("yield")
#endif
#endif/*compilers*/

#if !defined(IPC_LOCK_PAUSE_)
/*
    Just use a compiler fence, prevent compiler from optimizing loop
*/
#   define IPC_LOCK_PAUSE_() std::atomic_signal_fence(std::memory_order_seq_cst)
#endif/*!defined(IPC_LOCK_PAUSE_)*/

////////////////////////////////////////////////////////////////
/// Yield to other threads
////////////////////////////////////////////////////////////////

namespace ipc {

/*
无锁操作中往往使用了while 循环，如果CAS失败之后立刻进入下一次循环，
因为其他线程正在修改立刻进入下次循环往往会导致失败。所以最好休息一下，
这就是yield 函数的作用。
连续多次调用yield ，每次增加k 值，根据不同的k 选择不同的休息策略。值
越大休息的时间越长。
< 4: 不需要休息，也就是不中断当前线程执行
< 16: 如果相同CPU Core 上有相同或较低优先级的线程在执行，则让出时间片
< 32: 在所有Core 上找优先级类似的线程，为这些线程让出时间片
>=32: 让出时间片。依据不同系统的时间精度不同，虽然指定的是 1 毫秒，但这个操作可能花费 10-15 毫秒。
*/
template <typename K>
inline void yield(K& k) noexcept {
    if (k < 4)  { /* Do nothing */ }
    else
    if (k < 16) { IPC_LOCK_PAUSE_(); }
    else
    if (k < 32) { std::this_thread::yield(); }
    else {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
        return;
    }
    ++k;
}

template <std::size_t N = 32, typename K, typename F>
inline void sleep(K& k, F&& f) {
    if (k < static_cast<K>(N)) {
        std::this_thread::yield();
    }
    else {
        static_cast<void>(std::forward<F>(f)());
        return;
    }
    ++k;
}

template <std::size_t N = 32, typename K>
inline void sleep(K& k) {
    sleep<N>(k, [] {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    });
}

} // namespace ipc

#pragma pop_macro("IPC_LOCK_PAUSE_")

namespace ipc {

// 自旋锁
// 如果锁持有时间较短，自旋锁的性能会比阻塞锁更高，因为它避免了线程阻塞和上下文切换的开销。
class spin_lock {
    std::atomic<unsigned> lc_ { 0 };

public:
    // memory_order_acquire: 确保后续的读写操作不会被重排到锁获取之前。
    // memory_order_release: 确保先前的读写操作不会被重排到锁释放之后.
    // 也就是保证读写在锁的持有期内
    void lock(void) noexcept {
        for (unsigned k = 0;
             lc_.exchange(1, std::memory_order_acquire);
             yield(k)) ;
    }

    void unlock(void) noexcept {
        lc_.store(0, std::memory_order_release);
    }
};

// 读写锁：使用原子类型结合yield 实现
class rw_lock {
    using lc_ui_t = unsigned;

    std::atomic<lc_ui_t> lc_ { 0 };

	// std::make_signed_t: 将给定的整数类型（如 unsigned int）转换为其对应的带符号类型（如 int）
    // (w_flag, 0): 读锁的mask
    // w_flag: 写锁
    enum : lc_ui_t {
        w_mask = (std::numeric_limits<std::make_signed_t<lc_ui_t>>::max)(), // b 0111 1111
        w_flag = w_mask + 1                                                 // b 1000 0000
    };

public:
    rw_lock() = default;

    rw_lock(const rw_lock&) = delete;
    rw_lock& operator=(const rw_lock&) = delete;
    rw_lock(rw_lock&&) = delete;
    rw_lock& operator=(rw_lock&&) = delete;

    void lock() noexcept {
        for (unsigned k = 0;;) {
            auto old = lc_.fetch_or(w_flag, std::memory_order_acq_rel);
            if (!old) return;           // got w-lock
            if (!(old & w_flag)) break; // other thread having r-lock
            yield(k);                   // other thread having w-lock
        }
        // wait for reading finished
        for (unsigned k = 0;
             lc_.load(std::memory_order_acquire) & w_mask;
             yield(k)) ;
    }

    void unlock() noexcept {
        lc_.store(0, std::memory_order_release);
    }

    // compare_exchange_weak: 如果不等于预期值就会更新预期值。会伪失败，效率比strong 高。
    void lock_shared() noexcept {
        auto old = lc_.load(std::memory_order_acquire);
        for (unsigned k = 0;;) {
            // if w_flag set, just continue
            if (old & w_flag) {
                yield(k);
                old = lc_.load(std::memory_order_acquire);
            }
            // otherwise try cas lc + 1 (set r-lock)
            else if (lc_.compare_exchange_weak(old, old + 1, std::memory_order_release)) {
                return;
            }
            // set r-lock failed, old has been updated
        }
    }

    void unlock_shared() noexcept {
        lc_.fetch_sub(1, std::memory_order_release);
    }
};

} // namespace ipc
