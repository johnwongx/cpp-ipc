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

// windows �¿��������������ʵ��
// ���������ڴ� + ������ + ������
// ��Ȿ����Ҫ���˽�����������Ч��
class condition {
    ipc::sync::semaphore sem_; // ���������ȴ��ͻ���
    ipc::sync::mutex lock_; // ���ڱ���shm_
    ipc::shm::handle shm_; // �ȴ��ߵĸ���

    // ���ع����ڴ��ǰ4���ֽ����ݵ�����
    // ʹ�ù����ڴ�ͬ������
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
        // SignalObjectAndWait ����ԭ�ӵ��ͷ�һ�����󲢵ȴ���һ������
        // �ͷ��������ź�������P ������ԭ�Ӳ���
        // ������Ϊ���������ܻᵼ�������������⣺
        // 1. ������ͷ���mtx��Ȼ���̱߳��г�ȥ�ˣ���ʱ��һ�����̷������ź���������źžͻᶪʧ
        // 2. ����̵߳ȴ�ʱ�����ȼ����ܻᱻ�����߳���ռ
        bool rs = ::SignalObjectAndWait(mtx.native(), sem_.native(), ms, FALSE) == WAIT_OBJECT_0;
        bool rl = mtx.lock(); // INFINITE
        if (!rs) {
            IPC_UNUSED_ std::lock_guard<ipc::sync::mutex> guard {lock_};
            cnt -= 1;
        }
        return rs && rl;
    }

    // ֪ͨʱû��ʹ��������Ϊ�����ᵼ�µȴ��߱����Ѻ����̼������ٴν�������״̬������Ч��
    // ����������£�����ȴ��߻�رճ�����ʱ���������ٻ��ѣ������ź������ͷţ�notify��ʱ
    // �������Ч�ź�������������δ������Ϊ
    bool notify(ipc::sync::mutex &) noexcept {
        if (!valid()) return false;
        auto &cnt = counter(); // ����ֻ�ǻ�ȡ�ڴ�ĵ�ַ����û�ж�ȡ���������������
        if (!lock_.lock()) return false;
        bool ret = false;
		// ���û�еȴ��ߣ�����Ҫ�����ź���������ᵼ���´�wait ʱֱ�Ӵ���
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
