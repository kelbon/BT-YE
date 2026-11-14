#include "test_utils.hpp"

#include "btye/invoke.hpp"
#include "btye/sleep.hpp"

#include <chrono>

using namespace std::chrono_literals;
using btye::local_actor;

using btye::test::actors;

TEST("sleep: time is out") {
  int code = btye::run(actors(2), []() -> btye::task<int> {
    auto start = std::chrono::steady_clock::now();
    REQUIRE(co_await btye::sleep(10ms));
    REQUIRE(std::chrono::steady_clock::now() - start >= 10ms);
    co_return 0;
  });
  REQUIRE(code == 0);
}

TEST("sleep: woken by the stop") {
  // у каждого актора свои счётчик и флаг: задачи на разных акторах пишут без синхронизации
  std::array<int, 2> ticks{0, 0};
  std::array<bool, 2> after_stop{false, false};
  auto start = std::chrono::steady_clock::now();
  int code = btye::run(actors(2), [&]() -> btye::task<int> {
    for (size_t id : {0, 1}) {
      btye::run_in_background([](size_t id, int& ticks, bool& after_stop) -> btye::task<void> {
        co_await btye::jump_on_actor(id);
        while (co_await btye::sleep(id == 0 ? 1ms : 1h))
          ++ticks;
        // актор остановлен - сразу
        after_stop = !co_await btye::sleep(1h);
      }(id, ticks[id], after_stop[id]));
    }
    (void)co_await btye::sleep(20ms);
    co_return 0;
  });
  REQUIRE(code == 0 && ticks[0] > 0 && ticks[1] == 0 && after_stop[0] && after_stop[1]);
  // разбужены остановкой, а не дождались часа
  REQUIRE(std::chrono::steady_clock::now() - start < 1s);
}

TEST("sleep: woken by abort_source, abort while not sleeping is not lost") {
  bool woken = false, at_once = false;
  int code = btye::run(actors(2), [&]() -> btye::task<int> {
    btye::abort_source as;
    btye::run_in_background([](btye::abort_source& as) -> btye::task<void> {
      co_await btye::yield();
      as.request_abort();
    }(as));
    woken = !co_await btye::sleep(1h, as);
    auto start = std::chrono::steady_clock::now();
    at_once = !co_await btye::sleep(1h, as) && std::chrono::steady_clock::now() - start < 1s;
    // другой источник не мешает
    btye::abort_source other;
    REQUIRE(co_await btye::sleep(1ms, other));
    co_return 0;
  });
  REQUIRE(code == 0 && woken && at_once);
}

TEST("sleep: abort from another actor through invoke_on") {
  bool woken = false;
  int code = btye::run(actors(2), [&]() -> btye::task<int> {
    btye::abort_source as;  // на акторе 0
    btye::run_in_background([](btye::abort_source& as) -> btye::task<void> {
      co_await btye::jump_on_actor(1);
      co_await btye::invoke_on(0, [&] { as.request_abort(); });
    }(as));
    woken = !co_await btye::sleep(1h, as);
    co_return 0;
  });
  REQUIRE(code == 0 && woken);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
