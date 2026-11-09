#include "test_utils.hpp"

#include "btye/invoke.hpp"
#include "btye/mt_server.hpp"

#include "actors_api/client.hpp"
#include "actors_api/server.hpp"

#include <chrono>
#include <map>

using namespace std::chrono_literals;
using btye::local_actor;

// сгенерированный svarg сервер: отвечает номером актора, на котором обработан запрос
struct whoami_server : actors_api::server {
  using actors_api::server::server;

  dd::task<hidi::http_response> handle(actors_api::whoami, hidi::http_request,
                                       hidi::request_context) override {
    actors_api::whoami::response_200 rsp;
    rsp.body.actor = local_actor()->id();
    co_return svarg::cxx_support::make_response(rsp);
  }
};

// сгенерированный клиент поверх h2client на контексте актора, по соединению на запрос
static dd::task<std::map<size_t, int>> ask(hidi::internet_address addr, int connections) {
  std::map<size_t, int> answers;
  for (int i = 0; i < connections; ++i) {
    hidi::h2client h2(addr, nullptr, {}, btye::local_io_ref());
    actors_api::client cli{svarg::cxx_support::client_ref(&h2)};
    actors_api::whoami::result_t rsp = co_await cli.send(actors_api::whoami{}, hidi::deadline_after(5s));
    ++answers[rsp.body.actor];
    co_await h2.graceful_stop();
  }
  co_return answers;
}

APP_TEST("svarg: generated server under mt_server, generated client on an actor", .actors_count = 3) {
  btye::mt_server<whoami_server> server;
  co_await server.start();
  hidi::internet_address addr =
      server.listen0({.addr = hidi::internet_address(boost::asio::ip::make_address("127.0.0.1"), 0)});
  std::map<size_t, int> answers = co_await btye::invoke_on(2, [&] { return ask(addr, 6); });
  co_await server.shutdown();
  REQUIRE((answers == std::map<size_t, int>{{0, 2}, {1, 2}, {2, 2}}));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
