#include <moko3/moko3.hpp>

#include <btye/testing.hpp>

#include "kv/client.hpp"
#include "store.hpp"

#include <chrono>
#include <format>

using namespace std::chrono_literals;

static btye::task<void> put(kv::client& cli, std::string key, std::string value) {
  kv::putValue op;
  op.path.key = std::move(key);
  op.body = std::move(value);
  (void)co_await cli.send(op, hidi::deadline_after(5s));
}

static btye::task<kv::getValue::result_t> get(kv::client& cli, std::string key) {
  kv::getValue op;
  op.path.key = std::move(key);
  co_return co_await cli.send(op, hidi::deadline_after(5s));
}

BTYE_TEST("kv: put, get, delete", .actors_count = 3) {
  btye::mt_server<kv_store> server;
  kv::client cli{btye::client_to(co_await btye::testing::start_local(server))};

  REQUIRE((co_await get(cli, "a")).get_if_404());
  co_await put(cli, "a", "1");
  co_await put(cli, "a", "2");
  kv::getValue::result_t got = co_await get(cli, "a");
  auto* found = got.get_if_200();
  REQUIRE(found && found->body == "2");

  kv::deleteValue del;
  del.path.key = "a";
  REQUIRE((co_await cli.send(del, hidi::deadline_after(5s))).get_if_204());
  REQUIRE((co_await cli.send(del, hidi::deadline_after(5s))).get_if_404());
  co_await server.shutdown();
}

BTYE_TEST("kv: keys of all actors are counted", .actors_count = 3) {
  btye::mt_server<kv_store> server;
  kv::client cli{btye::client_to(co_await btye::testing::start_local(server))};
  for (int i = 0; i < 20; ++i)
    co_await put(cli, std::format("key{}", i), "v");
  kv::getSize::result_t size = co_await cli.send(kv::getSize{}, hidi::deadline_after(5s));
  REQUIRE(size.body.count == 20);
  co_await server.shutdown();
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
