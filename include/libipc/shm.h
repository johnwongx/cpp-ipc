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

// ���������ڴ�
IPC_EXPORT id_t         acquire(char const * name, std::size_t size, unsigned mode = create | open);
// �������ڴ�ӳ�䵽������
IPC_EXPORT void *       get_mem(id_t id, std::size_t * size);
// �ͷ��ڴ�
IPC_EXPORT std::int32_t release(id_t id) noexcept;
IPC_EXPORT void         remove (id_t id) noexcept;
IPC_EXPORT void         remove (char const * name) noexcept;

IPC_EXPORT std::int32_t get_ref(id_t id);
IPC_EXPORT void sub_ref(id_t id);

// �����ڴ���
// ������һ�ֵ��͵ľ�����ʵ�֣���������������һ���������ڲ��ж���Դ��ֱ��ref
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

    // Windows ������������ɶҲ����
    std::int32_t ref() const noexcept;
    void sub_ref() noexcept;

    // ��ȡ�ڴ棬�Ѿ��������˽���
    bool acquire(char const * name, std::size_t size, unsigned mode = create | open);
    std::int32_t release();

    // Clean the handle file.
    void clear() noexcept;
    static void clear_storage(char const * name) noexcept;

    // �����ڴ�ָ��
    void* get() const;

    // ��Ԫ�ڴ���Ϣ��ӵ�������
    void attach(id_t);
    // ���ڴ���Ϣ�뱾������룬������ԭʼ��λ����Ϣ
    id_t detach();

private:
    class handle_;
    handle_* p_;
};

} // namespace shm
} // namespace ipc
