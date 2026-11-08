#include "test_utils.hpp"

#include "btye/invoke.hpp"
#include "btye/mt_server.hpp"

#include <hidi/h2client.hpp>

#include <atomic>
#include <chrono>
#include <map>
#include <system_error>

using namespace std::chrono_literals;
using btye::local_actor;
using btye::test::id_server;
using btye::test::localhost;

// по запросу на каждом новом соединении (свой h2client)
static dd::task<std::map<char, int>> ask(hidi::internet_address addr, int connections) {
  std::map<char, int> answers;
  for (int i = 0; i < connections; ++i) {
    hidi::h2client client(addr, nullptr, {}, btye::local_io_ref());
    hidi::http_response rsp =
        co_await client.send_request(hidi::http_request{.path = "/"}, hidi::deadline_after(5s));
    ++answers[rsp.body_strview().at(0)];
    co_await client.graceful_stop();
  }
  co_return answers;
}

APP_TEST("mt_server: connections distributed over actors", .actors_count = 3) {
  btye::mt_server<id_server> server;
  co_await server.start(nullptr);
  hidi::internet_address addr = server.listen0(localhost());
  std::map<char, int> answers = co_await btye::invoke_on(1, ask(addr, 6));
  co_await server.shutdown();
  REQUIRE((answers == std::map<char, int>{{'0', 2}, {'1', 2}, {'2', 2}}));
}

APP_TEST("mt_server: only chosen actors", .actors_count = 3) {
  size_t n = 0;
  btye::mt_server<id_server> server([&n] noexcept { return 1 + n++ % 2; });
  co_await server.start(nullptr);
  hidi::internet_address addr = server.listen0(localhost());
  std::map<char, int> answers = co_await btye::invoke_on(1, ask(addr, 4));
  co_await server.shutdown();
  REQUIRE((answers == std::map<char, int>{{'1', 2}, {'2', 2}}));
}

APP_TEST("mt_server: listen from another actor, error returns to the caller", .actors_count = 3) {
  bool error_on_caller = false;
  btye::mt_server<id_server> server;
  co_await server.start(nullptr);
  co_await btye::jump_on_actor(2);
  hidi::internet_address addr = co_await server.listen(localhost());
  REQUIRE(local_actor()->id() == 2);
  // адрес не этой машины (TEST-NET-1): исключение на вызывающем акторе
  try {
    (void)co_await server.listen(
        {.addr = hidi::internet_address(boost::asio::ip::make_address("192.0.2.1"), 0)});
  } catch (std::exception&) {
    error_on_caller = local_actor()->id() == 2;
  }
  std::map<char, int> answers = co_await btye::invoke_on(1, ask(addr, 3));
  co_await server.shutdown();
  REQUIRE(error_on_caller);
  REQUIRE((answers == std::map<char, int>{{'0', 1}, {'1', 1}, {'2', 1}}));
}

APP_TEST("mt_server: stopped in the servers phase of the stop", .actors_count = 3) {
  btye::mt_server<id_server> server;
  co_await server.start(nullptr);
  hidi::internet_address addr = server.listen0(localhost());
  std::map<char, int> answers = co_await btye::invoke_on(1, ask(addr, 3));
  btye::request_stop();
  co_await btye::wait_stopped();
  // уже остановлен фазой: явная остановка ничего не делает, разрушение без неё
  co_await server.shutdown();
  REQUIRE((answers == std::map<char, int>{{'0', 1}, {'1', 1}, {'2', 1}}));
}

APP_TEST("mt_server: concurrent shutdowns - one stop, others wait for its end", .actors_count = 3) {
  btye::mt_server<id_server> server;
  co_await server.start(nullptr);
  (void)server.listen0(localhost());
  std::atomic_size_t returned = 0;
  // каждый вернулся на свой актор, когда серверы уже остановлены
  co_await btye::invoke_on_all([&]() -> dd::task<void> {
    size_t id = local_actor()->id();
    co_await server.shutdown();
    if (local_actor()->id() == id)
      ++returned;
  });
  REQUIRE(returned == 3);
  // остановка приложения его уже не трогает
  btye::request_stop();
  co_await btye::wait_stopped();
}

APP_TEST("mt_server: destroyed while running is terminated in the background", .actors_count = 3) {
  {
    btye::mt_server<id_server> server;
    co_await server.start(nullptr);
    hidi::internet_address addr = server.listen0(localhost());
    std::map<char, int> answers = co_await btye::invoke_on(1, ask(addr, 3));
    REQUIRE(answers.size() == 3);
  }
  // остановка идёт в фоне и держит циклы: приложение закончится после неё
}

// конструктор с дополнительным аргументом после контекста
struct tagged_server : hidi::h2server {
  char tag;

  tagged_server(hidi::server_ssl_context_ptr ssl, hidi::h2server_options opts, hidi::any_io_context io,
                char tag)
      : hidi::h2server(std::move(ssl), std::move(opts), std::move(io)), tag(tag) {
  }

  dd::task<hidi::http_response> handle_request(hidi::http_request, hidi::request_context) override {
    hidi::http_response rsp{.status = 200};
    rsp.body.push_back(hidi::byte_t(tag));
    co_return rsp;
  }
};

APP_TEST("mt_server: extra arguments of the server constructor", .actors_count = 2) {
  btye::mt_server<tagged_server> server;
  co_await server.start(nullptr, {}, 'x');
  hidi::internet_address addr = server.listen0(localhost());
  std::map<char, int> answers = co_await btye::invoke_on(1, ask(addr, 2));
  co_await server.shutdown();
  REQUIRE((answers == std::map<char, int>{{'x', 2}}));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
