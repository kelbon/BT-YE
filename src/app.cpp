#include "btye/app.hpp"
#include "btye/invoke.hpp"

#include "crash.hpp"
#include "interrupt.hpp"

#include <hidi/asio/io.hpp>
#include <kelcoro/job.hpp>
#include <kelcoro/road.hpp>

#include <atomic>
#include <cassert>
#include <latch>
#include <memory>
#include <mutex>
#include <print>
#include <thread>
#include <vector>

namespace btye {

static thread_local actor* current_actor = nullptr;
// для потоков, которые не акторы (all_actors). На акторах не читается
static std::atomic<noexport::app_state*> foreign_app = nullptr;

namespace noexport {

struct app_state {
  std::span<actor> actors;
  // + 1 от main, + 1 пока не прошёл stop_actors
  std::atomic_size_t background = 2;
  // только на акторе 0
  aa::move_only_fn<void() noexcept> stop_interrupt;
  // только на акторе 0
  bool stop_started = false;
  // только на акторе 0: все фазы остановки прошли
  bool stop_done = false;
  // только на акторе 0: wait_stopped
  dd::awaiters_queue<dd::task_node> stop_waiters;
  // пишется по ходу `main`, читается после конца циклов
  int exit_code = 0;

  static void init_actor(actor& a, app_state& s, size_t i) noexcept {
    a.app = &s;
    a.idx = i;
  }

  static void run_loop(actor& a) noexcept {
    a.ctx.run();
  }

  static void log_exception(std::string_view where, std::exception_ptr e) noexcept {
    try {
      std::rethrow_exception(std::move(e));
    } catch (const std::exception& e) {
      std::println(stderr, "btye: exception from {}: {}", where, e.what());
    } catch (...) {
      std::println(stderr, "btye: unknown exception from {}", where);
    }
  }

  static dd::job release_actors(app_state& s) {
    for (actor& a : s.actors) {
      co_await jump_on(a);
      a.ctx.end_task();
    }
  }

  void background_done() {
    if (background.fetch_sub(1, std::memory_order_acq_rel) == 1)
      release_actors(*this);
  }

  static dd::job run_background(app_state& s, dd::task<void> t) {
    try {
      co_await std::move(t);
    } catch (...) {
      log_exception("background task", std::current_exception());
    }
    s.background_done();
  }

  static stop_subscription subscribe_stop(actor& a, stop_phases_e p, aa::move_only_fn<dd::task<void>()> fn) {
    stop_subscription sub(std::move(fn));
    a.stop_handlers[size_t(p)].push_back(sub);
    return sub;
  }

  // обработчики фазы `p` текущего актора параллельно, ждёт все
  static dd::task<void> run_phase(stop_phases_e p) {
    std::vector<dd::task<void>> tasks;
    for (stop_subscription& sub : local_actor_ref().stop_handlers[size_t(p)]) {
      try {
        tasks.push_back(sub.fn());
      } catch (...) {
        log_exception("stop handler", std::current_exception());
      }
    }
    auto results = co_await dd::when_all(std::move(tasks));
    for (auto& r : results)
      if (!r)
        log_exception("stop handler", r.error());
  }

  struct stop_waiter : dd::task_node {
    app_state& s;

    explicit stop_waiter(app_state& s) noexcept : s(s) {
    }

    static bool await_ready() noexcept {
      return false;
    }
    void await_suspend(std::coroutine_handle<> h) noexcept {
      task = h;
      s.stop_waiters.push(this);
    }
    static void await_resume() noexcept {
    }
  };

  static dd::task<void> wait_stopped(actor& from) {
    app_state& s = *from.app;
    co_await jump_on(s.actors[0]);
    if (!s.stop_done)
      co_await stop_waiter(s);
    co_await jump_on(from);
  }

  static dd::job stop_actors(app_state& s) {
    co_await jump_on(s.actors[0]);
    if (std::exchange(s.stop_started, true))
      co_return;
    // дальше сигналы обрабатывает система: повторный Ctrl+C во время остановки завершает процесс
    if (s.stop_interrupt)
      s.stop_interrupt();
    co_await invoke_on_all([]() -> dd::task<void> {
      actor& a = local_actor_ref();
      a.is_stopped = true;
      a.stop_src.request_abort();
      co_return;
    });
    // invoke_on_all возвращается, когда фаза закончилась на всех акторах
    for (size_t p = 0; p < size_t(stop_phases_e::COUNT); ++p)
      co_await invoke_on_all([p] { return run_phase(stop_phases_e(p)); });
    s.stop_done = true;
    dd::attach_list(s.actors[0], s.stop_waiters.pop_all());
    s.background_done();
  }

  static void request_stop(actor& a) {
    if (!a.is_stopped)
      stop_actors(*a.app);
  }

  static dd::job start_main(app_state& s, aa::move_only_fn<dd::task<int>()>& main) {
    actor& a0 = s.actors[0];
    co_await yield();
    int code;
    try {
      code = co_await main();
    } catch (...) {
      code = 1;
      log_exception("main", std::current_exception());
    }
    s.exit_code = code;
    // `main` - фоновая задача, циклы живы: вернуться на актор можно всегда
    co_await jump_on(a0);
    request_stop(a0);
    s.background_done();
  }
};

}  // namespace noexport

actor_context_factory_t default_actor_context_factory() {
  return [] noexcept { return hidi::make_asio_io_context(); };
}

stop_subscription actor::on_stop(stop_phases_e p, aa::move_only_fn<dd::task<void>()> handler) {
  assert(current_actor == this && "on_stop must be called on the actor's thread");
  assert(!is_stopped && "on_stop called after the stop started");
  return noexport::app_state::subscribe_stop(*this, p, std::move(handler));
}

actor* local_actor() noexcept {
  return current_actor;
}

std::span<actor> all_actors() noexcept {
  if (current_actor)
    return current_actor->app->actors;
  noexport::app_state* s = foreign_app.load(std::memory_order_acquire);
  return s ? s->actors : std::span<actor>{};
}

void run_in_background(dd::task<void> t) {
  noexport::app_state& s = *local_actor_ref().app;
  size_t was = s.background.fetch_add(1, std::memory_order_acq_rel);
  assert(was != 0 && "run_in_background after the loops are released");
  (void)was;
  noexport::app_state::run_background(s, std::move(t));
}

void request_stop() noexcept {
  noexport::app_state::request_stop(local_actor_ref());
}

dd::task<void> wait_stopped() {
  return noexport::app_state::wait_stopped(local_actor_ref());
}

int run(app_options opts, aa::move_only_fn<dd::task<int>()> main, actor_context_factory_t factory) {
  using noexport::app_state;
  aa::move_only_fn<void() noexcept> restore_crash_handlers;
  if (opts.handle_crash)
    restore_crash_handlers = noexport::handle_crash();
  size_t n = opts.actors_count ? opts.actors_count : std::max(1u, std::thread::hardware_concurrency());
  std::unique_ptr<actor[]> actors(new actor[n]);
  app_state s{.actors = {actors.get(), n}};
  for (size_t i = 0; i < n; ++i)
    app_state::init_actor(actors[i], s, i);

  std::mutex factory_mtx;

  auto create_context = [&](actor& a) noexcept {
    std::lock_guard l(factory_mtx);
    a.ioctx() = factory();   // вызов на потоке акторе
    a.ioctx().start_task();  // снимется в release_actors
  };
  actor& a0 = actors[0];
  current_actor = &a0;
  create_context(a0);
  if (opts.handle_interrupt)
    s.stop_interrupt = noexport::handle_interrupt(a0);

  std::latch ready{ptrdiff_t(n - 1)};
  std::latch loops_done{ptrdiff_t(n)};
  std::vector<std::thread> threads;
  threads.reserve(n - 1);
  for (size_t i = 1; i < n; ++i) {
    threads.emplace_back([&, i]() noexcept {
      actor& a = actors[i];
      current_actor = &a;
      create_context(a);
      ready.count_down();
      app_state::run_loop(a);
      loops_done.arrive_and_wait();
      a.ioctx().reset();
      current_actor = nullptr;
    });
  }
  ready.wait();
  factory.reset();
  foreign_app.store(&s, std::memory_order_release);

  app_state::start_main(s, main);
  app_state::run_loop(a0);
  foreign_app.store(nullptr, std::memory_order_release);
  loops_done.arrive_and_wait();
  a0.ioctx().reset();
  current_actor = nullptr;
  for (std::thread& t : threads)
    t.join();
  if (restore_crash_handlers)
    restore_crash_handlers();
  return s.exit_code;
}

}  // namespace btye
