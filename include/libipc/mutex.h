#pragma once

#include <cstdint>  // std::uint64_t
#include <system_error>

#include "libipc/export.h"
#include "libipc/def.h"

namespace ipc {
namespace sync {

// 进程间互斥锁
class IPC_EXPORT mutex {
    mutex(mutex const &) = delete;
    mutex &operator=(mutex const &) = delete;

public:
    mutex();
    explicit mutex(char const *name);
    ~mutex();

    void const *native() const noexcept;
    void *native() noexcept;

    bool valid() const noexcept;

    bool open(char const *name) noexcept;
    void close() noexcept;

    void clear() noexcept;
    // Windows 下啥也不做
    static void clear_storage(char const * name) noexcept;

    bool lock(std::uint64_t tm = ipc::invalid_value) noexcept;
    bool try_lock() noexcept(false); // std::system_error
    bool unlock() noexcept;

private:
    class mutex_;
    mutex_* p_;
};

} // namespace sync
} // namespace ipc
