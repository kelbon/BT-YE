#include "btye/app.hpp"

#include <hidi/asio/io.hpp>
#include <kelcoro/job.hpp>
#include <kelcoro/latch.hpp>

#include <moko3/moko3.hpp>

#include <algorithm>
#include <memory>
#include <optional>
#include <stdexcept>
#include <thread>
#include <vector>

using btye::all_actors;
using btye::this_actor;

static btye::app_options actors(size_t n) {
  return btye::app_options{.actors_count = n};
}

using btye::yield;

TEST("main on actor 0, jumps between actors") {
  std::vector<size_t> visited;
  int code = btye::run(actors(3), [&]() -> dd::task<int> {
    visited.push_back(this_actor()->id());
    for (btye::actor& a : all_actors()) {
      (void)co_await btye::jump_on(a);
      visited.push_back(this_actor()->id());
    }
    co_return 7;
  });
  REQUIRE(code == 7);
  REQUIRE((visited == std::vector<size_t>{0, 0, 1, 2}));
  // вне run акторов нет
  REQUIRE(all_actors().empty() && !this_actor());
}

TEST("jump_on the current actor does not suspend, yield does") {
  std::vector<int> order;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    // задача в очереди актора 0
    [](std::vector<int>& order) -> dd::job {
      (void)co_await yield();
      order.push_back(2);
    }(order);
    (void)co_await btye::jump_on(all_actors()[0]);  // уже здесь: очередь не трогаем
    order.push_back(1);
    (void)co_await yield();  // задача из очереди выполнится раньше
    order.push_back(3);
    co_return 0;
  });
  REQUIRE(code == 0);
  REQUIRE((order == std::vector<int>{1, 2, 3}));
}

TEST("jump from a foreign thread") {
  size_t visited = 0;
  bool foreign_is_actor = true;
  // латч живёт дольше run: dd::latch нельзя разрушать сразу после wait, count_down может ещё выполняться
  std::optional<dd::latch> done;
  int code = btye::run(actors(2), [&]() -> dd::task<int> {
    done.emplace(1, all_actors()[0]);
    // чужому потоку акторов передаёт пользователь
    std::thread t([&, actors = all_actors()] {
      foreign_is_actor = this_actor() || !all_actors().empty();
      [](btye::actor& a, size_t& visited, dd::latch& done) -> dd::job {
        (void)co_await btye::jump_on(a);
        visited = this_actor()->id();
        done.count_down();
      }(actors[1], visited, *done);
    });
    co_await done->wait();
    t.join();
    co_return 0;
  });
  REQUIRE(code == 0 && visited == 1 && !foreign_is_actor);
}

TEST("stop reaches every actor") {
  // на каждом акторе задача ждёт остановки своего актора и после неё никуда не прыгает
  std::vector<int> seen(3, 0);
  int code = btye::run(actors(3), [&]() -> dd::task<int> {
    for (btye::actor& a : all_actors()) {
      [](btye::actor& a, int& seen) -> dd::job {
        (void)co_await btye::jump_on(a);
        seen = a.stopped() ? -1 : 1;
        while (!a.stopped())
          (void)co_await yield();
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
        (void)co_await btye::jump_on(a);
        btye::request_stop();
      }(*a);
    });
    while (!this_actor()->stopped())
      (void)co_await yield();
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
          (void)co_await btye::jump_on(a);
          actor_threads[a.id()] = std::this_thread::get_id();
        }
        co_return 0;
      },
      [&, marker = std::move(marker)] {
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
    (void)co_await btye::jump_on(all_actors()[1]);
    btye::request_stop();
    while (!this_actor()->stopped())
      (void)co_await yield();
    (void)co_await btye::jump_on(all_actors()[0]);
    co_return this_actor()->id() == 0 ? 9 : 1;
  });
  REQUIRE(code == 9);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
