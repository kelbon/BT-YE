#pragma once

#include <exception>
#include <functional>
#include <memory>
#include <type_traits>

#include <metel/fixed_array.hpp>
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
// перед разрушением требуется вызвать co_await stop()
// используе adl per_actor_shutdown(T&) для остановки если есть
template <typename T>
struct per_actor {
 private:
  // каждый экземпляр в своей памяти, выделенной на его акторе
  metel::fixed_array<std::unique_ptr<T>> instances;

  T* local_ptr() noexcept {
    return instances.size() ? instances[local_actor_ref().id()].get() : nullptr;
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
    instances = metel::fixed_array<std::unique_ptr<T>>(all_actors().size());
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

  // вызывает adl перегрузку per_actor_shutdown(T&) -> dd::task<void>) если есть
  // исключение из per_actor_shutdown пробрасывается после разрушения
  dd::task<void> stop() {
    if (instances.size() == 0)
      co_return;
    static constexpr bool has_per_actor_shutdown = requires(T& x) { per_actor_shutdown(x); };
    dd::task<void> stopall;
    if constexpr (has_per_actor_shutdown) {
      stopall = btye::invoke_on_all([&]() -> dd::task<void> {
        T* t = local_ptr();
        if (t)  // при исключении в start незаполнено
          co_await per_actor_shutdown(*t);
      });
      co_await stopall.wait();
    }
    // в два прохода, так как инстансы могут держать ссылки друг на друга
    co_await btye::invoke_on_all([&]() -> dd::task<void> {
      instances[local_actor()->id()].reset();
      co_return;
    });
    instances = {};
    if constexpr (has_per_actor_shutdown)
      stopall.get();  // may be rethrow
  }

  // start начат и stop ещё не закончен
  [[nodiscard]] bool started() const noexcept {
    return instances.size() != 0;
  }

  // экземпляр текущего актора
  T& local() noexcept {
    T* t = local_ptr();
    assert(t && "per_actor is not started");
    return *t;
  }

  // fn(T&) -> R или dd::task<R> с экземпляром актора `id`, результат на текущем акторе (см. btye::invoke_on)
  template <typename F>
  auto invoke_on(size_t id, F fn) {
    return btye::invoke_on(id, [this, fn = std::move(fn)]() mutable { return fn(local()); });
  }

  // fn(T&) -> void или dd::task<void> на всех акторах параллельно, у каждого своя копия fn
  template <typename F>
  dd::task<void> invoke_on_all(F fn) {
    return btye::invoke_on_all([this, fn = std::move(fn)]() mutable { return fn(local()); });
  }

  // то же, кроме текущего актора
  template <typename F>
  dd::task<void> invoke_on_others(F fn) {
    return btye::invoke_on_others([this, fn = std::move(fn)]() mutable { return fn(local()); });
  }
};

}  // namespace btye
