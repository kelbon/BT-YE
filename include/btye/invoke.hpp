#pragma once

#include <coroutine>
#include <exception>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <vector>

#include <kelcoro/algorithm.hpp>
#include <kelcoro/task.hpp>
#include <metel/fixed_array.hpp>

#include "btye/actor.hpp"

namespace btye {

// Fn -> T = T
// Fn -> co_awaitable T = await result type
template <typename F, typename... Args>
using await_invoke_result_t = typename decltype([] {
  using R = std::invoke_result_t<F&, Args...>;
  if constexpr (dd::co_awaitable<R>) {
    return std::type_identity<dd::await_result_t<R>>{};
  } else {
    return std::type_identity<R>{};
  }
}())::type;

namespace noexport {

// вызов внутри корутины: исключение из синхронной части fn тоже попадает в задачу
// TODO либо таску, либо не корутину тупо вызывать и авейтер возвращает результат (мб это avalue из келкоро)
// TODO возвращать awaitable напрямую, если он awaitable
template <typename F, typename... Args>
dd::task<await_invoke_result_t<F, Args&&...>> call(F& fn, Args&&... args) {
  if constexpr (dd::co_awaitable<std::invoke_result_t<F&, Args&&...>>)
    co_return co_await fn(std::forward<Args>(args)...);
  else
    co_return fn(std::forward<Args>(args)...);
}

}  // namespace noexport

// fn() -> R или dd::task<R> вызывается на `a`. Результат или исключение - на текущем акторе
template <typename F>
dd::task<await_invoke_result_t<F>> invoke_on(actor& a, F fn) {
  actor& from = local_actor_ref();
  co_await jump_on(a);
  dd::task t = noexport::call(fn);
  co_await t.wait();
  co_await jump_on(from);
  co_return t.get();
}

template <typename F>
dd::task<await_invoke_result_t<F>> invoke_on(size_t id, F fn) {
  return invoke_on(get_actor(id), std::move(fn));
}

namespace noexport {

// назначает исключение для dd::task<void>, после этого сразу должен быть co_return
struct set_exception_awaiter {
  std::exception_ptr&& e;

  bool await_ready() const noexcept {
    assert(e);
    return false;
  }
  bool await_suspend(dd::task<void>::handle_type h) noexcept {
    h.promise().set_exception(std::move(e));
    return false;
  }
  static void await_resume() noexcept {
  }
};
#define BTYE_RETURN_EXCEPTION(e) co_return co_await ::btye::noexport::set_exception_awaiter(e)

// ждёт все задачи, затем пробрасывает исключение первой по порядку
inline dd::task<void> wait_all(std::vector<dd::task<void>> tasks) {
  for (auto& r : co_await dd::when_all(std::move(tasks)))
    if (!r)
      BTYE_RETURN_EXCEPTION(std::move(r.error()));
}

}  // namespace noexport

// fn() -> void или dd::task<void> на всех акторах параллельно, у каждого актора своя копия fn (см. invoke_on)
// ждёт все, затем пробрасывает исключение актора с меньшим номером
template <typename F>
dd::task<void> invoke_on_all(F fn) {
  static_assert(std::is_void_v<await_invoke_result_t<F>>);
  std::vector<dd::task<void>> tasks;
  for (actor& a : all_actors())
    tasks.push_back(invoke_on(a, fn));
  co_await noexport::wait_all(std::move(tasks));
}

// то же, на всех акторах кроме текущего
template <typename F>
dd::task<void> invoke_on_others(F fn) {
  static_assert(std::is_void_v<await_invoke_result_t<F>>);
  std::vector<dd::task<void>> tasks;
  for (actor& a : all_actors())
    if (&a != local_actor())
      tasks.push_back(invoke_on(a, fn));
  co_await noexport::wait_all(std::move(tasks));
}

namespace noexport {

template <typename R>
struct partial_result {
  std::optional<R> value;
  std::exception_ptr error;
};

template <typename R, typename Reduce>
dd::task<void> merge(partial_result<R>& left, partial_result<R> right, Reduce& reduce) {
  if (left.error)
    co_return;  // исключение левого важнее
  if (right.error) {
    left = std::move(right);
    co_return;
  }
  dbg_actor_ref x;
  try {
    left.value = co_await call(reduce, std::move(*left.value), std::move(*right.value));
  } catch (...) {
    x.check();  // разрушение должно происходить на том же акторе
    left.value.reset();
    left.error = std::current_exception();
  }
}

// используется только на акторе получателя
template <typename R, typename Map, typename Reduce>
struct mailbox {
  partial_result<R> got;
  bool filled = false;
  std::coroutine_handle<> receiver;
  // за компанию здесь приютились, чтобы не аллоцировать отдельно
  KELCORO_NO_UNIQUE_ADDRESS Map map;
  KELCORO_NO_UNIQUE_ADDRESS Reduce reduce;

  mailbox(const Map& m, const Reduce& r) : map(m), reduce(r) {
  }

  void put(partial_result<R> r) {
    got = std::move(r);
    filled = true;
    if (receiver)
      std::exchange(receiver, nullptr).resume();
  }

  bool await_ready() const noexcept {
    return filled;
  }
  void await_suspend(std::coroutine_handle<> h) noexcept {
    receiver = h;
  }
  partial_result<R> await_resume() noexcept {
    return std::move(got);
  }
};

// Свёртка деревом, пример на 8 акторах. Число - чьи результаты уже свёрнуты, столбец - на каком акторе лежат
//
//   актор:   0    1    2    3    4    5    6    7
//   старт:   0    1    2    3    4    5    6    7    map на каждом
//   шаг 1:   01        23        45        67        1->0, 3->2, 5->4, 7->6
//   шаг 2:   0123                4567                2->0, 6->4
//   шаг 4:   01234567                                4->0
//
// Путь задач (на каких акторах побывала):
//   0 - 0
//   1 - 1, 0
//   2 - 2, 0
//   3 - 3, 2
//   4 - 4, 0
//   5 - 5, 4
//   6 - 6, 4
//   7 - 7, 6
//
// Результат идёт к получателю, получатели стоят на месте. Задача i (кроме 0) переходит ровно один раз:
// на шаге s = младший бит i к актору i - s, кладёт результат в свой ящик mail[i] и завершается.
// Получатель читает ящики по порядку шагов: mail[i+1], mail[i+2], mail[i+4]...
// У каждого ящика один писатель и один читатель, встреча одна: пришёл раньше отправитель - значение ждёт
// в ящике, пришёл раньше получатель - ждёт он. Оба на акторе получателя, синхронизация не нужна.
// Порядок свёртки - по номерам акторов, поэтому reduce нужна только ассоциативность.
// Если акторов не степень двойки, у кого нет пары на шаге - пропускает его: при 5 акторах 4 ждёт до шага 4
// и идёт к 0
template <typename R, typename Map, typename Reduce>
dd::task<void> tree_node(size_t i, std::span<mailbox<R, Map, Reduce>> mail, partial_result<R>& result) {
  co_await jump_on_actor(i);
  partial_result<R> mine;
  try {
    mine.value.emplace(co_await call(mail[i].map));
  } catch (...) {
    mine.error = std::current_exception();
  }
  assert(local_actor()->id() == i);
  for (size_t step = 1; step < mail.size(); step *= 2) {
    if (i & step) {  // здесь эквивалентно if (i % (2 * step) != 0)
      co_await jump_on(get_actor(i - step));
      mail[i].put(std::move(mine));  // каждый записывает значение ровно один раз
      co_return;
    }
    if (i + step < mail.size()) {
      co_await merge(mine, co_await mail[i + step], mail[i].reduce);
      assert(local_actor()->id() == i);
    }
  }
  assert(i == 0);
  result = std::move(mine);
}

}  // namespace noexport

// map() -> R или dd::task<R>
// reduce(R, R) -> R или dd::task<R>
// Свёртки идут параллельно на разных акторах, последняя - на акторе 0, результат приходит вызывающему
// Для ассоциативной reduce результат = reduce(...reduce(reduce(r0, r1), r2)..., rN-1),
// коммутативность не нужна. У каждого актора свои копии map и reduce: создаются и разрушаются на текущем
// Ждёт все, затем пробрасывает исключение (если их несколько - с акторов с меньшими номерами)
// Note: результаты map могут быть разрушены не на тех акторах, на которых созданы
template <typename Map, typename Reduce>
dd::task<await_invoke_result_t<Map&>> map_reduce_on_all(Map map, Reduce reduce) {
  using R = await_invoke_result_t<Map&>;
  static_assert(!std::is_void_v<R>, "use invoke_on_all");
  static_assert(std::is_convertible_v<await_invoke_result_t<Reduce, R, R>, R>);
  actor& from = local_actor_ref();
  metel::fixed_array<noexport::mailbox<R, Map, Reduce>> mail(all_actors().size(), std::in_place, map, reduce);
  noexport::partial_result<R> result;
  // все узлы запускаются сразу, копии map и reduce создаются здесь
  // Узел текущего актора последним: он начинает map без перехода, остальные к этому моменту уже ушли
  std::vector<dd::task<void>> nodes;
  for (actor& a : all_actors())
    if (&a != &from)
      nodes.push_back(noexport::tree_node<R>(a.id(), std::span(mail), result));
  nodes.push_back(noexport::tree_node<R>(from.id(), std::span(mail), result));
  co_await noexport::wait_all(std::move(nodes));
  co_await jump_on(from);
  if (result.error)
    co_return dd::return_exception(std::move(result.error));
  co_return std::move(*result.value);
}

}  // namespace btye
