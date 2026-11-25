# btye

Фреймворк для HTTP/2 сервисов на C++23: поток на ядро (актор), корутины, сервер и клиент из OpenAPI спеки.
Корутины - [kelcoro](https://github.com/kelbon/kelcoro), HTTP/2 - [hidi](https://github.com/kelbon/hidi),
OpenAPI -> C++ - [svarg](https://github.com/kelbon/svarg)

## Пример

Хранилище ключ - значение, ключи поделены между акторами. Полностью - [examples/kv](examples/kv)

`kv.yaml` (часть):

```yaml
paths:
  /keys/{key}:
    parameters:
      - {name: key, in: path, required: true, schema: {type: string}}
    get:
      operationId: getValue
      responses:
        "200":
          description: value
          content:
            text/plain:
              schema: {type: string}
        "404":
          description: no such key
```

Сервер:

```cpp
using svarg::cxx_support::make_response;

// сервер на каждом акторе хранит свои ключи, за чужими идёт к серверу владельца
struct kv_store : kv::server, btye::per_actor_peer<kv_store> {
  std::unordered_map<std::string, std::string> values;

  using kv::server::server;

  btye::task<hidi::http_response> handle(kv::getValue op, hidi::http_request, hidi::request_context) override {
    size_t id = std::hash<std::string>{}(op.path.key) % btye::all_actors().size();
    std::optional<std::string> value =
        co_await container().invoke_on(id, [key = op.path.key](kv_store& s) -> std::optional<std::string> {
          auto it = s.values.find(key);
          return it == s.values.end() ? std::nullopt : std::optional(it->second);
        });
    if (!value)
      co_return make_response(kv::getValue::response_404{});
    co_return make_response(kv::getValue::response_200{.body = std::move(*value)});
  }
  ...
};

int main() {
  return btye::run({}, []() -> btye::task<int> {
    btye::mt_server<kv_store> server;
    co_await server.start();
    server.listen_home({.addr = {boost::asio::ip::address_v4::loopback(), 8080}});
    co_await btye::wait_stopped();  // Ctrl+C
    co_return 0;
  });
}
```

Тест:

```cpp
BTYE_TEST("kv: put, get", .actors_count = 3) {
  btye::mt_server<kv_store> server;
  kv::client cli{btye::client_to(co_await btye::testing::start_local(server))};
  kv::putValue put;
  put.path.key = "a";
  put.body = "1";
  (void)co_await cli.send(put, hidi::deadline_after(5s));
  kv::getValue get;
  get.path.key = "a";
  kv::getValue::result_t got = co_await cli.send(get, hidi::deadline_after(5s));
  auto* found = got.get_if_200();
  REQUIRE(found && found->body == "1");
}
```

`CMakeLists.txt`:

```cmake
set(CMAKE_CXX_STANDARD 23)
include(cmake/CPM.cmake)
CPMAddPackage(NAME btye GITHUB_REPOSITORY kelbon/BT-YE GIT_TAG <коммит>)

# как svarg_generate_cxx, обработчики возвращают btye::task
btye_generate_cxx(
    TARGET    kv_api
    SPECS_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    OUT_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS     kv.yaml
)
add_executable(kv main.cpp store.cpp)
target_link_libraries(kv PRIVATE kv_api)
```

## Документация

- [Акторы](docs/actors.md) - запуск, переходы между акторами, per_actor, фоновые задачи, остановка
- [Сервер и клиенты](docs/server.md) - генерация из спеки, mt_server, client_to
- [Тесты](docs/testing.md) - BTYE_TEST, wait_until
- [Трассировка задач](docs/tracing.md) - стеки корутин в отладке
