#include "test_utils.hpp"

#include "btye/invoke.hpp"

#include <hidi/asio/io.hpp>
#include <kelcoro/job.hpp>
#include <kelcoro/latch.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

using btye::all_actors;
using btye::local_actor;

using btye::test::actors;

using btye::yield;

TEST("main on actor 0, jumps between actors") {
  std::vector<size_t> visited;
  int code = btye::run(actors(3), [&]() -> dd::task<int> {
    visited.push_back(local_actor()->id());
    for (btye::actor& a : all_actors()) {
      co_await btye::jump_on(a);
      visited.push_back(local_actor()->id());
    }
    co_return 7;
  });
  REQUIRE(code == 7);
  REQUIRE((visited == std::vector<size_t>{0, 0, 1, 2}));
  // вне run акторов нет
  REQUIRE(all_actors().empty() && !local_actor());
}

TEST("jump_on by id") {
  std::vector<size_t> visited;
  std::atomic_size_t foreign_visited = 0;
  int code = btye::run(actors(3), [&]() -> dd::task<int> {
    for (size_t id : {2u, 0u, 1u}) {
      co_await btye::jump_on_actor(id);
      visited.push_back(local_actor()->id());
    }
    REQUIRE(&btye::get_actor(1) == local_actor());
    std::thread t([&] {
      foreign_visited = []() -> dd::async_task<size_t> {
        co_await btye::jump_on_actor(2);
        co_return local_actor()->id();
      }()
                                    .get();
    });
    while (foreign_visited == 0)
      co_await yield();
    t.join();
    co_return 0;
  });
  REQUIRE(code == 0 && foreign_visited == 2);
  REQUIRE((visited == std::vector<size_t>{2, 0, 1}));
}

TEST("jump_on the current actor does not suspend, yield does") {
  std::vector<int> order;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    // задача в очереди актора 0
    [](std::vector<int>& order) -> dd::job {
      co_await yield();
      order.push_back(2);
    }(order);
    co_await btye::jump_on(all_actors()[0]);  // уже здесь: очередь не трогаем
    order.push_back(1);
    co_await yield();  // задача из очереди выполнится раньше
    order.push_back(3);
    co_return 0;
  });
  REQUIRE(code == 0);
  REQUIRE((order == std::vector<int>{1, 2, 3}));
}

TEST("jump from a foreign thread") {
  size_t visited = 0;
  bool foreign_is_actor = true;
  size_t foreign_sees = 0;
  // латч живёт дольше run: dd::latch нельзя разрушать сразу после wait, count_down может ещё выполняться
  std::optional<dd::latch> done;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    done.emplace(1, all_actors()[0]);
    std::thread t([&] {
      foreign_is_actor = local_actor() != nullptr;
      foreign_sees = all_actors().size();
      [](btye::actor& a, size_t& visited, dd::latch& done) -> dd::job {
        co_await btye::jump_on(a);
        visited = local_actor()->id();
        done.count_down();
      }(all_actors()[1], visited, *done);
    });
    co_await done->wait();
    t.join();
    co_return 0;
  });
  REQUIRE(code == 0 && visited == 1 && !foreign_is_actor && foreign_sees == 2);
  // вне run акторов нет и на чужом потоке
  std::thread([&] { foreign_sees = all_actors().size(); }).join();
  REQUIRE(foreign_sees == 0);
}

TEST("result to a foreign thread through async_task") {
  std::atomic_size_t result = 0;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    std::thread t([&] {
      auto on_actor = []() -> dd::async_task<size_t> {
        co_await btye::jump_on(all_actors()[1]);
        co_return local_actor()->id() + 10;
      };
      result = on_actor().get();
    });
    // ждём, не блокируя актор 0: поток держит актор 1
    while (result == 0)
      co_await yield();
    t.join();
    co_return 0;
  });
  REQUIRE(code == 0 && result == 11);
}

TEST("stop reaches every actor") {
  // на каждом акторе задача ждёт остановки своего актора и после неё никуда не прыгает
  std::vector<int> seen(3, 0);
  int code = btye::run(actors(3), [&]() -> dd::task<int> {
    for (btye::actor& a : all_actors()) {
      [](btye::actor& a, int& seen) -> dd::job {
        co_await btye::jump_on(a);
        seen = a.stopped() ? -1 : 1;
        while (!a.stopped())
          co_await yield();
        seen += 1;
      }(a, seen[a.id()]);
    }
    btye::request_stop();
    btye::request_stop();  // повторный ничего не делает
    co_return 3;
  });
  REQUIRE(code == 3);
  REQUIRE((seen == std::vector<int>(3, 2)));
}

TEST("stop requested from a foreign thread") {
  int code = btye::run(actors(2), []() -> dd::task<int> {
    // request_stop - только на акторе, чужой поток сначала прыгает
    std::thread t([a = &all_actors()[1]] {
      [](btye::actor& a) -> dd::job {
        co_await btye::jump_on(a);
        btye::request_stop();
      }(*a);
    });
    while (!local_actor()->stopped())
      co_await yield();
    t.join();
    co_return 5;
  });
  REQUIRE(code == 5);
}

TEST("exception from main") {
  int code = btye::run(actors(2), []() -> dd::task<int> {
    throw std::runtime_error("main failed");
    co_return 0;
  });
  REQUIRE(code == 1);
}

TEST("factory is called on each actor and destroyed before main") {
  std::shared_ptr<int> marker = std::make_shared<int>();
  std::weak_ptr<int> alive = marker;
  std::vector<std::thread::id> created_on;
  std::vector<std::thread::id> actor_threads(3);
  bool factory_destroyed = false;
  int code = btye::run(
      actors(3),
      [&]() -> dd::task<int> {
        factory_destroyed = alive.expired();
        for (btye::actor& a : all_actors()) {
          co_await btye::jump_on(a);
          actor_threads[a.id()] = std::this_thread::get_id();
        }
        co_return 0;
      },
      [&, marker = std::move(marker)] noexcept {
        created_on.push_back(std::this_thread::get_id());
        return hidi::make_asio_io_context();
      });
  REQUIRE(code == 0 && factory_destroyed);
  std::ranges::sort(created_on);
  std::ranges::sort(actor_threads);
  REQUIRE(created_on == actor_threads);
}

TEST("main returns to actor 0 after stop") {
  // остановку запросили, пока main на другом акторе: цикл актора 0 держит main
  int code = btye::run(actors(2), []() -> dd::task<int> {
    co_await btye::jump_on(all_actors()[1]);
    btye::request_stop();
    while (!local_actor()->stopped())
      co_await yield();
    co_await btye::jump_on(all_actors()[0]);
    co_return local_actor()->id() == 0 ? 9 : 1;
  });
  REQUIRE(code == 9);
}

TEST("stop phases: in order, a phase ends on all actors before the next") {
  constexpr size_t n = 3;
  std::array<std::atomic_size_t, size_t(btye::stop_phases_e::COUNT)> finished{};
  std::atomic_bool in_order = true, stopped_in_handlers = true;
  // подписки живут до остановки, у каждого актора свои
  std::array<std::vector<btye::stop_subscription>, n> subs;
  int code = btye::run(actors(n), [&]() -> dd::task<int> {
    co_await btye::invoke_on_all([&]() -> dd::task<void> {
      btye::actor& a = btye::local_actor_ref();
      for (size_t p = 0; p < size_t(btye::stop_phases_e::COUNT); ++p) {
        subs[a.id()].push_back(a.on_stop(btye::stop_phases_e(p), [&, p]() -> dd::task<void> {
          if (p > 0 && finished[p - 1] != n)
            in_order = false;
          if (!local_actor()->stopped())
            stopped_in_handlers = false;
          // без границы между фазами следующая успела бы начаться на другом акторе
          for (int i = 0; i < 3; ++i)
            co_await yield();
          ++finished[p];
        }));
      }
      co_return;
    });
    co_return 0;
  });
  REQUIRE(code == 0 && in_order && stopped_in_handlers);
  for (auto& f : finished)
    REQUIRE(f == n);
}

TEST("stop subscription destroyed before the stop is not called") {
  bool called = false;
  int code = btye::run(actors(1), [&]() -> dd::task<int> {
    {
      btye::stop_subscription sub = local_actor()->on_stop(btye::stop_phases_e::servers, [&]() -> dd::task<void> {
        called = true;
        co_return;
      });
      // перемещённая подписка заменяет исходную
      btye::stop_subscription moved = std::move(sub);
    }
    co_return 0;
  });
  REQUIRE(code == 0 && !called);
}

TEST("wait_stopped: after all phases, returns to the caller actor") {
  bool phase_done = false;
  bool after_phases = false;
  btye::stop_subscription sub;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    sub = local_actor()->on_stop(btye::stop_phases_e::last, [&]() -> dd::task<void> {
      co_await yield();
      phase_done = true;
    });
    co_await btye::jump_on_actor(1);
    btye::request_stop();
    co_await btye::wait_stopped();
    after_phases = phase_done && local_actor()->id() == 1;
    // после остановки - сразу
    co_await btye::wait_stopped();
    co_return 3;
  });
  REQUIRE(code == 3 && after_phases);
}

TEST("background task started from a stop handler keeps all loops alive") {
  // обработчик запускает фоновую задачу, которая ходит по акторам уже после всех фаз и конца main
  std::vector<size_t> visited;
  btye::stop_subscription sub;
  int code = btye::run(actors(3), [&]() -> dd::task<int> {
    sub = local_actor()->on_stop(btye::stop_phases_e::last, [&]() -> dd::task<void> {
      btye::run_in_background([](std::vector<size_t>& visited) -> dd::task<void> {
        for (int round = 0; round < 3; ++round) {
          for (btye::actor& a : all_actors()) {
            co_await btye::jump_on(a);
            visited.push_back(a.id());
            co_await yield();
          }
        }
      }(visited));
      co_return;
    });
    co_return 4;
  });
  REQUIRE(code == 4);
  REQUIRE((visited == std::vector<size_t>{0, 1, 2, 0, 1, 2, 0, 1, 2}));
}

TEST("main waits for the stop requested from a foreign thread") {
  bool stopped = false;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    std::thread t([] {
      [](btye::actor& a) -> dd::job {
        co_await btye::jump_on(a);
        btye::request_stop();
      }(all_actors()[1]);
    });
    co_await btye::wait_stopped();
    t.join();
    // main - фоновая задача: после остановки можно ходить по акторам
    for (btye::actor& a : all_actors())
      co_await btye::jump_on(a);
    stopped = true;
    co_return 5;
  });
  REQUIRE(code == 5 && stopped);
}

TEST("exceptions from stop handlers and background tasks are only logged") {
  bool other_called = false;
  btye::stop_subscription throws, other;
  int code = btye::run(actors(1), [&]() -> dd::task<int> {
    throws = local_actor()->on_stop(btye::stop_phases_e::first, []() -> dd::task<void> {
      throw std::runtime_error("stop handler failed");
      co_return;
    });
    other = local_actor()->on_stop(btye::stop_phases_e::first, [&]() -> dd::task<void> {
      other_called = true;
      co_return;
    });
    co_return 0;
  });
  REQUIRE(code == 0 && other_called);

  code = btye::run(actors(2), []() -> dd::task<int> {
    btye::run_in_background([]() -> dd::task<void> {
      co_await btye::jump_on(all_actors()[1]);
      throw std::runtime_error("background failed");
    }());
    co_return 0;
  });
  REQUIRE(code == 0);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
