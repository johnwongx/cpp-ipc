#pragma once

#include <type_traits>

#include "libipc/def.h"
#include "libipc/prod_cons.h"

#include "libipc/circ/elem_array.h"

namespace ipc {
namespace policy {

// ����ʵ������һ����װ�࣬����ֻʵ����������������ģ�͵�ƫ�ػ���ʹ�ñ���֮����Է�����滻Ϊ���ģ��
// <ģ��ģ������� Flag>
// ģ��ģ�����<���ͣ�����б�>
template <template <typename, std::size_t...> class Elems, typename Flag>
struct choose;

template <typename Flag>
struct choose<circ::elem_array, Flag> {
    using flag_t = Flag;

    // Ƕ�׵�ģ���������������size
    template <std::size_t DataSize, std::size_t AlignSize>
    using elems_t = circ::elem_array<ipc::prod_cons_impl<flag_t>, DataSize, AlignSize>;
};

} // namespace policy
} // namespace ipc
