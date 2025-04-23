#pragma once

#include <cstdint>  // std::uint64_t

#include "libipc/export.h"
#include "libipc/def.h"

namespace ipc {
namespace sync {

// �ź�����ͨ����Դ�������ƽ���ͬ������PV������
// P: ����ź�����������0����1��Ȼ�����ִ�С������������ֱ��������
// V(cnt): �ź�����cnt������������ȴ����߳̾ͻ���
// �ź��������������ִ�����Դ��������������̨��ӡ����������n���߳�Ҫʹ�ô�ӡ������ô��ӡ���ĳ�ʼ��Դ��
//     Ϊ3.��һ��������߳�ռ��һ����ӡ�����ͽ�������һ����Ϊ2.�ڶ�����Ҳ��һ�������ĸ�����ʱ������
//     ��ӡ������ԴΪ�㣬�������ȴ���ֱ��֮ǰ���߳��ͷ���һ����ӡ����
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
