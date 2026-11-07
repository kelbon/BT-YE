#include "btye/app.hpp"
#include "btye/io.hpp"
#include "btye/sleep.hpp"

#include <moko3/moko3.hpp>

#include <chrono>

using namespace std::chrono_literals;
using btye::local_actor;

static btye::app_options actors(size_t n) {
  return btye::app_options{.actors_count = n};
}

TEST("sleep: time is out") {
  int code = btye::run(actors(2), []() -> dd::task<int> {
    hidi::any_timer t = btye::create_timer();
    auto start = std::chrono::steady_clock::now();
    REQUIRE(co_await btye::sleep(t, 10ms));
    REQUIRE(std::chrono::steady_clock::now() - start >= 10ms);
    co_return 0;
  });
  REQUIRE(code == 0);
}

TEST("sleep: periodic background work woken by at_exit") {
  int ticks = 0;
  auto start = std::chrono::steady_clock::now();
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    btye::run_in_background([](int& ticks) -> dd::task<void> {
      hidi::any_timer t = btye::create_timer();
      local_actor()->at_exit([&t] { t.cancel(); });
      while (co_await btye::sleep(t, 1ms))
        ++ticks;
      // актор остановлен - сразу
      REQUIRE(!co_await btye::sleep(t, 1h));
    }(ticks));
    hidi::any_timer t = btye::create_timer();
    (void)co_await btye::sleep(t, 20ms);
    co_return 0;
  });
  REQUIRE(code == 0 && ticks > 0);
  // разбужен отменой, а не дождался часа
  REQUIRE(std::chrono::steady_clock::now() - start < 1s);
}

TEST("sleep: cancel while not sleeping is not lost") {
  auto start = std::chrono::steady_clock::now();
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    btye::run_in_background([]() -> dd::task<void> {
      hidi::any_timer t = btye::create_timer();
      bool canceled = false;
      local_actor()->at_exit([&] {
        t.cancel();  // таймер не взведён, отмена ничего не будит
        canceled = true;
      });
      while (!canceled)
        co_await btye::yield();
      REQUIRE(!co_await btye::sleep(t, 1h));
    }());
    co_return 0;
  });
  REQUIRE(code == 0);
  REQUIRE(std::chrono::steady_clock::now() - start < 1s);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
