#pragma once

#include <cstddef>
#include <cstdint>

#include "libipc/export.h"

namespace ipc {
namespace shm {

using id_t = void*; // id_info_t

enum : unsigned {
    create = 0x01,
    open   = 0x02
};

// 申请物理内存
IPC_EXPORT id_t         acquire(char const * name, std::size_t size, unsigned mode = create | open);
// 将物理内存映射到进程中
IPC_EXPORT void *       get_mem(id_t id, std::size_t * size);
// 释放内存
IPC_EXPORT std::int32_t release(id_t id) noexcept;
IPC_EXPORT void         remove (id_t id) noexcept;
IPC_EXPORT void         remove (char const * name) noexcept;

IPC_EXPORT std::int32_t get_ref(id_t id);
IPC_EXPORT void sub_ref(id_t id);

// 共享内存句柄
// 本类是一种典型的句柄类的实现，用起来像是索引一样，但是内部有对资源的直接ref
class IPC_EXPORT handle {
public:
    handle();
    handle(char const * name, std::size_t size, unsigned mode = create | open);
    handle(handle&& rhs);

    ~handle();

    void swap(handle& rhs);
    handle& operator=(handle rhs);

    bool         valid() const noexcept;
    std::size_t  size () const noexcept;
    char const * name () const noexcept;

    // Windows 下这两个函数啥也不做
    std::int32_t ref() const noexcept;
    void sub_ref() noexcept;

    // 获取内存，已经关联到了进程
    bool acquire(char const * name, std::size_t size, unsigned mode = create | open);
    std::int32_t release();

    // Clean the handle file.
    void clear() noexcept;
    static void clear_storage(char const * name) noexcept;

    // 返回内存指针
    void* get() const;

    // 将元内存信息添加到本类中
    void attach(id_t);
    // 将内存信息与本对象分离，并返回原始的位置信息
    id_t detach();

private:
    class handle_;
    handle_* p_;
};

} // namespace shm
} // namespace ipc
