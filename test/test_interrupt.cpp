#include "btye/app.hpp"

#include <hidi/asio/io_impl.hpp>

#include <moko3/moko3.hpp>

#include <atomic>
#include <csignal>

#ifndef _WIN32
  #include <unistd.h>
#endif

static btye::app_options actors(size_t n) {
  return btye::app_options{.actors_count = n};
}

// контекст asio, но не hidi::asio_io: обработка прерывания без asio (поток sigwait)
struct not_asio_io : hidi::asio_io {
  using hidi::asio_io::asio_io;

  static void rebind_context(hidi::any_connection_t&, hidi::any_io_context_ref) {
    std::abort();  // в тесте соединений нет
  }
};

static btye::actor_context_factory_t not_asio_factory() {
  return [] { return hidi::any_io_context(std::in_place_type<not_asio_io>); };
}

// сигнал процессу. POSIX - kill: raise доставил бы его только текущему потоку, где сигнал заблокирован для
// sigwait, и поток sigwait бы его не получил
static void send_signal(int sig) {
#ifdef _WIN32
  std::raise(sig);
#else
  kill(getpid(), sig);
#endif
}

// какие пути обработки проверяются. На Windows без asio - SetConsoleCtrlHandler, Ctrl+C из теста ему не
// отправить (GenerateConsoleCtrlEvent получат все процессы консоли)
#ifdef _WIN32
static constexpr bool asio_paths[] = {true};
#else
static constexpr bool asio_paths[] = {true, false};
#endif

TEST("SIGTERM and SIGINT request stop") {
  for (bool asio : asio_paths)
    for (int sig : {SIGTERM, SIGINT}) {
      bool stopped = false;
      auto factory = asio ? btye::default_actor_context_factory() : not_asio_factory();
      int code = btye::run(
          actors(3),
          [&]() -> dd::task<int> {
            bool on_exit = false;
            btye::local_actor()->at_exit([&] { on_exit = true; });
            send_signal(sig);
            while (!btye::local_actor()->stopped())
              co_await btye::yield();
            stopped = on_exit;
            co_return 4;
          },
          std::move(factory));
      REQUIRE(code == 4 && stopped);
    }
}

TEST("signal while actors are created is not lost") {
  for (bool asio : asio_paths) {
    // контекст актора 0 создаётся первым, дальше обработка прерывания уже включена
    std::atomic_int created = 0;
    auto factory = [&, inner = asio ? btye::default_actor_context_factory() : not_asio_factory()]() mutable {
      if (created++ == 1)
        send_signal(SIGTERM);
      return inner();
    };
    bool stopped = false;
    int code = btye::run(
        actors(3),
        [&]() -> dd::task<int> {
          while (!btye::local_actor()->stopped())
            co_await btye::yield();
          stopped = true;
          co_return 5;
        },
        std::move(factory));
    REQUIRE(code == 5 && stopped);
  }
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
