#include "test_utils.hpp"

#include "btye/invoke.hpp"
#include "btye/utils/thread_tune.hpp"

#include <string>
#include <thread>
#include <vector>

#ifdef __linux__
  #include <pthread.h>
  #include <sched.h>
#endif

using btye::local_actor;
using btye::test::actors;

TEST("available_cpus is not empty") {
  REQUIRE(!btye::available_cpus().empty());
}

#ifdef __linux__

static std::string current_thread_name() {
  char buf[16] = {};
  pthread_getname_np(pthread_self(), buf, sizeof(buf));
  return buf;
}

TEST("set_current_thread_name: long name is cut to 15 bytes by UTF-8 character boundary") {
  std::vector<std::string> names;
  std::thread([&] {
    REQUIRE(btye::set_current_thread_name("short"));
    names.push_back(current_thread_name());
    REQUIRE(btye::set_current_thread_name("0123456789abcdefgh"));
    names.push_back(current_thread_name());
    // 8 символов по 2 байта: 15-й байт - середина 8-го символа
    REQUIRE(btye::set_current_thread_name("абвгдежз"));
    names.push_back(current_thread_name());
  }).join();
  REQUIRE((names == std::vector<std::string>{"short", "0123456789abcde", "абвгдеж"}));
}

TEST("actor threads are named actor_N, the caller keeps its name") {
  btye::set_current_thread_name("test_main");
  std::vector<std::string> names(3);
  int code = btye::run(btye::app_options{.actors_count = 3, .name_threads = true}, [&]() -> btye::task<int> {
    co_await btye::invoke_on_all([&] { names[local_actor()->id()] = current_thread_name(); });
    co_return 0;
  });
  REQUIRE(code == 0);
  REQUIRE((names == std::vector<std::string>{"test_main", "actor_1", "actor_2"}));
}

#endif

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
