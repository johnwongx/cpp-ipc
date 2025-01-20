#pragma once

#include <type_traits>

#include "libipc/def.h"
#include "libipc/prod_cons.h"

#include "libipc/circ/elem_array.h"

namespace ipc {
namespace policy {

// 本类实际上是一个包装类，现在只实现了生产者消费者模型的偏特化，使用本类之后可以方便得替换为别的模型
// <模版模版参数， Flag>
// 模版模版参数<类型，变参列表>
template <template <typename, std::size_t...> class Elems, typename Flag>
struct choose;

template <typename Flag>
struct choose<circ::elem_array, Flag> {
    using flag_t = Flag;

    // 嵌套的模版别名，依赖两个size
    template <std::size_t DataSize, std::size_t AlignSize>
    using elems_t = circ::elem_array<ipc::prod_cons_impl<flag_t>, DataSize, AlignSize>;
};

} // namespace policy
} // namespace ipc
