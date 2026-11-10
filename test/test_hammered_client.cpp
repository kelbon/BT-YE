#include "test_utils.hpp"

#include "btye/invoke.hpp"
#include "btye/mt_server.hpp"

#include <hidi/h2client.hpp>

#include <atomic>
#include <chrono>

using namespace std::chrono_literals;
using btye::test::id_server;
using btye::test::localhost;

// тестирует сложный случай когда клиент hidi может синхронно прочитать сразу готовое GOAWAY из соединения
// тест добавлен после бага
static dd::task<void> hammer(hidi::internet_address addr, std::atomic_bool& stop, std::atomic_size_t& ok) {
  while (!stop) {
    hidi::h2client client(addr, nullptr, {}, btye::local_io_ref());
    try {
      hidi::http_response rsp =
          co_await client.send_request(hidi::http_request{.path = "/"}, hidi::deadline_after(10ms));
      if (rsp.status == 200)
        ++ok;
    } catch (std::exception&) {
    }
    co_await client.graceful_stop();
  }
}

static dd::task<void> rounds(size_t home, bool local_only = false, bool one_client = false) {
  for (int round = 0; round < 4; ++round) {
    co_await btye::jump_on_actor(home);
    btye::mt_server<id_server> server(
        local_only ? aa::move_only_fn<size_t() noexcept>([home]() noexcept { return home; })
                   : btye::round_robin());
    co_await server.start(nullptr);
    hidi::internet_address addr = co_await server.listen(localhost());
    std::atomic_bool stop = false;
    std::atomic_size_t ok = 0;
    dd::task<void> clients = one_client ? btye::invoke_on(1, [&] { return hammer(addr, stop, ok); })
                                        : btye::invoke_on_all([&] { return hammer(addr, stop, ok); });
    dd::task<void> stopper = [](btye::mt_server<id_server>& server, std::atomic_size_t& ok,
                                std::atomic_bool& stop) -> dd::task<void> {
      while (ok < 8)
        co_await btye::yield();
      co_await server.shutdown();
      stop = true;
    }(server, ok, stop);
    (void)co_await dd::when_all(std::move(clients), std::move(stopper));
    REQUIRE(ok >= 8);
  }
}

APP_TEST("stress single", .actors_count = 1) {
  co_await rounds(0);
}
APP_TEST("stress nodist", .actors_count = 4) {
  co_await rounds(0, true);
}
APP_TEST("stress oneclient", .actors_count = 4) {
  co_await rounds(0, false, true);
}
APP_TEST("stress home 0", .actors_count = 4) {
  co_await rounds(0);
}
APP_TEST("stress home 3", .actors_count = 4) {
  co_await rounds(3);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
