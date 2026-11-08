#include "test_utils.hpp"

#include "btye/per_actor.hpp"

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using btye::local_actor;

// где создан, остановлен и разрушен каждый экземпляр
struct journal {
  std::vector<size_t> created_on, stopped_on, destroyed_on;
  std::vector<std::thread::id> threads;

  explicit journal(size_t n) : created_on(n, -1), stopped_on(n, -1), destroyed_on(n, -1), threads(n) {
  }
};

struct counter {
  journal* j;
  size_t id;
  int value = 0;

  counter(journal* j, int start) : j(j), id(local_actor()->id()), value(start) {
    j->created_on[id] = local_actor()->id();
    j->threads[id] = std::this_thread::get_id();
  }
  ~counter() {
    j->destroyed_on[id] = local_actor()->id();
  }
  dd::task<void> stop() {
    co_await btye::yield();
    j->stopped_on[id] = local_actor()->id();
  }
};

APP_TEST("per_actor: start, local, invoke, stop", .actors_count = 3) {
  journal j(3);
  std::vector<std::thread::id> actor_threads(3);
  btye::per_actor<counter> c;
  co_await btye::jump_on(btye::get_actor(1));
  co_await c.start(&j, 5);
  REQUIRE(local_actor()->id() == 1 && c.local().value == 5 && c.local().id == 1);
  co_await c.invoke_on_all([](counter& x) -> dd::task<void> {
    x.value += int(x.id);
    co_return;
  });
  int on2 = co_await c.invoke_on(2, [](counter& x) -> dd::task<int> { co_return x.value; });
  REQUIRE(on2 == 7 && local_actor()->id() == 1);
  co_await c.invoke_on_others([](counter& x) -> dd::task<void> {
    co_await btye::yield();
    x.value = 100;
  });
  REQUIRE(c.local().value == 6);
  REQUIRE(co_await c.invoke_on(0, [](counter& x) -> dd::task<int> { co_return x.value; }) == 100);
  co_await btye::invoke_on_all([&]() -> dd::task<void> {
    actor_threads[local_actor()->id()] = std::this_thread::get_id();
    co_return;
  });
  co_await c.stop();
  REQUIRE(local_actor()->id() == 1);
  std::vector<size_t> ids{0, 1, 2};
  REQUIRE(j.created_on == ids && j.stopped_on == ids && j.destroyed_on == ids);
  REQUIRE(j.threads == actor_threads);
}

struct no_stop {
  int x = 0;
};

struct fails_on_2 {
  std::vector<size_t>* destroyed;

  explicit fails_on_2(std::vector<size_t>* d) : destroyed(d) {
    if (local_actor()->id() == 2)
      throw std::runtime_error("no 2");
  }
  ~fails_on_2() {
    (*destroyed)[local_actor()->id()] = 1;
  }
};

APP_TEST("per_actor: without stop, exception in start", .actors_count = 3) {
  std::vector<size_t> destroyed(3, 0);
  std::string error;
  btye::per_actor<no_stop> a;
  co_await a.start();
  a.local().x = 1;
  co_await a.stop();

  btye::per_actor<fails_on_2> f;
  try {
    co_await f.start(&destroyed);
  } catch (std::runtime_error& e) {
    error = e.what();
  }
  // созданные разрушены, per_actor пуст и его можно разрушить
  REQUIRE(error == "no 2");
  REQUIRE((destroyed == std::vector<size_t>{1, 1, 0}));
}

struct base_service {
  size_t id = local_actor()->id();
};

// зависит от другого per_actor: локальный экземпляр, весь per_actor, значение для своего актора
struct dependent {
  base_service& local_base;
  btye::per_actor<base_service>& all_bases;
  size_t param;
  std::string name;

  dependent(base_service& b, btye::per_actor<base_service>& all, size_t p, std::string n)
      : local_base(b), all_bases(all), param(p), name(std::move(n)) {
  }
};

// обращается к соседям через container()
struct peer : btye::per_actor_peer<peer> {
  size_t value = local_actor()->id() * 100;

  dd::task<size_t> next_value() {
    size_t next = (local_actor()->id() + 1) % btye::all_actors().size();
    co_return co_await container().invoke_on(next, [](peer& p) -> dd::task<size_t> { co_return p.value; });
  }
};

APP_TEST("per_actor: std::ref, per_actor_param, per_actor_peer", .actors_count = 3) {
  std::vector<int> ok(3, 0);
  std::vector<size_t> next(3, 0);
  btye::per_actor<base_service> bases;
  co_await bases.start();
  btye::per_actor<dependent> deps;
  co_await deps.start(std::ref(bases), std::ref(bases),
                      btye::per_actor_param{[] { return local_actor()->id() * 10; }}, std::string("svc"));
  co_await deps.invoke_on_all([&](dependent& d) -> dd::task<void> {
    size_t id = local_actor()->id();
    ok[id] = d.local_base.id == id && &d.all_bases == &bases && d.param == id * 10 && d.name == "svc";
    co_return;
  });

  btye::per_actor<peer> peers;
  co_await peers.start();
  co_await peers.invoke_on_all(
      [&](peer& p) -> dd::task<void> { next[local_actor()->id()] = co_await p.next_value(); });
  co_await peers.stop();
  co_await deps.stop();
  co_await bases.stop();
  REQUIRE((ok == std::vector<int>(3, 1)));
  REQUIRE((next == std::vector<size_t>{100, 200, 0}));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
