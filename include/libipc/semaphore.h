#pragma once

#include <cstdint>  // std::uint64_t

#include "libipc/export.h"
#include "libipc/def.h"

namespace ipc {
namespace sync {

// 信号量：通过资源计数控制进程同步，有PV操作
// P: 如果信号量计数大于0，减1，然后继续执行。否则会阻塞，直到大于零
// V: 信号量加1，如果有因为0阻塞的，就唤醒其中一个
class IPC_EXPORT semaphore {
    semaphore(semaphore const &) = delete;
    semaphore &operator=(semaphore const &) = delete;

public:
    semaphore();
    explicit semaphore(char const *name, std::uint32_t count = 0);
    ~semaphore();

    void const *native() const noexcept;
    void *native() noexcept;

    bool valid() const noexcept;

    bool open(char const *name, std::uint32_t count = 0) noexcept;
    void close() noexcept;

    void clear() noexcept;
    static void clear_storage(char const * name) noexcept;

    bool wait(std::uint64_t tm = ipc::invalid_value) noexcept;
    bool post(std::uint32_t count = 1) noexcept;

private:
    class semaphore_;
    semaphore_* p_;
};

} // namespace sync
} // namespace ipc
