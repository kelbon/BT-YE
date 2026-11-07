#pragma once

#include <hidi/utils/any_timer.hpp>
#include <hidi/utils/deadline.hpp>

#include <kelcoro/task.hpp>

namespace btye {

// Возвращает !local_actor.stopped()
dd::task<bool> sleep(hidi::any_timer& t, hidi::duration_t d);

}  // namespace btye
