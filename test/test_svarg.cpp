#include "test_utils.hpp"

#include "btye/h2.hpp"
#include "btye/invoke.hpp"
#include "btye/mt_server.hpp"

#include "actors_api/client.hpp"
#include "actors_api/server.hpp"

#include <chrono>
#include <map>

using namespace std::chrono_literals;
using btye::local_actor;
namespace h2 = btye::h2;

// сгенерированный svarg сервер: отвечает номером актора, на котором обработан запрос
struct whoami_server : actors_api::server {
  using actors_api::server::server;

  dd::task<h2::http_response> handle(actors_api::whoami, h2::http_request, h2::request_context) override {
    actors_api::whoami::response_200 rsp;
    rsp.body.actor = local_actor()->id();
    co_return h2::make_response(rsp);
  }
};

// сгенерированный клиент поверх h2client на контексте актора, по соединению на запрос
static dd::task<std::map<size_t, int>> ask(h2::internet_address addr, int connections) {
  std::map<size_t, int> answers;
  for (int i = 0; i < connections; ++i) {
    h2::h2client client(addr, nullptr, {}, btye::local_io_ref());
    actors_api::client cli{h2::client_ref(&client)};
    actors_api::whoami::result_t rsp = co_await cli.send(actors_api::whoami{}, h2::deadline_after(5s));
    ++answers[rsp.body.actor];
    co_await client.graceful_stop();
  }
  co_return answers;
}

APP_TEST("svarg: generated server under mt_server, generated client on an actor", .actors_count = 3) {
  btye::mt_server<whoami_server> server;
  co_await server.start();
  h2::internet_address addr =
      server.listen_home({.addr = h2::internet_address(boost::asio::ip::make_address("127.0.0.1"), 0)});
  std::map<size_t, int> answers = co_await btye::invoke_on(2, [&] { return ask(addr, 6); });
  co_await server.shutdown();
  REQUIRE((answers == std::map<size_t, int>{{0, 2}, {1, 2}, {2, 2}}));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
