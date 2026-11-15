#pragma once

#include <chrono>
#include <exception>
#include <type_traits>

#include "btye/app.hpp"
#include "btye/mt_server.hpp"
#include "btye/sleep.hpp"

// утилиты для тестов приложений на btye
namespace btye::testing {

// 127.0.0.1, порт выбирает система
inline hidi::server_endpoint localhost() {
  return {.addr = hidi::internet_address(boost::asio::ip::make_address("127.0.0.1"), 0)};
}

// run, где `main` - `body`. Исключение `body` пробрасывается отсюда, а не превращается в код 1
inline void run_test(app_options opts, aa::move_only_fn<task<void>()> body,
                     actor_context_factory_t factory = default_actor_context_factory()) {
  std::exception_ptr ex;
  (void)run(
      std::move(opts),
      [&]() -> task<int> {
        try {
          co_await body();
        } catch (...) {
          ex = std::current_exception();
        }
        co_return 0;
      },
      std::move(factory));
  if (ex)
    std::rethrow_exception(ex);
}

// server.start(args...) и listen на localhost(). Клиент к нему - client_to(адрес)
template <typename S, typename... Args>
task<hidi::internet_address> start_local(mt_server<S>& server, Args... args) {
  co_await server.start(nullptr, hidi::h2server_options::hardened(), std::move(args)...);
  co_return co_await server.listen(localhost());
}

// pred() -> bool или task<bool> вызывается каждые `period`, пока не вернёт true. false - не дождались за
// `timeout` или актор остановлен
template <typename F>
task<bool> wait_until(F pred, hidi::duration_t timeout, hidi::duration_t period = std::chrono::milliseconds(1)) {
  hidi::deadline_t d = hidi::deadline_after(timeout);
  for (;;) {
    bool ok;
    if constexpr (std::is_same_v<std::invoke_result_t<F&>, bool>)
      ok = pred();
    else
      ok = co_await pred();
    if (ok)
      co_return true;
    if (d.is_reached() || !co_await sleep(period))
      co_return false;
  }
}

}  // namespace btye::testing

#define BTYE_TEST_FN LOGIC_GUARDS_CONCAT(btye_test, __LINE__, __LINE__)

// TEST moko3, тело - `main` приложения (btye::task<void>), ему доступен `_section` (SECTION, GENERATE).
// После NAME - поля app_options:
//   BTYE_TEST("name", .actors_count = 2) { ... }
#define BTYE_TEST(NAME, ...)                                                                                \
  static ::btye::task<void> BTYE_TEST_FN(::moko3::section_info* _section);                                  \
  TEST(NAME) {                                                                                              \
    ::btye::testing::run_test(::btye::app_options{__VA_ARGS__}, [&] { return BTYE_TEST_FN(_section); });    \
  }                                                                                                         \
  static ::btye::task<void> BTYE_TEST_FN(::moko3::section_info* _section)
