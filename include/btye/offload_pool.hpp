#pragma once

#include <kelcoro/task.hpp>
#include <kelcoro/thread_pool.hpp>

#include "btye/actor.hpp"

namespace btye {

struct offload_pool_options {
  // если 0, то ставится 1
  size_t threads_count = 0;
  bool name_threads = false;
};

struct offload_pool {
 private:
  dd::blocking_thread_pool tp;

 public:
  // деструктор дожидается исполняющихся задач, отменяет те кто напушили во время остановки
  explicit offload_pool(offload_pool_options);

  // можно вызывать только с актора
  template <typename F>
  dd::task<std::invoke_result_t<F&>> offload(F fn) {
    actor& home = local_actor_ref();
    if (!home.offload_gate.try_enter())
      throw std::runtime_error("cannot offload when stopping");
    on_scope_exit {
      home.offload_gate.leave();
    };
    auto status = co_await dd::jump_on(tp);
    assert(status);
    (void)status;
    auto h = co_await dd::this_coro::handle;
    auto& p = h.promise();
    constexpr bool is_void = std::is_void_v<std::invoke_result_t<F&>>;
    try {
      if constexpr (!is_void)
        p.return_place(fn());
      else
        fn();
    } catch (...) {
      p.set_exception(std::current_exception());
    }
    co_await jump_on(home);
    if constexpr (!is_void)
      co_return dd::rvo;
  }
};

}  // namespace btye
