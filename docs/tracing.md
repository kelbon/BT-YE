# Трассировка задач

С трассировкой каждая `btye::task` помнит, кто её ждёт, где создана, где запущена и на каком `co_await` стоит.
Стеки задач можно напечатать в любой момент: из кода или из отладчика

Опция CMake `btye_TRACE_TASKS`:

- `DEBUG` (по умолчанию) - только в Debug сборке
- `ON` - всегда
- `OFF` - никогда, `btye::task` без накладных расходов

```cpp
std::string s = btye::make_trace(co_await dd::this_coro::context);  // стек текущей задачи
std::string all = btye::local_actor_trace();                        // все стеки текущего актора
```

Из отладчика (gdb, lldb, cppvsdbg): `btye_print_actor(id)` печатает все стеки актора `id`

Стек теста из [examples/kv](../examples/kv), ждущего ответ сервера (имена функций сокращены):

```
task stack (actor 0):
  #0 dd::task<...> svarg::cxx_support::noexport::receive_response(...) [ctx 0x556cc11815f0]
     at svarg/backend/cxx/support/operations.hpp:742
  #1 btye::task<kv::getValue::result_t> get(kv::client&, std::string) [ctx 0x556cc117f820]
     at examples/kv/test_store.cpp:23
  #2 btye::task<void> btye_test_26_26(moko3::section_info*) [ctx 0x556cc117e728]
     at examples/kv/test_store.cpp:30
  #3 btye::testing::run_test(...)::<lambda()> [ctx 0x556cc117e690]
     at btye/testing.hpp:27
  ^ root
```

Если к концу `run` остались незаконченные задачи, их стеки печатаются в stderr

Ограничения трассировки:

- задача переходит между акторами только через `jump_on` (и то, что на нём построено) и не может выходить на поток-не актор
