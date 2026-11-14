#pragma once

#include <kelcoro/task.hpp>

namespace btye {

// задача btye: везде в API btye и для кода пользователя
template <typename T>
using task = dd::task<T>;

}  // namespace btye
