#pragma once

#include <hidi/utils/any_io_context.hpp>
#include <hidi/utils/any_timer.hpp>

#include "btye/actor.hpp"

namespace btye {

// Ссылка на контекст текущего актора (не владеет им) для hidi: h2server, h2client, сгенерированные svarg
// серверы. Созданное на ней разрушается на этом же акторе до конца btye::run (например, через per_actor).
// Циклом управляет btye::run: run/stop у таких серверов и клиентов не вызывать.
// Перед разрушением: h2server - co_await shutdown(), h2client - co_await graceful_stop(). Их деструкторы
// внутри цикла не дожидаются своих корутин
hidi::any_io_context local_io_ref();

// таймер на контексте текущего актора, использовать и разрушать на нём же
hidi::any_timer create_timer();

}  // namespace btye
