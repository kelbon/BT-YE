#include "test_utils.hpp"

#include <csignal>
#include <string>

#ifndef _WIN32
  #include <sys/wait.h>
  #include <unistd.h>
#endif


static void user_handler(int) {
}

TEST("crash handlers are set for run and restored after") {
  auto old = std::signal(SIGSEGV, user_handler);
  bool set_in_run = false;
  int code = btye::run(btye::app_options{.actors_count = 2, .handle_crash = true}, [&]() -> btye::task<int> {
    auto h = std::signal(SIGSEGV, SIG_DFL);
    std::signal(SIGSEGV, h);
    set_in_run = h != user_handler && h != SIG_DFL;
    co_return 0;
  });
  REQUIRE(code == 0 && set_in_run);
  REQUIRE(std::signal(SIGSEGV, old) == user_handler);
}

#if defined(__has_feature)
  #if __has_feature(thread_sanitizer) || __has_feature(address_sanitizer)
    #define BTYE_TEST_SANITIZER
  #endif
#endif
#if defined(__SANITIZE_THREAD__) || defined(__SANITIZE_ADDRESS__)
  #define BTYE_TEST_SANITIZER
#endif

TEST("handle_crash is off by default under sanitizers") {
#ifdef BTYE_TEST_SANITIZER
  REQUIRE(!btye::app_options{}.handle_crash);
#else
  REQUIRE(btye::app_options{}.handle_crash);
#endif
}

// санитайзеры ставят свои обработчики падений, под TSan печать стека в обработчике зависает
#if !defined(_WIN32) && !defined(BTYE_TEST_SANITIZER)

// запускает `crash` в btye::run в дочернем процессе, возвращает его stderr и сигнал, которым он завершился
static std::pair<std::string, int> crash_in_child(void (*crash)()) {
  int fds[2];
  REQUIRE(pipe(fds) == 0);
  pid_t pid = fork();
  REQUIRE(pid >= 0);
  if (pid == 0) {
    dup2(fds[1], STDERR_FILENO);
    close(fds[0]);
    btye::run(btye::app_options{.actors_count = 2, .handle_crash = true}, [crash]() -> btye::task<int> {
      co_await btye::jump_on_actor(1);
      crash();
      co_return 0;
    });
    _exit(0);
  }
  close(fds[1]);
  std::string out;
  char buf[4096];
  for (ssize_t n; (n = read(fds[0], buf, sizeof(buf))) > 0;)
    out.append(buf, size_t(n));
  close(fds[0]);
  int status = 0;
  waitpid(pid, &status, 0);
  return {out, WIFSIGNALED(status) ? WTERMSIG(status) : 0};
}

static void null_write() {
  volatile int* volatile p = nullptr;
  *p = 1;
}

static void call_abort() {
  std::abort();
}

TEST("crash prints the signal and the stack, then the process dies by the signal") {
  for (auto [crash, sig] : {std::pair{&null_write, SIGSEGV}, std::pair{&call_abort, SIGABRT}}) {
    auto [out, died_by] = crash_in_child(crash);
    REQUIRE(died_by == sig);
    REQUIRE(out.contains("btye: crash, signal " + std::to_string(sig)));
    REQUIRE(out.contains(" 0# "));
  }
}

#endif

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
