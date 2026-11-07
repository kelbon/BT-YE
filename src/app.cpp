#include "btye/app.hpp"

#include <hidi/asio/io.hpp>
#include <kelcoro/job.hpp>

#include <cassert>
#include <latch>
#include <memory>
#include <mutex>
#include <print>
#include <thread>
#include <vector>

namespace btye {

static thread_local actor* current_actor = nullptr;

namespace noexport {

struct app_state {
  std::span<actor> actors;
  // только на акторе 0
  bool stop_started = false;
  int exit_code = 0;

  static void init_actor(actor& a, app_state& s, size_t i) noexcept {
    a.app = &s;
    a.idx = i;
  }

  static void run_loop(actor& a) noexcept {
    a.ctx.run();
  }

  static dd::job stop_actors(app_state& s) {
    (void)co_await jump_on(s.actors[0]);
    if (std::exchange(s.stop_started, true))
      co_return;
    for (actor& a : s.actors) {
      (void)co_await jump_on(a);
      a.is_stopped = true;
    }
    for (actor& a : s.actors) {
      (void)co_await jump_on(a);
      a.ctx.end_task();
    }
  }

  static void request_stop(actor& a) {
    if (!a.is_stopped)
      stop_actors(*a.app);
  }

  // `main` держит цикл актора 0 (start_task в run), поэтому вернуться на него можно всегда
  static dd::job run_main(app_state& s, aa::move_only_fn<dd::task<int>()>& main) {
    actor& a0 = s.actors[0];
    // run вызывает на акторе 0 до запуска цикла: `main` выполняется уже в цикле
    (void)co_await yield();
    int code;
    try {
      code = co_await main();
    } catch (const std::exception& e) {
      code = 1;
      std::println(stderr, "btye: exception from main: {}", e.what());
    } catch (...) {
      code = 1;
      std::println(stderr, "btye: unknown exception from main");
    }
    (void)co_await jump_on(a0);
    s.exit_code = code;
    // до end_task: если это последнее удержание, задача остановки уже в очереди и цикл её выполнит
    request_stop(a0);
    a0.ctx.end_task();
  }
};

}  // namespace noexport

actor_context_factory_t default_actor_context_factory() {
  return [] { return hidi::make_asio_io_context(); };
}

actor* this_actor() noexcept {
  return current_actor;
}

std::span<actor> all_actors() noexcept {
  // TODO доступ с другого потока должен быть
  return current_actor ? current_actor->app->actors : std::span<actor>{};
}

void request_stop() noexcept {
  assert(current_actor && "request_stop must be called on an actor thread");
  noexport::app_state::request_stop(*current_actor);
}

int run(app_options opts, aa::move_only_fn<dd::task<int>()> main, actor_context_factory_t factory) {
  using noexport::app_state;
  size_t n = opts.actors_count ? opts.actors_count : std::max(1u, std::thread::hardware_concurrency());
  std::unique_ptr<actor[]> actors(new actor[n]);
  app_state s{.actors = {actors.get(), n}};
  for (size_t i = 0; i < n; ++i)
    app_state::init_actor(actors[i], s, i);

  std::mutex factory_mtx;
  // на потоке актора, цикл удерживается до второго прохода остановки
  auto create_context = [&](actor& a) noexcept {
    std::lock_guard l(factory_mtx);
    a.ioctx() = factory();
    a.ioctx().start_task();
  };
  actor& a0 = actors[0];
  current_actor = &a0;
  create_context(a0);

  std::latch ready{ptrdiff_t(n - 1)};
  // Контекст разрушается, только когда закончились циклы всех акторов: поток, сделавший post в контекст,
  // выходит из post уже после того, как получатель мог выполнить задачу и закончить свой цикл.
  // Конец цикла отправителя означает, что его post завершён
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

  a0.ioctx().start_task();  // держит `main`
  app_state::run_main(s, main);
  app_state::run_loop(a0);
  loops_done.arrive_and_wait();
  a0.ioctx().reset();
  current_actor = nullptr;
  for (std::thread& t : threads)
    t.join();
  return s.exit_code;
}

}  // namespace btye
