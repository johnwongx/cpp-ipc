#pragma once

#include <cstdint>
#include <string>
#include <mutex>

#include <Windows.h>

#include "libipc/utility/log.h"
#include "libipc/utility/scope_guard.h"
#include "libipc/platform/detail.h"
#include "libipc/mutex.h"
#include "libipc/semaphore.h"
#include "libipc/shm.h"

namespace ipc {
namespace detail {
namespace sync {

// windows 下跨进程条件变量的实现
// 命名共享内存 + 互斥量 + 互斥锁
// 理解本类需要先了解条件变量的效果
class condition {
    ipc::sync::semaphore sem_; // 用于阻塞等待和唤醒
    ipc::sync::mutex lock_; // 用于保护shm_
    ipc::shm::handle shm_; // 等待者的个数

    // 返回共享内存的前4个字节数据的引用
    // 使用共享内存同步个数
    std::int32_t &counter() {
        return *static_cast<std::int32_t *>(shm_.get());
    }

public:
    condition() = default;
    ~condition() noexcept = default;

    auto native() noexcept {
        return sem_.native();
    }

    auto native() const noexcept {
        return sem_.native();
    }

    bool valid() const noexcept {
        return sem_.valid() && lock_.valid() && shm_.valid();
    }

    bool open(char const *name) noexcept {
        close();
        if (!sem_.open((std::string{name} + "_COND_SEM_").c_str())) {
            return false;
        }
        auto finally_sem = ipc::guard([this] { sem_.close(); }); // close when failed
        if (!lock_.open((std::string{name} + "_COND_LOCK_").c_str())) {
            return false;
        }
        auto finally_lock = ipc::guard([this] { lock_.close(); }); // close when failed
        if (!shm_.acquire((std::string{name} + "_COND_SHM_").c_str(), sizeof(std::int32_t))) {
            return false;
        }
        finally_lock.dismiss();
        finally_sem.dismiss();
        return valid();
    }

    void close() noexcept {
        if (!valid()) return;
        sem_.close();
        lock_.close();
        shm_.release();
    }

    void clear() noexcept {
        close();
    }

    static void clear_storage(char const *name) noexcept {
        ipc::shm::handle::clear_storage(name);
        ipc::sync::mutex::clear_storage(name);
        ipc::sync::semaphore::clear_storage(name);
    }

    bool wait(ipc::sync::mutex &mtx, std::uint64_t tm) noexcept {
        if (!valid()) return false;
        auto &cnt = counter();
        {
            IPC_UNUSED_ std::lock_guard<ipc::sync::mutex> guard {lock_};
            cnt = (cnt < 0) ? 1 : cnt + 1;
        }
        DWORD ms = (tm == invalid_value) ? INFINITE : static_cast<DWORD>(tm);
        /**
         * \see
         *  - https://www.microsoft.com/en-us/research/wp-content/uploads/2004/12/ImplementingCVs.pdf
         *  - https://docs.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-signalobjectandwait
        */
        // SignalObjectAndWait 用于原子地释放一个对象并等待另一个对象
        // 释放锁并对信号量进行P 操作，原子操作
        // 如果拆分为两步，可能会导致以下两种问题：
        // 1. 如果先释放了mtx，然后线程被切出去了，此时另一个进程发出了信号量，这个信号就会丢失
        // 2. 多个线程等待时，优先级可能会被其他线程抢占
        bool rs = ::SignalObjectAndWait(mtx.native(), sem_.native(), ms, FALSE) == WAIT_OBJECT_0;
        bool rl = mtx.lock(); // INFINITE
        if (!rs) {
            IPC_UNUSED_ std::lock_guard<ipc::sync::mutex> guard {lock_};
            cnt -= 1;
        }
        return rs && rl;
    }

    // 通知时没有使用锁，因为这样会导致等待者被唤醒后，立刻加锁又再次进入阻塞状态，降低效率
    // 在特殊情况下，比如等待者会关闭程序，这时如果产生虚假唤醒，导致信号量被释放，notify此时
    // 会操作无效信号量，进而引发未定义行为
    bool notify(ipc::sync::mutex &) noexcept {
        if (!valid()) return false;
        auto &cnt = counter(); // 这里只是获取内存的地址，并没有读取内容所以无需加锁
        if (!lock_.lock()) return false;
        bool ret = false;
		// 如果没有等待者，不需要增加信号量，否则会导致下次wait 时直接触发
        if (cnt > 0) {
            ret = sem_.post(1);
            cnt -= 1;
        }
        return lock_.unlock() && ret;
    }

    bool broadcast(ipc::sync::mutex &) noexcept {
        if (!valid()) return false;
        auto &cnt = counter();
        if (!lock_.lock()) return false;
        bool ret = false;
        if (cnt > 0) {
            ret = sem_.post(cnt);
            cnt = 0;
        }
        return lock_.unlock() && ret;
    }
};

} // namespace sync
} // namespace detail
} // namespace ipc
