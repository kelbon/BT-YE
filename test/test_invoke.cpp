#include "test_utils.hpp"

#include "btye/invoke.hpp"

#include <array>
#include <atomic>
#include <functional>
#include <numeric>
#include <stdexcept>
#include <string>
#include <variant>
#include <vector>

using btye::local_actor;
using btye::test::actor_bound;
using btye::test::actors;

static btye::task<size_t> where() {
  co_return local_actor()->id();
}

APP_TEST("invoke_on: result, returns to the caller", .actors_count = 3) {
  co_await btye::jump_on(btye::get_actor(1));
  size_t on2 = co_await btye::invoke_on(2, where);
  REQUIRE(on2 == 2 && local_actor()->id() == 1);
  size_t on0 = co_await btye::invoke_on(btye::get_actor(0), []() -> btye::task<size_t> {
    co_await btye::yield();
    co_return local_actor()->id() + 10;
  });
  REQUIRE(on0 == 10 && local_actor()->id() == 1);
  // тот же актор
  REQUIRE(co_await btye::invoke_on(1, where) == 1);
}

APP_TEST("invoke_on: the function is called on the target actor, its copy per actor", .actors_count = 3) {
  // синхронная функция, её вызов - на целевом акторе
  REQUIRE(co_await btye::invoke_on(2, [] { return local_actor()->id(); }) == 2);
  // исключение из синхронной функции - на вызывающем
  bool caught_on_caller = false;
  try {
    (void)co_await btye::invoke_on(1, []() -> int { throw std::runtime_error("sync"); });
  } catch (std::runtime_error&) {
    caught_on_caller = local_actor()->id() == 0;
  }
  REQUIRE(caught_on_caller);
  // у каждого актора своя копия: изменения копии не видны другим
  std::vector<int> calls(3, 0);
  co_await btye::invoke_on_all([&calls, n = 0]() mutable { calls[local_actor()->id()] = ++n; });
  REQUIRE((calls == std::vector<int>{1, 1, 1}));
}

APP_TEST("invoke_on: exception after returning to the caller", .actors_count = 2) {
  bool caught_on_caller = false;
  try {
    co_await btye::invoke_on(1, []() -> btye::task<void> {
      throw std::runtime_error("on 1");
      co_return;
    });
  } catch (std::runtime_error& e) {
    caught_on_caller = local_actor()->id() == 0 && std::string_view(e.what()) == "on 1";
  }
  REQUIRE(caught_on_caller);
}

APP_TEST("invoke_on_all: parallel", .actors_count = 4) {
  constexpr size_t n = 4;
  std::atomic_size_t arrived = 0;
  std::vector<int> visited(n, 0);
  co_await btye::jump_on(btye::get_actor(2));
  // все ждут друг друга: при последовательном вызове первый не дождался бы остальных
  co_await btye::invoke_on_all([&]() -> btye::task<void> {
    ++arrived;
    while (arrived != n)
      co_await btye::yield();
    visited[local_actor()->id()] = 1;
  });
  REQUIRE(local_actor()->id() == 2);
  REQUIRE((visited == std::vector<int>(n, 1)));
}

APP_TEST("invoke_on_others and exceptions of invoke_on_all", .actors_count = 3) {
  std::vector<int> visited(3, 0);
  std::string error;
  co_await btye::jump_on(btye::get_actor(1));
  co_await btye::invoke_on_others([&]() -> btye::task<void> {
    visited[local_actor()->id()] = 1;
    co_return;
  });
  // все выполняются, пробрасывается исключение актора с меньшим номером
  try {
    co_await btye::invoke_on_all([&]() -> btye::task<void> {
      visited[local_actor()->id()] += 10;
      if (local_actor()->id() != 0)
        throw std::runtime_error(std::to_string(local_actor()->id()));
      co_return;
    });
  } catch (std::runtime_error& e) {
    error = e.what();
  }
  REQUIRE(local_actor()->id() == 1 && error == "1");
  REQUIRE((visited == std::vector<int>{11, 10, 11}));
}

// значение, которое нельзя разрушать на чужом акторе
struct traced {
  actor_bound bound;
  std::string s;
};

// конкатенация ассоциативна, но не коммутативна: результат зависит от порядка свёртки
TEST("map_reduce_on_all: same as std::accumulate, values and copies of map/reduce live on their actors") {
  for (size_t n = 1; n <= 9; ++n) {
    std::string expected, got;
    size_t caller = size_t(-1);
    std::vector<std::atomic_size_t> reduces_on(n);
    int violations_before = actor_bound::violations();
    int code = btye::run(actors(n), [&]() -> btye::task<int> {
      // вызов не с актора 0: результат должен вернуться сюда
      co_await btye::jump_on_actor(n - 1);
      auto map = [b = actor_bound{}] { return traced{{}, std::to_string(local_actor()->id())}; };
      auto concat = [&reduces_on, b = actor_bound{}](traced a, traced c) {
        ++reduces_on[local_actor()->id()];
        return traced{{}, a.s + c.s};
      };
      std::vector<size_t> ids(n);
      std::iota(ids.begin(), ids.end(), 0);
      expected = std::accumulate(ids.begin(), ids.end(), std::string(),
                                 [](std::string s, size_t id) { return s + std::to_string(id); });
      traced r = co_await btye::map_reduce_on_all(map, concat);
      got = r.s;
      caller = local_actor()->id();
      co_return 0;
    });
    REQUIRE(code == 0);
    REQUIRE(caller == n - 1);
    REQUIRE(got == expected);
    REQUIRE(actor_bound::violations() == violations_before);
    size_t reduces = 0;
    for (auto& r : reduces_on)
      reduces += r;
    REQUIRE(reduces == n - 1);
    if (n == 8) {
      // 1->0, 3->2, 5->4, 7->6, затем 2->0, 6->4, затем 4->0
      std::vector<size_t> counts(reduces_on.begin(), reduces_on.end());
      REQUIRE((counts == std::vector<size_t>{3, 0, 1, 0, 2, 0, 1, 0}));
    }
  }
}

APP_TEST("map_reduce_on_all: map and reduce returning task", .actors_count = 5) {
  co_await btye::jump_on_actor(2);
  size_t sum = co_await btye::map_reduce_on_all(
      []() -> btye::task<size_t> {
        co_await btye::yield();
        co_return local_actor()->id();
      },
      [](size_t a, size_t b) -> btye::task<size_t> {
        co_await btye::yield();
        co_return a + b;
      });
  REQUIRE(sum == 0 + 1 + 2 + 3 + 4);
  REQUIRE(local_actor()->id() == 2);
}

APP_TEST("map_reduce_on_all: exceptions after all actors are done", .actors_count = 4) {
  std::vector<int> visited(4, 0);
  auto throw_on_odd = [&]() -> size_t {
    size_t id = local_actor()->id();
    ++visited[id];
    if (id % 2)
      throw std::runtime_error(std::to_string(id));
    return id;
  };
  // исключения с 1 и 3: пробрасывается с меньшего номера
  std::string error;
  try {
    (void)co_await btye::map_reduce_on_all(throw_on_odd, std::plus<>{});
  } catch (std::runtime_error& e) {
    error = e.what();
  }
  REQUIRE(error == "1");
  REQUIRE((visited == std::vector<int>{1, 1, 1, 1}));

  // исключение из reduce на акторе 2 (сворачивает 2 и 3)
  error.clear();
  try {
    (void)co_await btye::map_reduce_on_all([] { return local_actor()->id(); },
                                           [](size_t a, size_t b) -> size_t {
                                             if (local_actor()->id() == 2)
                                               throw std::runtime_error("reduce");
                                             return a + b;
                                           });
  } catch (std::runtime_error& e) {
    error = e.what();
  }
  REQUIRE(error == "reduce");
  REQUIRE(local_actor()->id() == 0);
}

static btye::task<size_t> ends_on(size_t id, size_t value) {
  co_await btye::jump_on_actor(id);
  co_return value;
}

static btye::task<size_t> throws_on(size_t id) {
  co_await btye::jump_on_actor(id);
  throw std::runtime_error("throws_on");
}

APP_TEST("when_any: continues on the current actor, not on the winner's", .actors_count = 3) {
  auto r = co_await btye::when_any(ends_on(1, 10), ends_on(2, 20));
  REQUIRE(local_actor()->id() == 0);
  REQUIRE((r.index() == 0 ? *std::get<0>(r) : *std::get<1>(r)) == (r.index() == 0 ? 10 : 20));

  std::vector<btye::task<size_t>> tasks;
  tasks.push_back(ends_on(2, 30));
  tasks.push_back(ends_on(1, 40));
  auto d = co_await btye::when_any(std::move(tasks));
  REQUIRE(local_actor()->id() == 0);
  REQUIRE(*d.value == (d.index() == 0 ? 30 : 40));

  // все упали: исключение тоже здесь
  auto e = co_await btye::when_any(throws_on(1), throws_on(2));
  REQUIRE(local_actor()->id() == 0);
  REQUIRE(std::visit([](auto& x) { return !x; }, e));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
