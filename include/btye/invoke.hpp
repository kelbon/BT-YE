#pragma once

#include <vector>

#include <kelcoro/algorithm.hpp>
#include <kelcoro/task.hpp>

#include "btye/actor.hpp"

namespace btye {

template <typename F>
dd::task<std::invoke_result_t<F&>> as_task(F fn) {
  co_return fn();
}

// выполняет `t` на `a`, возвращается и пробрасывает исключение если есть
template <typename T>
dd::task<T> invoke_on(actor& a, dd::task<T> t) {
  actor& from = local_actor_ref();
  co_await jump_on(a);
  co_await t.wait();
  co_await jump_on(from);
  co_return t.get();
}

template <typename T>
dd::task<T> invoke_on(size_t id, dd::task<T> t) {
  return invoke_on(get_actor(id), std::move(t));
}

namespace noexport {

// ждёт все задачи, затем пробрасывает исключение первой по порядку
inline dd::task<void> wait_all(std::vector<dd::task<void>> tasks) {
  for (auto& r : co_await dd::when_all(std::move(tasks)))
    if (!r)
      std::rethrow_exception(r.error());
}

}  // namespace noexport

// fn() -> dd::task<void>, задачи выполняются на всех акторах параллельно
// ждёт все, затем пробрасывает исключение актора с меньшим номером
dd::task<void> invoke_on_all(auto fn) {
  std::vector<dd::task<void>> tasks;
  for (actor& a : all_actors())
    tasks.push_back(invoke_on(a, fn()));
  co_await noexport::wait_all(std::move(tasks));
}

// то же, на всех акторах кроме текущего
dd::task<void> invoke_on_others(auto fn) {
  std::vector<dd::task<void>> tasks;
  for (actor& a : all_actors())
    if (&a != local_actor())
      tasks.push_back(invoke_on(a, fn()));
  co_await noexport::wait_all(std::move(tasks));
}

}  // namespace btye
