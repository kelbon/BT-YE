#include "btye/app.hpp"

#include "crash.hpp"
#include "interrupt.hpp"

#include <hidi/asio/io.hpp>
#include <kelcoro/job.hpp>

#include <atomic>
#include <cassert>
#include <latch>
#include <memory>
#include <mutex>
#include <print>
#include <ranges>
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
  // пишутся по ходу `main` и остановки, читаются после конца циклов
  bool exit_callback_failed = false;
  int exit_code = 0;

  static void init_actor(actor& a, app_state& s, size_t i) noexcept {
    a.app = &s;
    a.idx = i;
  }

  static void run_loop(actor& a) noexcept {
    a.ctx.run();
  }

  static void log_exception(std::string_view where) noexcept {
    try {
      throw;
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
      log_exception("background task");
    }
    s.background_done();
  }

  static dd::job stop_actors(app_state& s) {
    co_await jump_on(s.actors[0]);
    if (std::exchange(s.stop_started, true))
      co_return;
    // дальше сигналы обрабатывает система: повторный Ctrl+C во время остановки завершает процесс
    if (s.stop_interrupt)
      s.stop_interrupt();
    for (actor& a : s.actors) {
      co_await jump_on(a);
      a.is_stopped = true;
      a.stop_src.request_abort();
      for (auto& cb : a.exit_callbacks | std::views::reverse) {
        try {
          cb();
        } catch (...) {
          s.exit_callback_failed = true;
          log_exception("at_exit callback");
        }
      }
      a.exit_callbacks.clear();
    }
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
      log_exception("main");
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

void actor::at_exit(aa::move_only_fn<void()> cb) {
  assert(current_actor == this && "at_exit must be called on the actor's thread");
  assert(!is_stopped && "at_exit called after the actor is stopped");
  exit_callbacks.push_back(std::move(cb));
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
  if (s.exit_code == 0 && s.exit_callback_failed)
    return 1;
  return s.exit_code;
}

}  // namespace btye
