#pragma once

#include <cassert>
#include <coroutine>
#include <span>

#include <kelcoro/executor_interface.hpp>
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
  size_t idx = 0;
  bool is_stopped = false;

  friend struct noexport::app_state;
  friend std::span<actor> all_actors() noexcept;

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

  // Остановка дошла до этого актора (см. btye::request_stop). Только с потока актора.
  // Увидев остановку, корутина не должна планировать новые задачи и прыгать на другие акторы
  [[nodiscard]] bool stopped() const noexcept {
    return is_stopped;
  }

  // executor kelcoro, с любого потока. Всегда ставит задачу в очередь (см. btye::jump_on, btye::yield)
  void attach(dd::task_node* n) {
    // TODO is_stopped надо отменять задачу
    ctx.attach(n);
  }
};

// Актор текущего потока. nullptr - поток не актор
[[nodiscard]] actor* this_actor() noexcept;

// Все акторы. На потоке, который не актор, - пусто: чужому потоку их передаёт пользователь
[[nodiscard]] std::span<actor> all_actors() noexcept;

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
  [[nodiscard]] dd::schedule_status await_resume() const noexcept {
    return dd::schedule_status{status};
  }
};

}  // namespace noexport

// co_await jump_on(actor): продолжить на акторе. Уже на нём - продолжает сразу, без очереди.
// С любого потока. Перегрузка для ADL: неквалифицированный jump_on внутри kelcoro тоже выберет её
[[nodiscard]] inline noexport::jump_on_actor<true> jump_on(actor& a) noexcept {
  return noexport::jump_on_actor<true>(a);
}

// co_await yield(): уступить исполнение - встать в конец очереди текущего актора. Только на акторе
[[nodiscard]] inline noexport::jump_on_actor<false> yield() noexcept {
  assert(this_actor() && "yield must be called on an actor thread");
  return noexport::jump_on_actor<false>(*this_actor());
}

}  // namespace btye
