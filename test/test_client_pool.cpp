#include "test_utils.hpp"

#include "btye/client_pool.hpp"
#include "btye/invoke.hpp"
#include "btye/io.hpp"
#include "btye/mt_server.hpp"
#include "btye/sleep.hpp"

#include <atomic>
#include <chrono>

using namespace std::chrono_literals;

using btye::test::id_server;
using btye::test::localhost;

static btye::task<int> status_of(btye::raw_client& c) {
  hidi::http_response rsp = co_await svarg::cxx_support::send_buffered(c, hidi::http_request{.path = "/"},
                                                                       hidi::deadline_after(5s));
  co_return rsp.status;
}

static bool throws(auto f) {
  try {
    f();
  } catch (std::exception&) {
    return true;
  }
  return false;
}

APP_TEST("local_client_pool: a client per address and params, stop", .actors_count = 2) {
  btye::mt_server<id_server> server;
  co_await server.start(nullptr);
  hidi::internet_address addr = server.listen_home(localhost());

  btye::local_client_pool pool;
  btye::raw_client a = pool.get_client_to(addr);
  btye::raw_client b = pool.get_client_to(addr);
  REQUIRE(pool.size() == 1);
  hidi::internet_address local(boost::asio::ip::make_address("127.0.0.1"), 0);
  btye::raw_client c = pool.get_client_to(addr, {.src_addr = local});
  REQUIRE(pool.size() == 2);
  REQUIRE(co_await status_of(a) == 200 && co_await status_of(b) == 200 && co_await status_of(c) == 200);

  co_await pool.stop();
  REQUIRE(pool.size() == 0);
  REQUIRE(throws([&] { (void)pool.get_client_to(addr); }));
  // выданные после остановки не отправляют запросы (иначе соединились бы снова)
  bool cancelled = false;
  try {
    (void)co_await status_of(a);
  } catch (std::exception&) {
    cancelled = true;
  }
  REQUIRE(cancelled);
}

APP_TEST("local_client_pool: idle clients are collected, used are kept", .actors_count = 1) {
  btye::mt_server<id_server> server;
  co_await server.start(nullptr);
  hidi::internet_address addr = server.listen_home(localhost());

  btye::local_client_pool pool({.idle_timeout = 20ms});
  btye::raw_client kept = pool.get_client_to(addr);
  {
    hidi::internet_address local(boost::asio::ip::make_address("127.0.0.1"), 0);
    btye::raw_client idle = pool.get_client_to(addr, {.src_addr = local});
    REQUIRE(co_await status_of(idle) == 200);
  }
  REQUIRE(pool.size() == 2);
  (void)co_await btye::sleep(100ms);
  REQUIRE(pool.size() == 1);
  REQUIRE(co_await status_of(kept) == 200);
  co_await pool.stop();
}

APP_TEST("client_to: on every actor, stopped in the clients phase", .actors_count = 3) {
  btye::mt_server<id_server> server;
  co_await server.start(nullptr);
  hidi::internet_address addr = server.listen_home(localhost());
  std::atomic_size_t ok = 0;
  co_await btye::invoke_on_all([&]() -> btye::task<void> {
    btye::raw_client c = btye::client_to(addr);
    if (co_await status_of(c) == 200)
      ++ok;
  });
  REQUIRE(ok == 3);
  btye::request_stop();
  co_await btye::wait_stopped();
  REQUIRE(throws([&] { (void)btye::client_to(addr); }));
}

APP_TEST("client_to: the default pool is disabled", .actors_count = 1, .connection_pool = std::nullopt) {
  REQUIRE(
      throws([] { (void)btye::client_to(hidi::endpoint(boost::asio::ip::make_address("127.0.0.1"), 1)); }));
  co_return;
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
