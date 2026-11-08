#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>

#include <kelcoro/noexport/fixed_array.hpp>
#include <kelcoro/task.hpp>

#include "btye/invoke.hpp"

namespace btye {

template <typename T>
struct per_actor;

// база T для per_actor<T>, даёт экземпляру его per_actor (например, чтобы обратиться к соседям)
template <typename T>
struct per_actor_peer {
 private:
  per_actor<T>* c = nullptr;

  friend struct per_actor<T>;

 public:
  // доступен после конструктора T
  per_actor<T>& container() const noexcept {
    assert(c);
    return *c;
  }
};

// аргумент per_actor::start, fn() вызывается на каждом акторе
template <typename F>
struct per_actor_param {
  F fn;
};

namespace noexport {

// std::ref(per_actor<U>) в аргументах start: преобразуется и в per_actor<U>&, и в U& текущего актора
template <typename U>
struct per_actor_or_local {
  per_actor<U>& pa;

  operator per_actor<U>&() const noexcept {
    return pa;
  }
  operator U&() const noexcept {
    return pa.local();
  }
};

template <typename A>
A& unwrap_arg(A& a) noexcept {
  return a;
}
template <typename U>
per_actor_or_local<U> unwrap_arg(std::reference_wrapper<per_actor<U>>& r) noexcept {
  return {r.get()};
}
template <typename F>
auto unwrap_arg(per_actor_param<F>& p) {
  return p.fn();
}

}  // namespace noexport

// экземпляр T на каждом акторе, создаётся и разрушается на своём акторе
// только с актора. Перед разрушением - co_await stop()
template <typename T>
struct per_actor {
 private:
  // каждый экземпляр в своей памяти, выделенной на его акторе
  dd::noexport::fixed_array<std::unique_ptr<T>> instances;

  T* local_ptr() noexcept {
    return instances.size() ? instances[local_actor_ref().id()].get() : nullptr;
  }

  // создаётся на том акторе, где задача стартует
  template <typename F>
  std::invoke_result_t<F&, T&> call_local(F& fn) {
    co_return co_await fn(local());
  }

 public:
  per_actor() = default;
  per_actor(per_actor&&) = delete;
  void operator=(per_actor&&) = delete;

  ~per_actor() {
    assert(instances.size() == 0 && "per_actor must be stopped");
  }

  // T(args...) на всех акторах параллельно. Аргументы общие, на каждом акторе передаются как lvalue
  // std::ref(per_actor<U>) - как per_actor<U>& или U& текущего актора, per_actor_param - результат fn()
  // при исключении созданные экземпляры останавливаются (.stop), исключение пробрасывается
  template <typename... Args>
  dd::task<void> start(Args... args) {
    assert(instances.size() == 0 && "per_actor is already started");
    instances = dd::noexport::fixed_array<std::unique_ptr<T>>(all_actors().size());
    dd::task<void> t = btye::invoke_on_all([&]() -> dd::task<void> {
      auto& t = instances[local_actor()->id()];
      t = std::make_unique<T>(noexport::unwrap_arg(args)...);
      if constexpr (std::is_base_of_v<per_actor_peer<T>, T>)
        t->c = this;
      co_return;
    });
    co_await t.wait();
    auto& promise = t.raw_handle().promise();
    if (promise.has_exception()) {
      co_await stop();
      std::rethrow_exception(promise.take_exception());
    }
  }

  // T::stop() (если есть, void или dd::task<void>) на всех акторах параллельно, затем экземпляры
  // разрушаются, каждый на своём акторе. Исключение из T::stop() пробрасывается после разрушения
  dd::task<void> stop() {
    if (instances.size() == 0)
      co_return;
    dd::task<void> stopall = btye::invoke_on_all([&]() -> dd::task<void> {
      T* t = local_ptr();
      if constexpr (requires { t->stop(); }) {
        if (!t)
          co_return;
        if constexpr (std::is_void_v<decltype(t->stop())>)
          t->stop();
        else
          co_await t->stop();
      }
      co_return;
    });
    co_await stopall.wait();
    co_await btye::invoke_on_all([&]() -> dd::task<void> {
      instances[local_actor()->id()].reset();
      co_return;
    });
    instances = {};
    stopall.get();  // may be rethrow
  }

  // экземпляр текущего актора
  T& local() noexcept {
    T* t = local_ptr();
    assert(t && "per_actor is not started");
    return *t;
  }

  // fn(T&) -> dd::task<R> с экземпляром актора `id`, результат на текущем акторе
  template <typename F>
  std::invoke_result_t<F&, T&> invoke_on(size_t id, F fn) {
    co_return co_await btye::invoke_on(id, call_local(fn));
  }

  // fn(T&) -> dd::task<void> на всех акторах параллельно
  template <typename F>
  dd::task<void> invoke_on_all(F fn) {
    co_await btye::invoke_on_all([&] { return call_local(fn); });
  }

  // то же, кроме текущего актора
  template <typename F>
  dd::task<void> invoke_on_others(F fn) {
    co_await btye::invoke_on_others([&] { return call_local(fn); });
  }
};

}  // namespace btye
