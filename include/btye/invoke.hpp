#pragma once

#include <type_traits>
#include <vector>

#include <kelcoro/algorithm.hpp>
#include <kelcoro/task.hpp>

#include "btye/actor.hpp"

namespace btye {

// Fn -> T = T
// Fn -> co_awaitable T = await result type
template <typename F>
using await_invoke_result_t = typename decltype([] {
  using R = std::invoke_result_t<F&>;
  if constexpr (dd::co_awaitable<R>) {
    return std::type_identity<dd::await_result_t<R>>{};
  } else {
    return std::type_identity<R>{};
  }
}())::type;

namespace noexport {

// вызов внутри корутины: исключение из синхронной части fn тоже попадает в задачу
template <typename F>
dd::task<await_invoke_result_t<F>> call(F& fn) {
  if constexpr (dd::co_awaitable<std::invoke_result_t<F&>>)
    co_return co_await fn();
  else
    co_return fn();
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

// ждёт все задачи, затем пробрасывает исключение первой по порядку
inline dd::task<void> wait_all(std::vector<dd::task<void>> tasks) {
  for (auto& r : co_await dd::when_all(std::move(tasks)))
    if (!r)
      std::rethrow_exception(r.error());
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

}  // namespace btye
