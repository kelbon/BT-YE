#pragma once

#include <hidi/utils/deadline.hpp>

#include "btye/abort_source.hpp"
#include "btye/task.hpp"

namespace btye {

// Ждёт `d` на текущем акторе. true - время вышло, false - отменено: остановкой актора (actor::stop_source)
// или `as`. Если отменено до вызова, возвращает false сразу
task<bool> sleep(hidi::duration_t d);
task<bool> sleep(hidi::duration_t d, abort_source& as);

}  // namespace btye
