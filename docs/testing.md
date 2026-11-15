# Тесты

`btye/testing.hpp` поверх [moko3](https://github.com/kelbon/moko3)

```cpp
#include <moko3/moko3.hpp>
#include <btye/testing.hpp>

BTYE_TEST("kv: put, get", .actors_count = 3) {
  btye::mt_server<kv_store> server;
  hidi::internet_address addr = co_await btye::testing::start_local(server);
  kv::client cli{btye::client_to(addr)};
  ...
  REQUIRE(co_await btye::testing::wait_until([&] { return done; }, 5s));
}

REGISTER_TEST_LISTENER(moko3::gtest_listener);
MOKO3_MAIN;
```

- `BTYE_TEST(NAME, поля app_options...)` - `TEST` moko3, тело - `main` приложения. Исключение из тела (в том
  числе `REQUIRE`) роняет тест. В теле работают `SECTION` и `GENERATE`: каждый случай - новый `run`
- `start_local(server, args...)` - `start(args...)` и `listen` на 127.0.0.1 со свободным портом, возвращает адрес
- `wait_until(pred, timeout, period = 1ms)` - вызывает `pred()` (`bool` или `task<bool>`, например запрос), пока
  не вернёт `true`. `false` - не дождались
- `run_test(options, body)` - то же, что `BTYE_TEST`, без moko3
- `localhost()` - 127.0.0.1 с портом 0, для своего `listen`

```cpp
// ждём, пока прогон закончится
REQUIRE(co_await btye::testing::wait_until([&]() -> btye::task<bool> {
  auto r = co_await cli.send(api::getRun{...}, hidi::deadline_after(5s));
  co_return r.body.status == "done";
}, 10s));
```
