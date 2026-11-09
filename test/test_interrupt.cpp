#include "test_utils.hpp"

#include <atomic>
#include <csignal>
#include <thread>

#ifndef _WIN32
  #include <unistd.h>
#endif

using btye::test::actors;

// сигнал процессу (POSIX - kill): его получает любой поток, а не только текущий
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
      auto factory = asio ? btye::default_actor_context_factory() : btye::test::not_asio_factory();
      int code = btye::run(
          actors(3),
          [&]() -> dd::task<int> {
            bool on_stop = false;
            btye::stop_subscription sub =
                btye::local_actor_ref().on_stop(btye::stop_phases_e::first, [&]() -> dd::task<void> {
                  on_stop = true;
                  co_return;
                });
            send_signal(sig);
            co_await btye::wait_stopped();
            stopped = on_stop;
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
    auto factory = [&, inner = asio ? btye::default_actor_context_factory()
                                    : btye::test::not_asio_factory()]() mutable noexcept {
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

// поток, созданный до run (пул, сторонняя библиотека), может получить сигнал: обработка от этого не зависит
TEST("signal is handled when it may be delivered to a thread created before run") {
  for (bool asio : asio_paths) {
    std::atomic_bool done = false;
    std::thread foreign([&] {
      while (!done)
        std::this_thread::yield();
    });
    auto factory = asio ? btye::default_actor_context_factory() : btye::test::not_asio_factory();
    int code = btye::run(
        actors(2),
        [&]() -> dd::task<int> {
          send_signal(SIGTERM);
          co_await btye::wait_stopped();
          co_return 6;
        },
        std::move(factory));
    done = true;
    foreign.join();
    REQUIRE(code == 6);
  }
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
