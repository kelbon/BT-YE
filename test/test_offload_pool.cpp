#include "test_utils.hpp"

#include "btye/invoke.hpp"
#include "btye/offload_pool.hpp"
#include "btye/sleep.hpp"

#include <atomic>
#include <chrono>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
  #include <pthread.h>
#endif

using namespace std::chrono_literals;
using btye::local_actor;
using btye::test::actors;

static bool throws(auto f) {
  try {
    f();
  } catch (std::exception&) {
    return true;
  }
  return false;
}

static btye::offload_pool& pool() {
  return btye::local_actor_ref().offpool();
}

APP_TEST("offload: fn runs not on an actor, the result returns to the caller actor", .actors_count = 2) {
  co_await btye::jump_on(btye::get_actor(1));
  std::thread::id caller = std::this_thread::get_id();
  auto [on_actor, id] =
      co_await pool().offload([] { return std::pair(local_actor() != nullptr, std::this_thread::get_id()); });
  REQUIRE(!on_actor);
  REQUIRE(id != caller);
  REQUIRE(local_actor()->id() == 1);
}

APP_TEST("offload: exception from fn is rethrown on the caller actor", .actors_count = 2) {
  co_await btye::jump_on(btye::get_actor(1));
  std::string error;
  try {
    (void)co_await pool().offload([]() -> int { throw std::runtime_error("blocking"); });
  } catch (std::runtime_error& e) {
    error = e.what();
  }
  REQUIRE(error == "blocking");
  REQUIRE(local_actor()->id() == 1);
}

APP_TEST("offload: no pool when offload_pool = nullopt", .actors_count = 1, .offload_pool = std::nullopt) {
  REQUIRE(throws([] { (void)pool(); }));
  co_return;
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
