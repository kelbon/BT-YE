#include "interrupt.hpp"

#include <boost/asio/signal_set.hpp>
#include <kelcoro/job.hpp>

#include "asio_of.hpp"
#include "btye/app.hpp"

#ifdef _WIN32
  #include <windows.h>
#else
  #include <pthread.h>
#endif

#include <atomic>
#include <csignal>
#include <memory>
#include <thread>

namespace btye::noexport {

// asio: сигнал приходит в цикл актора 0 через boost::asio::signal_set, без своего потока
static aa::move_only_fn<void() noexcept> asio_interrupt(boost::asio::io_context& io) {
  auto set = std::make_unique<boost::asio::signal_set>(io, SIGINT, SIGTERM);
#ifdef SIGBREAK
  set->add(SIGBREAK);
#endif
  // обработчик вызывается в цикле актора 0. При разрушении set ожидание отменяется (operation_aborted)
  set->async_wait([](const boost::system::error_code& ec, int) {
    if (!ec)
      request_stop();
  });
  // ожидание держит цикл актора 0, поэтому set разрушается в начале остановки
  return [set = std::move(set)]() mutable noexcept { set.reset(); };
}

// не на акторе: переходит на актор 0 и вызывает там request_stop. Задачу никто не ждёт, но переход
// безопасен: обработка заканчивается в начале остановки, пока циклы живы, и задача к этому моменту уже в
// очереди актора 0. В очередь контекста можно ставить и до запуска его цикла
static dd::job stop_on(actor& a0) {
  co_await jump_on(a0);
  request_stop();
}

#ifdef _WIN32

static std::atomic<actor*> console_actor = nullptr;

// система вызывает обработчик на своём потоке. Закрытие консоли не обрабатывается: после возврата из
// обработчика система всё равно завершает процесс
static BOOL WINAPI console_handler(DWORD event) {
  if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT)
    return FALSE;
  actor* a0 = console_actor.exchange(nullptr);
  if (!a0)
    return FALSE;
  stop_on(*a0);
  return TRUE;
}

// не asio: SetConsoleCtrlHandler
static aa::move_only_fn<void() noexcept> fallback_interrupt(actor& a0) {
  console_actor = &a0;
  SetConsoleCtrlHandler(console_handler, TRUE);
  return [] noexcept {
    console_actor = nullptr;
    SetConsoleCtrlHandler(console_handler, FALSE);
  };
}

#else

// не asio: сигналы блокируются до создания потоков остальных акторов (они наследуют маску), их принимает
// sigwait отдельного потока
struct sigwait_interrupt {
  sigset_t set;
  sigset_t old;
  std::thread waiter;
  std::atomic_bool exiting = false;

  explicit sigwait_interrupt(actor& a0) {
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &set, &old);
    waiter = std::thread([this, &a0] {
      for (;;) {
        int sig = 0;
        sigwait(&set, &sig);
        if (exiting.load())
          return;
        stop_on(a0);
      }
    });
  }

  // на потоке, который заблокировал сигналы (актор 0)
  ~sigwait_interrupt() {
    exiting = true;
    pthread_kill(waiter.native_handle(), SIGTERM);
    waiter.join();
    pthread_sigmask(SIG_SETMASK, &old, nullptr);
  }
};

static aa::move_only_fn<void() noexcept> fallback_interrupt(actor& a0) {
  return [s = std::make_unique<sigwait_interrupt>(a0)]() mutable { s.reset(); };
}

#endif

aa::move_only_fn<void() noexcept> handle_interrupt(actor& a0) {
  if (hidi::asio_ref_io* io = asio_of(a0.ioctx()))
    return asio_interrupt(io->ioctx);
  return fallback_interrupt(a0);
}

}  // namespace btye::noexport
