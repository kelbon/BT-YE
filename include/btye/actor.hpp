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

  // 0..N-1, актор 0 - поток, вызвавший btye::run
  [[nodiscard]] size_t id() const noexcept {
    return idx;
  }

  // создан фабрикой на потоке актора и там же разрушается
  [[nodiscard]] hidi::any_io_context& ioctx() noexcept {
    return ctx;
  }

  // Остановка дошла до этого актора (см. btye::request_stop). Только с потока актора
  [[nodiscard]] bool stopped() const noexcept {
    return is_stopped;
  }

  // вызовется до остановки, может создавать фоновые задачи - их дождутся
  void at_exit(aa::move_only_fn<void()>);

  // executor kelcoro, с любого потока. Всегда ставит задачу в очередь (см. btye::jump_on, btye::yield).
  void attach(dd::task_node* n) {
    ctx.attach(n);
  }
};

// Актор текущего потока. nullptr - поток не актор
[[nodiscard]] actor* this_actor() noexcept;

// можно вызывать с любого потока, но вызывающий отвечает за синхронизацию и не обращение завершившимся
// акторам
[[nodiscard]] std::span<actor> all_actors() noexcept;

// актор с номером id, с любого потока (как all_actors)
[[nodiscard]] inline actor& get_actor(size_t id) noexcept {
  assert(id < all_actors().size());
  return all_actors()[id];
}

// Запускает задачу на текущем акторе (только на акторе) как фоновую: пока идёт хоть одна фоновая задача,
// циклы всех акторов живы. Исключение из задачи - лог.
// Если stopped() у своего актора - только из фоновой задачи или at_exit
void run_in_background(dd::task<void>);

namespace noexport {

// HereReady - уже на акторе: продолжить сразу, без очереди
template <bool HereReady>
struct jump_on_actor : dd::task_node {
  actor& a;

  explicit jump_on_actor(actor& a) noexcept : a(a) {
  }

  bool await_ready() const noexcept {
    if constexpr (HereReady)
      return this_actor() == &a;
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

// co_await jump_on(actor): продолжить на акторе. Уже на нём - продолжает сразу, без очереди.
// С любого потока. Перегрузка для ADL: неквалифицированный jump_on внутри kelcoro тоже выберет её.
// Если stopped() у своего актора, на другой актор - только из фоновой задачи или at_exit: иначе цикл
// цели мог уже закончиться и задача не выполнится
[[nodiscard]] inline noexport::jump_on_actor<true> jump_on(actor& a) noexcept {
  return noexport::jump_on_actor<true>(a);
}

// co_await jump_on(id): то же для актора с номером id
[[nodiscard]] inline noexport::jump_on_actor<true> jump_on_actor(size_t id) noexcept {
  return jump_on(get_actor(id));
}

// co_await yield(): уступить исполнение - встать в конец очереди текущего актора. Только на акторе
[[nodiscard]] inline noexport::jump_on_actor<false> yield() noexcept {
  assert(this_actor() && "yield must be called on an actor thread");
  return noexport::jump_on_actor<false>(*this_actor());
}

}  // namespace btye
