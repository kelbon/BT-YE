#include "test_utils.hpp"

#include "btye/invoke.hpp"

#include <atomic>
#include <string_view>

using btye::local_actor;
using btye::yield;
using btye::test::actors;

static btye::actor_locals& locals() {
  return btye::local_actor_ref().locals();
}

TEST("actor locals: own map on each actor, empty at the start of every run") {
  for (int r = 0; r < 2; ++r) {
    std::atomic_bool empty_at_start = true;
    int code = btye::run(actors(3), [&]() -> dd::task<int> {
      co_await btye::invoke_on_all([&] {
        if (!locals().empty())
          empty_at_start = false;
        locals().try_emplace("id", local_actor()->id());
      });
      co_await btye::invoke_on_all([&] {
        btye::actor& a = btye::local_actor_ref();
        auto it = a.locals().find(std::string_view("id"));
        REQUIRE(it != a.locals().end());
        auto* id = a.get_local<decltype(local_actor()->id())>("id");
        REQUIRE(id && id == aa::any_cast<size_t>(&it->second));
        REQUIRE(*id == a.id());
        REQUIRE(aa::any_cast<int>(&it->second) == nullptr);
        REQUIRE(a.get_local<float>("id") == nullptr);
      });
      co_return 0;
    });
    REQUIRE(code == 0 && empty_at_start);
  }
}

struct counted {
  btye::actor* home = local_actor();
  std::atomic_size_t& destroyed;
  std::atomic_bool& ok;

  counted(std::atomic_size_t& d, std::atomic_bool& ok) noexcept : destroyed(d), ok(ok) {
  }
  counted(counted&&) = delete;

  ~counted() {
    if (local_actor() != home)
      ok = false;
    ++destroyed;
  }
};

// неперемещаемый, асинхронная остановка через on_stop
struct probe {
  btye::actor* home = local_actor();
  size_t actors_count;
  std::atomic_size_t& last_phases_done;
  std::atomic_size_t& background_done;
  std::atomic_size_t& destroyed;
  std::atomic_size_t& late_destroyed;
  std::atomic_bool& ok;
  btye::stop_subscription on_last;

  probe(size_t n, std::atomic_size_t& phases, std::atomic_size_t& bg, std::atomic_size_t& destroyed,
        std::atomic_size_t& late, std::atomic_bool& ok)
      : actors_count(n),
        last_phases_done(phases),
        background_done(bg),
        destroyed(destroyed),
        late_destroyed(late),
        ok(ok) {
    on_last = home->on_stop(btye::stop_phases_e::last, [&p = last_phases_done]() -> dd::task<void> {
      co_await yield();
      ++p;
    });
  }
  probe(probe&&) = delete;

  ~probe() {
    // на своём акторе, после фаз остановки и фоновых задач всех акторов, контекст ещё жив
    if (local_actor() != home || last_phases_done != actors_count || background_done != actors_count ||
        !home->ioctx().has_value())
      ok = false;
    // мапа уже пустая, положенное сюда разрушится следующим кругом
    if (!home->locals().empty())
      ok = false;
    home->locals().try_emplace("late", std::in_place_type<counted>, late_destroyed, ok);
    ++destroyed;
  }
};

TEST("actor locals: destroyed on own actor after stop phases and background tasks") {
  constexpr size_t n = 3;
  std::atomic_size_t last_phases_done = 0, background_done = 0, destroyed = 0, late_destroyed = 0;
  std::atomic_bool ok = true;
  int code = btye::run(actors(n), [&]() -> dd::task<int> {
    co_await btye::invoke_on_all([&] {
      locals().try_emplace("probe", std::in_place_type<probe>, n, last_phases_done, background_done,
                           destroyed, late_destroyed, ok);
      // фоновая задача переживает main и фазы остановки
      btye::run_in_background([](std::atomic_size_t& done) -> dd::task<void> {
        while (!local_actor()->stopped())
          co_await yield();
        for (int i = 0; i < 10; ++i)
          co_await yield();
        ++done;
      }(background_done));
    });
    co_return 0;
  });
  REQUIRE(code == 0 && ok);
  REQUIRE(destroyed == n && late_destroyed == n);
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
