#pragma once

#include <array>
#include <cassert>
#include <coroutine>
#include <span>
#include <vector>

#include <anyany/anyany.hpp>
#include <kelcoro/executor_interface.hpp>
#include <kelcoro/task.hpp>
#include <hidi/utils/any_io_context.hpp>

#include "btye/abort_source.hpp"
#include "btye/subscription.hpp"

namespace btye {

namespace noexport {
struct app_state;
}

// Остановка приложения идёт по фазам по порядку. Фаза проходит на всех акторах, и следующая начинается,
// когда закончились все обработчики предыдущей на всех акторах
enum struct stop_phases_e : size_t {
  first,
  servers,  // перестают принимать запросы и дожидаются текущих
  clients,  // после серверов: обработчики запросов ещё могли ими пользоваться
  last,
  COUNT,
};

// Подписка на фазу остановки, см. actor::on_stop. Отписывается при разрушении
using stop_subscription = noexport::subscription<aa::move_only_fn<dd::task<void>()>, noexport::app_state>;

// Поток со своим циклом событий. Создаются и живут внутри btye::run
// TODO actor local
// на каждом акторе unordered_map<ключ, aa::any>
struct actor {
 private:
  hidi::any_io_context ctx;
  noexport::app_state* app = nullptr;
  // только на потоке актора
  std::array<noexport::subscriptions_list<stop_subscription>, size_t(stop_phases_e::COUNT)> stop_handlers;
  abort_source stop_src;
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

  // `handler` вызывается на этом акторе в фазе `p`, если подписка жива, его задачу дожидаются. Исключение -
  // лог. Только на этом акторе, до остановки.
  // Задача обработчика может пережить подписку и её владельца: всё, что ей нужно, она держит сама
  [[nodiscard]] stop_subscription on_stop(stop_phases_e p, aa::move_only_fn<dd::task<void>()> handler);

  // отменяется в начале остановки, на всех акторах до фаз: после stopped() == true. Только на этом акторе
  [[nodiscard]] abort_source& stop_source() noexcept {
    return stop_src;
  }

  // можно вызывать с любого потока. Всегда ставит задачу в очередь
  void attach(dd::task_node* n) {
    ctx.attach(n);
  }

  // Контекст актора. Не трогать, если не понимаешь, зачем он нужен: для hidi есть local_io_ref(), для
  // таймеров - create_timer() (btye/io.hpp).
  // Циклом управляет btye::run (run, poll, stop, restart, start_task, end_task не вызывать), контекст
  // разрушается в конце run. Можно вызывать с любого потока, а что можно делать с контекстом с чужого
  // потока - определяет его реализация (attach и rebind_context можно)
  [[nodiscard]] hidi::any_io_context& ioctx() noexcept {
    return ctx;
  }
};

// Актор текущего потока. nullptr - поток не актор
[[nodiscard]] actor* local_actor() noexcept;

// актор текущего потока, assert что поток - актор
[[nodiscard]] inline actor& local_actor_ref() noexcept {
  assert(local_actor() && "must be called on an actor thread");
  return *local_actor();
}

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
  return noexport::jump_on_actor<false>(local_actor_ref());
}

}  // namespace btye
