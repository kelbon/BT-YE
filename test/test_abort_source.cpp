#include "btye/abort_source.hpp"
#include "btye/app.hpp"

#include <moko3/moko3.hpp>

#include <optional>
#include <vector>

TEST("abort_source: callbacks once, in order of subscription") {
  btye::abort_source as;
  std::vector<int> calls;
  btye::abort_source::subscription a = as.subscribe([&] noexcept { calls.push_back(1); });
  std::optional<btye::abort_source::subscription> b = as.subscribe([&] noexcept { calls.push_back(2); });
  btye::abort_source::subscription c = as.subscribe([&] noexcept { calls.push_back(3); });
  b.reset();  // отписалась
  REQUIRE(!as.aborted());
  as.request_abort();
  as.request_abort();
  REQUIRE(as.aborted() && (calls == std::vector<int>{1, 3}));
  // после отмены - сразу
  btye::abort_source::subscription d = as.subscribe([&] noexcept { calls.push_back(4); });
  REQUIRE((calls == std::vector<int>{1, 3, 4}));
}

TEST("abort_source: callback destroys another subscription") {
  btye::abort_source as;
  std::vector<int> calls;
  std::optional<btye::abort_source::subscription> second;
  btye::abort_source::subscription first = as.subscribe([&] noexcept {
    calls.push_back(1);
    second.reset();
  });
  second = as.subscribe([&] noexcept { calls.push_back(2); });
  as.request_abort();
  REQUIRE((calls == std::vector<int>{1}));
}

TEST("abort_source: moved subscription takes the place, moved-from is empty") {
  btye::abort_source as;
  std::vector<int> calls;
  btye::abort_source::subscription first = as.subscribe([&] noexcept { calls.push_back(1); });
  btye::abort_source::subscription second = as.subscribe([&] noexcept { calls.push_back(2); });
  btye::abort_source::subscription third = as.subscribe([&] noexcept { calls.push_back(3); });
  btye::abort_source::subscription moved = std::move(first);
  std::vector<btye::abort_source::subscription> stored;
  stored.push_back(std::move(third));
  stored.reserve(100);                          // перемещение элементов вектора
  second = btye::abort_source::subscription();  // отписалась
  first = std::move(moved);                     // обратно
  as.request_abort();
  REQUIRE((calls == std::vector<int>{1, 3}));
}

TEST("abort_source: destroyed before subscriptions") {
  std::optional<btye::abort_source> as;
  as.emplace();
  bool called = false;
  btye::abort_source::subscription s = as->subscribe([&] noexcept { called = true; });
  as.reset();
  REQUIRE(!called);
}

TEST("actor::stop_source is aborted at the start of the stop, before phases") {
  std::vector<int> order;
  int code = btye::run(btye::app_options{.actors_count = 2}, [&]() -> dd::task<int> {
    co_await btye::jump_on_actor(1);
    btye::run_in_background([](std::vector<int>& order) -> dd::task<void> {
      btye::actor& a = btye::local_actor_ref();
      btye::abort_source::subscription sub =
          a.stop_source().subscribe([&] noexcept { order.push_back(a.stopped() ? 1 : -1); });
      btye::stop_subscription phase = a.on_stop(btye::stop_phases_e::first, [&]() -> dd::task<void> {
        order.push_back(2);
        co_return;
      });
      co_await btye::wait_stopped();
    }(order));
    co_return 0;
  });
  REQUIRE(code == 0 && (order == std::vector<int>{1, 2}));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
