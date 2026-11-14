#include "test_utils.hpp"

#include "btye/app.hpp"
#include "btye/invoke.hpp"
#include "btye/sleep.hpp"
#include "btye/trace.hpp"

#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;
using btye::traced_task;

static size_t count_of(std::string_view s, std::string_view what) {
  size_t n = 0;
  for (size_t pos = s.find(what); pos != s.npos; pos = s.find(what, pos + what.size()))
    ++n;
  return n;
}

static size_t actor_trace_size() {
  return btye::trace_ctx::leaves(btye::local_actor_ref()).size();
}

// ------------------------- вверх от задачи -------------------------

static traced_task<std::string> inner() {
  btye::trace_ctx& self = co_await dd::this_coro::context;
  co_await btye::yield();
  co_return btye::make_trace(self);
}

static traced_task<std::string> outer() {
  co_return co_await inner();
}

APP_TEST("trace: the stack of a task up to the root", .actors_count = 1) {
  std::string s = co_await outer();
  REQUIRE(s.contains("inner") && s.contains("outer") && s.find("inner") < s.find("outer"));
  // где стоит каждая задача
  REQUIRE(count_of(s, "     at ") >= 2 && s.contains("test_trace.cpp:"));
  REQUIRE(s.contains("  ^ root"));
  // btye::task с трассировкой или без, функции дампа компилируются для обеих
  std::string main_trace = btye::make_trace(co_await dd::this_coro::context);
  REQUIRE(BTYE_TRACE_TASKS ? main_trace.contains("task stack") : main_trace.contains("disabled"));
}

// ------------------------- листья актора -------------------------

static traced_task<void> child_sleep(btye::abort_source& as) {
  (void)co_await btye::sleep(1h, as);
}

static traced_task<void> fan_out(btye::abort_source& as) {
  (void)co_await dd::when_all(child_sleep(as), child_sleep(as));
}

// без прерываний: на контексте не asio их опрос - фоновая задача btye со своим листом на акторе 0
APP_TEST("trace: the actor keeps the deepest task of each chain", .actors_count = 1,
         .handle_interrupt = false) {
  btye::abort_source as;
  std::string seen;
  size_t leaves = 0;
  btye::run_in_background([](btye::abort_source& as, std::string& seen, size_t& leaves) -> btye::task<void> {
    co_await btye::yield();  // main успевает запустить детей, они ждут таймеры
    leaves = actor_trace_size();
    seen = btye::local_actor_trace();
    as.request_abort();
  }(as, seen, leaves));
  co_await fan_out(as);
  // два стека: ребёнок -> when_all -> fan_out -> main. Задача-наблюдатель - btye::task
  REQUIRE(leaves == 2 + BTYE_TRACE_TASKS);
  REQUIRE(count_of(seen, "child_sleep") == 2 && count_of(seen, "fan_out") == 2);
  REQUIRE(count_of(seen, "when_all") >= 2);
  if (BTYE_TRACE_TASKS)  // наблюдатель - фоновая задача: известно, откуда запущена
    REQUIRE(seen.contains("^ root, started at ") && seen.contains("test_trace.cpp:"));
}

static traced_task<void> noop_child() {
  co_return;
}

APP_TEST("trace: the owner returns to the actor on its next co_await", .actors_count = 1,
         .handle_interrupt = false) {
  size_t while_running = 99, after_co_await = 99;
  co_await [](size_t& while_running, size_t& after_co_await) -> traced_task<void> {
    co_await noop_child();
    // ребёнок закончился, эта задача выполняется и ещё не в списке
    while_running = actor_trace_size();
    co_await btye::yield();
    after_co_await = actor_trace_size();
  }(while_running, after_co_await);
  REQUIRE(while_running == 0 && after_co_await == 1);
}

static traced_task<std::pair<std::string, std::string>> traveler() {
  co_await btye::jump_on_actor(1);
  std::string on_1 = btye::local_actor_trace();
  co_await btye::jump_on_actor(0);
  std::string on_0 = btye::local_actor_trace();
  co_return std::pair{std::move(on_1), std::move(on_0)};
}

APP_TEST("trace: jump_on moves the task to another actor", .actors_count = 2) {
  auto [on_1, on_0] = co_await traveler();
  REQUIRE(on_1.contains("btye: actor 1") && on_1.contains("traveler") && on_1.contains("(actor 1)"));
  REQUIRE(on_0.contains("btye: actor 0") && on_0.contains("traveler") && on_0.contains("(actor 0)"));
}

static traced_task<int> blocking_job(std::atomic_bool& in_pool, std::atomic_bool& release) {
  co_return co_await btye::offload([&] {
    in_pool = true;
    while (!release)
      std::this_thread::yield();
    return 1;
  });
}

APP_TEST("trace: the task stays on its actor while in the offload pool", .actors_count = 1) {
  std::atomic_bool in_pool = false, release = false;
  std::string seen;
  btye::run_in_background(
      [](std::atomic_bool& in_pool, std::atomic_bool& release, std::string& seen) -> btye::task<void> {
        while (!in_pool)
          co_await btye::yield();
        seen = btye::local_actor_trace();
        release = true;
      }(in_pool, release, seen));
  REQUIRE(co_await blocking_job(in_pool, release) == 1);
  // лист - ждущая задача, стоит на co_await offload
  REQUIRE(seen.contains("blocking_job") && !seen.contains("offload_pool.hpp"));
}

APP_TEST("trace: a destroyed suspended task leaves the actor", .actors_count = 1) {
  size_t before = actor_trace_size();
  auto h = []() -> traced_task<void> { co_await dd::this_coro::suspend_and([](auto) {}); }().start_and_detach(
                    /*stop_at_end=*/true);
  REQUIRE(actor_trace_size() == before + 1);
  h.destroy();
  REQUIRE(actor_trace_size() == before);
  co_return;
}

static traced_task<void> sleeper(btye::abort_source& as) {
  (void)co_await btye::sleep(1h, as);
}

static traced_task<void> start_then_await(btye::abort_source& as, std::string& started_trace) {
  auto started = sleeper(as).start();
  // sleeper - корень со своим стеком, приостановлен
  started_trace = btye::local_actor_trace();
  co_await started;  // sleeper уже на акторе, эта задача уходит из списка
}

APP_TEST("trace: a started task awaited later", .actors_count = 1, .handle_interrupt = false) {
  btye::abort_source as;
  std::string started_trace, awaited_trace;
  size_t awaited_leaves = 0;
  btye::run_in_background([](btye::abort_source& as, std::string& seen, size_t& leaves) -> btye::task<void> {
    co_await btye::yield();
    leaves = actor_trace_size();
    seen = btye::local_actor_trace();
    as.request_abort();
  }(as, awaited_trace, awaited_leaves));
  co_await start_then_await(as, started_trace);
  REQUIRE(started_trace.contains("^ root, started at ") && started_trace.contains("test_trace.cpp:"));
  // один стек sleeper -> start_then_await и задача-наблюдатель - btye::task
  REQUIRE(awaited_leaves == 1 + BTYE_TRACE_TASKS);
  REQUIRE(count_of(awaited_trace, "sleeper") == 1 && count_of(awaited_trace, "start_then_await") == 1);
  REQUIRE(awaited_trace.find("sleeper") < awaited_trace.find("start_then_await"));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
