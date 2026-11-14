#pragma once

#include "btye/app.hpp"

#include <hidi/asio/io_impl.hpp>
#include <hidi/h2server.hpp>

#include <moko3/moko3.hpp>

#include <atomic>
#include <exception>

namespace btye::test {

inline app_options actors(size_t n) {
  return app_options{.actors_count = n};
}

// живёт на одном акторе: создаётся, присваивается и разрушается на нём. Копия и перемещение - новый объект на
// текущем акторе, перемещённый (пустой) можно разрушить где угодно. Нарушения считает violations(), тест
// проверяет что их 0 (в отличие от dbg_actor_ref работает и в Release)
struct actor_bound {
  actor* home = local_actor();
  bool moved = false;

  actor_bound() = default;
  actor_bound(const actor_bound&) noexcept {
  }
  actor_bound(actor_bound&& o) noexcept {
    o.moved = true;
  }
  actor_bound& operator=(const actor_bound&) noexcept {
    check();
    moved = false;
    return *this;
  }
  actor_bound& operator=(actor_bound&& o) noexcept {
    check();
    moved = false;
    o.moved = true;
    return *this;
  }
  ~actor_bound() {
    if (!moved)
      check();
  }

  void check() const noexcept {
    if (home != local_actor())
      ++violations();
  }
  static std::atomic_int& violations() noexcept {
    static std::atomic_int v = 0;
    return v;
  }
};

// отвечает номером актора, на котором обработан запрос
struct id_server : hidi::h2server {
  using hidi::h2server::h2server;

  dd::task<hidi::http_response> handle_request(hidi::http_request, hidi::request_context) override {
    hidi::http_response rsp{.status = 200};
    rsp.body.push_back(hidi::byte_t('0' + local_actor_ref().id()));
    co_return rsp;
  }
};

// 127.0.0.1, порт выбирает система
inline hidi::server_endpoint localhost() {
  return {.addr = hidi::internet_address(boost::asio::ip::make_address("127.0.0.1"), 0)};
}

// контекст asio, но не hidi::asio_io: btye идёт по путям для любого контекста (прерывание через поток
// sigwait, local_io_ref с пересылкой)
struct not_asio_io : hidi::asio_io {
  using hidi::asio_io::asio_io;

  static void rebind_context(hidi::any_connection_t& con, hidi::any_io_context_ref other) {
    hidi::asio_io& to = aa::any_cast<not_asio_io&>(other);
    hidi::asio_io::rebind_context(con, to);
  }
};

inline actor_context_factory_t not_asio_factory() {
  return [] noexcept { return hidi::any_io_context(std::in_place_type<not_asio_io>); };
}

// исключение теста пробрасывается из run, а не превращается в код 1
template <auto* Body>
void app_test_impl(moko3::section_info* section, app_options opts, actor_context_factory_t factory) {
  std::exception_ptr ex;
  int code = btye::run(
      opts,
      [&]() -> btye::task<int> {
        try {
          co_await Body(section);
        } catch (...) {
          ex = std::current_exception();
        }
        co_return 0;
      },
      std::move(factory));
  if (ex)
    std::rethrow_exception(ex);
  REQUIRE(code == 0);
}

}  // namespace btye::test

#define BTYE_TEST_NAME LOGIC_GUARDS_CONCAT(btye_app_test, __LINE__, __LINE__)

// тело - `main` приложения (btye::task<void>), ему доступен `_section` (SECTION, GENERATE)
// прогоняется на asio и на контексте, который не asio. После NAME - поля app_options:
// APP_TEST("name", .actors_count = 3) { ... }
#define APP_TEST(NAME, ...)                                                                    \
  ::btye::task<void> BTYE_TEST_NAME(::moko3::section_info* _section);                          \
  TEST(NAME) {                                                                                 \
    SECTION("asio", 0) {                                                                       \
      ::btye::test::app_test_impl<&BTYE_TEST_NAME>(_section, ::btye::app_options{__VA_ARGS__}, \
                                                   ::btye::default_actor_context_factory());   \
    }                                                                                          \
    SECTION("not asio", 1) {                                                                   \
      ::btye::test::app_test_impl<&BTYE_TEST_NAME>(_section, ::btye::app_options{__VA_ARGS__}, \
                                                   ::btye::test::not_asio_factory());          \
    }                                                                                          \
  }                                                                                            \
  ::btye::task<void> BTYE_TEST_NAME(::moko3::section_info* _section)
