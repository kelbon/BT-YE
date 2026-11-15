// как тесты пишет пользователь: только btye/testing.hpp и moko3
#include <moko3/moko3.hpp>

#include "btye/h2.hpp"
#include "btye/testing.hpp"

#include "actors_api/client.hpp"
#include "actors_api/server.hpp"

#include <chrono>
#include <stdexcept>

using namespace std::chrono_literals;
namespace h2 = btye::h2;

struct whoami_server : actors_api::server {
  using actors_api::server::server;

  btye::task<h2::http_response> handle(actors_api::whoami, h2::http_request, h2::request_context) override {
    actors_api::whoami::response_200 rsp;
    rsp.body.actor = btye::local_actor()->id();
    co_return h2::make_response(rsp);
  }
};

BTYE_TEST("testing: service on localhost and a client to it", .actors_count = 2) {
  REQUIRE(btye::all_actors().size() == 2 && btye::local_actor()->id() == 0);
  btye::mt_server<whoami_server> server;
  h2::internet_address addr = co_await btye::testing::start_local(server);
  actors_api::client cli{btye::client_to(addr)};
  actors_api::whoami::result_t rsp = co_await cli.send(actors_api::whoami{}, h2::deadline_after(5s));
  REQUIRE(rsp.body.actor < 2);
  co_await server.shutdown();
}

BTYE_TEST("testing: wait_until") {
  int calls = 0;
  REQUIRE(co_await btye::testing::wait_until([&] { return ++calls == 3; }, 5s));
  REQUIRE(calls == 3);
  int task_calls = 0;
  REQUIRE(co_await btye::testing::wait_until([&]() -> btye::task<bool> { co_return ++task_calls == 2; }, 5s));
  REQUIRE(task_calls == 2);
  auto start = std::chrono::steady_clock::now();
  REQUIRE(!co_await btye::testing::wait_until([] { return false; }, 20ms));
  REQUIRE(std::chrono::steady_clock::now() - start >= 20ms);
}

TEST("testing: run_test rethrows the exception of the body") {
  REQUIRE_THROWS(btye::testing::run_test({.actors_count = 1}, []() -> btye::task<void> {
    throw std::runtime_error("body failed");
    co_return;
  }));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
