# Сервер и клиенты

## Код из спеки

`btye_generate_cxx` - [svarg_generate_cxx](https://github.com/kelbon/svarg/blob/main/docs/openapi2cxx.md), в
котором обработчики возвращают `btye::task`, а цель подключает btye:

```cmake
btye_generate_cxx(
    TARGET    kv_api
    SPECS_DIR ${CMAKE_CURRENT_SOURCE_DIR}
    OUT_DIR   ${CMAKE_CURRENT_BINARY_DIR}/generated
    SPECS     kv.yaml
)
```

Что генерируется - [сервер](https://github.com/kelbon/svarg/blob/main/docs/cxx_server.md) и
[клиент](https://github.com/kelbon/svarg/blob/main/docs/cxx_client.md) svarg

## mt_server

`mt_server<S>` создаёт сервер `S` на каждом акторе. Слушает один актор (на котором вызван `start`), принятые
соединения раздаются всем, по умолчанию по кругу

```cpp
btye::mt_server<kv_store> server;
co_await server.start(ssl, hidi::h2server_options::hardened(), args...);
hidi::internet_address addr = server.listen_home({.addr = {boost::asio::ip::address_v4::any(), 8080}});
```

- конструктор `S` получает `(ssl, options, io, args...)`
- `listen_home(endpoint)` - на акторе `start`, `listen(endpoint)` - с любого. Возвращают адрес (с портом, если
  был 0)
- `shutdown(deadline)` - мягкая остановка. При остановке приложения сервер останавливается сам, в фазе `servers`
- свой выбор актора для соединения - `mt_server(choose)`, `choose() -> size_t` вызывается на акторе `start`

Серверы на акторах - это `per_actor<S>`: `S` может наследовать `per_actor_peer<S>` и ходить к серверам других
акторов через `container()`, а `per_actor_shutdown(S&)` вызывается при остановке после ответов на текущие
запросы

```cpp
struct my_server : api::server, btye::per_actor_peer<my_server> {
  config cfg;

  my_server(hidi::server_ssl_context_ptr ssl, hidi::h2server_options o, hidi::any_io_context io, config c)
      : api::server(std::move(ssl), std::move(o), std::move(io)), cfg(std::move(c)) {
  }
  ...
};
```

Note: сервер наследует `hidi::h2server`, и внутри обработчиков его имена (`run`, `stop`, `listen`...) закрывают
одноимённые типы и функции. Пишите их с пространством имён

## Клиенты

`btye::client_to(endpoint, params)` - клиент из пула текущего актора: одно соединение на адрес и параметры,
неиспользуемое закрывается через `idle_timeout`. Сгенерированный клиент строится поверх:

```cpp
api::client cli{btye::client_to(addr)};
api::getItem::result_t r = co_await cli.send(api::getItem{...}, hidi::deadline_after(5s));
```

- `client_params::ssl` - TLS, `src_addr` - адрес, с которого подключаться
- настройки пула - `app_options::connection_pool`
- пул останавливается в фазе `clients`, после неё `client_to` бросает исключение
