#pragma once

#include <cstddef>
#include <string>

#include <kelcoro/generator.hpp>

#include "btye/task.hpp"

namespace btye {

// стек от `c` вверх
[[nodiscard]] dd::generator<const trace_ctx&> trace_up(const trace_ctx& c);

[[nodiscard]] std::string make_trace(const trace_ctx& c);
[[nodiscard]] inline std::string make_trace(const dd::null_context&) {
  return "<tracing disabled>";
}

// все стеки traced_task текущего актора
[[nodiscard]] std::string local_actor_trace();

}  // namespace btye

extern "C" {
// для вызова напрямую из отладки
void btye_print_actor(size_t id);
}
