#include "crash.hpp"

#include <boost/stacktrace.hpp>

#include <array>
#include <csignal>
#include <print>

namespace btye::noexport {

static constexpr std::array crash_signals = {
    SIGSEGV, SIGABRT, SIGFPE, SIGILL,
#ifdef SIGBUS
    SIGBUS,
#endif
};

static void on_crash(int sig) {
  std::signal(sig, SIG_DFL);
  std::println(stderr, "btye: crash, signal {}", sig);  // до построения стека
  std::println(stderr, "{}", boost::stacktrace::to_string(boost::stacktrace::stacktrace()));
  std::fflush(stderr);  // если stderr сделали буферизованным
  std::raise(sig);
}

aa::move_only_fn<void() noexcept> handle_crash() {
  using handler_t = void (*)(int);
  std::array<handler_t, crash_signals.size()> old;
  for (size_t i = 0; i < crash_signals.size(); ++i)
    old[i] = std::signal(crash_signals[i], on_crash);
  return [old] noexcept {
    for (size_t i = 0; i < crash_signals.size(); ++i)
      std::signal(crash_signals[i], old[i]);
  };
}

}  // namespace btye::noexport
