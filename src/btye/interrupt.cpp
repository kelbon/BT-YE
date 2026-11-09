#include "interrupt.hpp"

#include <boost/asio/signal_set.hpp>
#include <kelcoro/job.hpp>

#include "asio_of.hpp"
#include "btye/app.hpp"
#include "btye/sleep.hpp"

#ifdef _WIN32
  #include <windows.h>
#endif

#include <array>
#include <atomic>
#include <chrono>
#include <csignal>
#include <memory>

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

// не asio: обработчик (общий на процесс, срабатывает на любом потоке) только ставит флаг,
// актор 0 проверяет его по таймеру
static std::atomic_bool interrupted = false;

static void on_signal(int) noexcept {
  interrupted = true;
}

// заканчивается в начале остановки: stop_source отменяется на всех акторах
static dd::job poll_interrupted(actor& a0) {
  while (co_await sleep(std::chrono::milliseconds(100), a0.stop_source()))
    if (interrupted.exchange(false))
      request_stop();
}

static aa::move_only_fn<void() noexcept> fallback_interrupt(actor& a0) {
  interrupted = false;
  struct sigaction sa = {};
  sa.sa_handler = &on_signal;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  std::array<struct sigaction, 2> old;
  sigaction(SIGINT, &sa, &old[0]);
  sigaction(SIGTERM, &sa, &old[1]);
  poll_interrupted(a0);
  return [old] noexcept {
    sigaction(SIGINT, &old[0], nullptr);
    sigaction(SIGTERM, &old[1], nullptr);
  };
}

#endif

aa::move_only_fn<void() noexcept> handle_interrupt(actor& a0) {
  if (hidi::asio_ref_io* io = asio_of(a0.ioctx()))
    return asio_interrupt(io->ioctx);
  return fallback_interrupt(a0);
}

}  // namespace btye::noexport
