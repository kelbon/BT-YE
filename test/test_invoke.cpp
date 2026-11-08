#include "test_utils.hpp"

#include "btye/invoke.hpp"

#include <atomic>
#include <stdexcept>
#include <string>
#include <vector>

using btye::local_actor;

static dd::task<size_t> where() {
  co_return local_actor()->id();
}

APP_TEST("invoke_on: result, returns to the caller", .actors_count = 3) {
  co_await btye::jump_on(btye::get_actor(1));
  size_t on2 = co_await btye::invoke_on(2, where());
  REQUIRE(on2 == 2 && local_actor()->id() == 1);
  size_t on0 = co_await btye::invoke_on(btye::get_actor(0), []() -> dd::task<size_t> {
    co_await btye::yield();
    co_return local_actor()->id() + 10;
  }());
  REQUIRE(on0 == 10 && local_actor()->id() == 1);
  // тот же актор
  REQUIRE(co_await btye::invoke_on(1, where()) == 1);
}

APP_TEST("invoke_on: exception after returning to the caller", .actors_count = 2) {
  bool caught_on_caller = false;
  try {
    co_await btye::invoke_on(1, []() -> dd::task<void> {
      throw std::runtime_error("on 1");
      co_return;
    }());
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
  co_await btye::invoke_on_all([&]() -> dd::task<void> {
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
  co_await btye::invoke_on_others([&]() -> dd::task<void> {
    visited[local_actor()->id()] = 1;
    co_return;
  });
  // все выполняются, пробрасывается исключение актора с меньшим номером
  try {
    co_await btye::invoke_on_all([&]() -> dd::task<void> {
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

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
