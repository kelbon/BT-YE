#include "test_utils.hpp"

#include "btye/invoke.hpp"
#include "btye/io.hpp"

#include <hidi/asio/awaiters.hpp>
#include <hidi/asio/io_impl.hpp>
#include <hidi/h2client.hpp>
#include <hidi/h2server.hpp>

#include <atomic>
#include <chrono>

using namespace std::chrono_literals;
using btye::local_actor;
using btye::test::id_server;

using btye::test::actors;

TEST("h2server and h2client on actor contexts") {
  std::string answer;
  size_t server_actor = 99;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    id_server server(nullptr, {}, btye::local_io_ref());
    hidi::internet_address addr =
        server.listen({.addr = hidi::internet_address(boost::asio::ip::make_address("127.0.0.1"), 0)});
    answer = co_await btye::invoke_on(1, [](hidi::internet_address addr) -> dd::task<std::string> {
      hidi::h2client client(addr, nullptr, {}, btye::local_io_ref());
      hidi::http_response rsp = co_await client.send_request(
          hidi::http_request{.path = "/", .method = hidi::http_method_e::GET}, hidi::deadline_after(5s));
      // деструктор h2client внутри цикла не дожидается чтения соединения
      co_await client.graceful_stop();
      co_return std::string(rsp.body_strview());
    }(addr));
    server_actor = local_actor()->id();
    co_await server.shutdown();
    co_return 0;
  });
  REQUIRE(code == 0 && answer == "0" && server_actor == 0);
}

TEST("create_timer: the timer of the current actor") {
  size_t woke_on = 99;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    woke_on = co_await btye::invoke_on(1, []() -> dd::task<size_t> {
      hidi::any_timer t = btye::create_timer();
      hidi::io_error_code ec;
      auto start = std::chrono::steady_clock::now();
      co_await hidi::net.sleep(t, 10ms, ec);
      REQUIRE(!ec && std::chrono::steady_clock::now() - start >= 10ms);
      co_return local_actor()->id();
    }());
    co_return 0;
  });
  REQUIRE(code == 0 && woke_on == 1);
}

TEST("local_io_ref: asio directly, other contexts and asio with starter through forwarding") {
  // по умолчанию asio_io: ссылка на тот же asio::io_context
  bool direct = false;
  btye::run(actors(2), [&]() -> dd::task<int> {
    hidi::any_io_context ref = btye::local_io_ref();
    hidi::asio_ref_io* r = aa::any_cast<hidi::asio_ref_io>(&ref);
    hidi::asio_io* own = aa::any_cast<hidi::asio_io>(&local_actor()->ioctx());
    direct = r && own && &r->ioctx == &own->ioctx;
    co_return 0;
  });
  REQUIRE(direct);

  // starter не теряется: контекст со starter - через пересылку
  std::atomic_int started = 0;
  std::string answer;
  bool forwarded = false;
  btye::actor_context_factory_t factory = [&] noexcept {
    return hidi::any_io_context(std::in_place_type<hidi::asio_io>,
                                [&](boost::asio::ip::tcp::socket&, hidi::deadline_t) -> dd::task<void> {
                                  ++started;
                                  co_return;
                                });
  };
  int code = btye::run(
      actors(2),
      [&]() -> dd::task<int> {
        hidi::any_io_context ref = btye::local_io_ref();
        forwarded = !aa::any_cast<hidi::asio_ref_io>(&ref);
        id_server server(nullptr, {}, std::move(ref));
        hidi::internet_address addr =
            server.listen({.addr = hidi::internet_address(boost::asio::ip::make_address("127.0.0.1"), 0)});
        answer = co_await btye::invoke_on(1, [](hidi::internet_address addr) -> dd::task<std::string> {
          hidi::h2client client(addr, nullptr, {}, btye::local_io_ref());
          hidi::http_response rsp =
              co_await client.send_request(hidi::http_request{.path = "/"}, hidi::deadline_after(5s));
          co_await client.graceful_stop();
          co_return std::string(rsp.body_strview());
        }(addr));
        co_await server.shutdown();
        co_return 0;
      },
      std::move(factory));
  REQUIRE(code == 0 && forwarded && answer == "0" && started > 0);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
