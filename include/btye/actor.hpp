#pragma once

#include <cassert>
#include <coroutine>
#include <span>
#include <vector>

#include <anyany/anyany.hpp>
#include <kelcoro/executor_interface.hpp>
#include <kelcoro/task.hpp>
#include <hidi/utils/any_io_context.hpp>

namespace btye {

namespace noexport {
struct app_state;
}

// Поток со своим циклом событий. Создаются и живут внутри btye::run
// TODO id потока, thread::id, actor-local переменные (безопасно вставляемые и удаляемые не ломая ссылки.
// Может быть разрешать вставлять с одного потока, так будет легче). Например для реализации sharded без
// тредлокалов, функции проверяют что находятся на правильном треде (которые ограничены тредом)
struct actor {
 private:
  hidi::any_io_context ctx;
  noexport::app_state* app = nullptr;
  // только на потоке актора
  std::vector<aa::move_only_fn<void()>> exit_callbacks;
  size_t idx = 0;
  bool is_stopped = false;

  friend struct noexport::app_state;
  friend std::span<actor> all_actors() noexcept;
  friend void run_in_background(dd::task<void>);

 public:
  actor() = default;
  actor(actor&&) = delete;
  void operator=(actor&&) = delete;

  [[nodiscard]] size_t id() const noexcept {
    return idx;
  }

  // Остановка дошла до этого актора (см. btye::request_stop)
  // можно вызывать только с актора
  [[nodiscard]] bool stopped() const noexcept {
    return is_stopped;
  }

  // вызовется до остановки, может создавать фоновые задачи - их дождутся
  void at_exit(aa::move_only_fn<void()>);

  // можно вызывать с любого потока. Всегда ставит задачу в очередь
  void attach(dd::task_node* n) {
    ctx.attach(n);
  }
};

// Актор текущего потока. nullptr - поток не актор
[[nodiscard]] actor* local_actor() noexcept;

// можно вызывать с любого потока, но вызывающий отвечает за синхронизацию и не обращение завершившимся
// акторам
[[nodiscard]] std::span<actor> all_actors() noexcept;

// актор с номером id, с любого потока (как all_actors)
[[nodiscard]] inline actor& get_actor(size_t id) noexcept {
  assert(id < all_actors().size());
  return all_actors()[id];
}

// Запускает задачу на текущем акторе (только на акторе) как фоновую
// Исключение из `task` игнорируется (лог)
// Нельзя вызывать из корутины, которую никто не ждёт
void run_in_background(dd::task<void> task);

namespace noexport {

template <bool HereReady>
struct jump_on_actor : dd::task_node {
  actor& a;

  explicit jump_on_actor(actor& a) noexcept : a(a) {
  }

  bool await_ready() const noexcept {
    if constexpr (HereReady)
      return local_actor() == &a;
    else
      return false;
  }
  void await_suspend(std::coroutine_handle<> h) {
    task = h;
    a.attach(this);
  }
  static void await_resume() noexcept {
  }
};

}  // namespace noexport

// noop если уже на этом акторе
// С любого потока
// jump_on нельзя делать корутине, которую никто не ждёт
[[nodiscard]] inline noexport::jump_on_actor<true> jump_on(actor& a) noexcept {
  return noexport::jump_on_actor<true>(a);
}

[[nodiscard]] inline noexport::jump_on_actor<true> jump_on_actor(size_t id) noexcept {
  return jump_on(get_actor(id));
}

// встать в конец очереди текущего актора
// можно вызывать только на акторе
[[nodiscard]] inline noexport::jump_on_actor<false> yield() noexcept {
  assert(local_actor() && "yield must be called on an actor thread");
  return noexport::jump_on_actor<false>(*local_actor());
}

}  // namespace btye
