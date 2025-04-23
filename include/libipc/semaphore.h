#pragma once

#include <cstdint>  // std::uint64_t

#include "libipc/export.h"
#include "libipc/def.h"

namespace ipc {
namespace sync {

// 信号量：通过资源计数控制进程同步，有PV操作。
// P: 如果信号量计数大于0，减1，然后继续执行。否则会阻塞，直到大于零
// V(cnt): 信号量加cnt，如果有阻塞等待的线程就唤醒
// 信号量本质是用数字代表资源数量。比如有三台打印机，现在有n个线程要使用打印机。那么打印机的初始资源数
//     为3.第一个进入的线程占用一个打印机，就将数量减一，变为2.第二三个也是一样。第四个进入时，发现
//     打印机的资源为零，就阻塞等待，直到之前的线程释放了一个打印机。
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
