#pragma once

#include <cstdint>
#include <system_error>

#include <Windows.h>

#include "libipc/utility/log.h"

#include "to_tchar.h"
#include "get_sa.h"

namespace ipc {
namespace detail {
namespace sync {

// Windows 下的锁
class mutex {
    HANDLE h_ = NULL;

public:
    mutex() noexcept = default;
    ~mutex() noexcept = default;

    static void init() {}

    HANDLE native() const noexcept {
        return h_;
    }

    bool valid() const noexcept {
        return h_ != NULL;
    }

    bool open(char const *name) noexcept {
        close();
        h_ = ::CreateMutex(detail::get_sa(), FALSE, detail::to_tchar(name).c_str());
        if (h_ == NULL) {
            ipc::error("fail CreateMutex[%lu]: %s\n", ::GetLastError(), name);
            return false;
        }
        return true;
    }

    void close() noexcept {
        if (!valid()) return;
        ::CloseHandle(h_);
        h_ = NULL;
    }

    void clear() noexcept {
        close();
    }

    static void clear_storage(char const */*name*/) noexcept {
    }

    bool lock(std::uint64_t tm) noexcept {
        DWORD ret, ms = (tm == invalid_value) ? INFINITE : static_cast<DWORD>(tm);
        for(;;) {
            switch ((ret = ::WaitForSingleObject(h_, ms))) {
            case WAIT_OBJECT_0:
                return true;
            case WAIT_TIMEOUT:
                return false;
            case WAIT_ABANDONED:
                // 互斥量被另一个线程放弃（比如持有锁的线程终止），这时尝试解锁
                ipc::log("fail WaitForSingleObject[%lu]: WAIT_ABANDONED, try again.\n", ::GetLastError());
                if (!unlock()) {
                    return false;
                }
                break; // loop again
            default:
                ipc::error("fail WaitForSingleObject[%lu]: 0x%08X\n", ::GetLastError(), ret);
                return false;
            }
        }
    }

    bool try_lock() noexcept(false) {
        DWORD ret = ::WaitForSingleObject(h_, 0);
        switch (ret) {
        case WAIT_OBJECT_0:
            return true;
        case WAIT_TIMEOUT:
            return false;
        case WAIT_ABANDONED:
            unlock();
            IPC_FALLTHROUGH_;
        default:
            ipc::error("fail WaitForSingleObject[%lu]: 0x%08X\n", ::GetLastError(), ret);
            throw std::system_error{static_cast<int>(ret), std::system_category()};
        }
    }

    bool unlock() noexcept {
        if (!::ReleaseMutex(h_)) {
            ipc::error("fail ReleaseMutex[%lu]\n", ::GetLastError());
            return false;
        }
        return true;
    }
};

} // namespace sync
} // namespace detail
} // namespace ipc
