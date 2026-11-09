#include <kelcoro/task.hpp>
#include <kelcoro/thread_pool.hpp>

#include "btye/actor.hpp"

namespace btye {

struct offload_pool_options {
  // если 0, то ставится 1
  size_t threads_count = 10;  // TODO 0
};

struct offload_pool {
 private:
  dd::blocking_thread_pool tp;

 public:
  // деструктор дожидается исполняющихся задач, отменяет те кто напушили во время остановки
  explicit offload_pool(offload_pool_options);

  // можно вызывать только с актора
  // `home` - где проснётся делающий co_await offload
  template <typename F>
  dd::task<std::invoke_result_t<F>> offload(F fn, actor& dest = local_actor_ref()) {
    if (!co_await dd::jump_on(tp)) {
      // cancel вызывается на акторе где делается destroy
      co_await jump_on(dest);
      throw std::runtime_error("offload task was canceled due to app stop");
    }
    auto& p = (co_await dd::this_coro::handle).promise();
    try {
      p.return_place(fn());
    } catch (...) {
      p.set_exception(std::current_exception());
    }
    co_await jump_on(dest);
    co_return dd::rvo;
  }
};

}  // namespace btye
