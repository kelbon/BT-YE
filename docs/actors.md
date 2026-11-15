# Акторы

`btye::run(options, foo)` запускает акторы - потоки, у каждого свой цикл событий. Вызывающий поток - актор 0,
на нём идёт `foo`. `run` возвращается, когда остановка закончилась, с кодом `foo`

```cpp
btye::task<int> foo() {
  ...
  co_await btye::wait_stopped();
  co_return 0;
}

int main() {
  return btye::run({.actors_count = 4}, foo);
}
```

`app_options`:

- `actors_count` - 0 по числу ядер
- `handle_interrupt` - SIGINT/SIGTERM (Ctrl+C) вызывают `request_stop`
- `handle_crash` - при падении печатает сигнал и стек
- `connection_pool` - пул клиентов на каждом акторе (для `client_to`), nullopt - без пула
- `offload_pool` - пул потоков для блокирующих вызовов (для `offload`), nullopt - без пула
- `name_threads` - потоки акторов называются actor_1, actor_2...

Корутины btye - `btye::task<T>`. Данные актора используются только на его потоке, синхронизация не нужна.
К чужим данным - через переход на их актор

## Переходы между акторами

- `co_await btye::jump_on(actor)`, `jump_on_actor(id)` - продолжить на другом акторе
- `co_await btye::invoke_on(id, fn)` - `fn()` (или задача) на акторе `id`, результат или исключение - на текущем
- `invoke_on_all(fn)`, `invoke_on_others(fn)` - на всех акторах параллельно, у каждого своя копия `fn`
- `map_reduce_on_all(map, reduce)` - `map()` на каждом, результаты сворачиваются деревом, `reduce` достаточно
  ассоциативной
- `btye::when_any(tasks...)` - как `dd::when_any`, но продолжает на том же акторе
- `yield()` - пропустить вперёд остальные задачи актора

`local_actor()` - актор текущего потока, `all_actors()`, `get_actor(id)`, `actor::id()`

```cpp
// сумма размеров кешей всех акторов (caches - per_actor<cache>, см. ниже)
size_t count = co_await btye::map_reduce_on_all([&] { return caches.local().size(); }, std::plus<>{});
```

## Объект на каждом акторе

`per_actor<T>` создаёт `T` на каждом акторе (на его потоке и в его памяти):

```cpp
btye::per_actor<cache> caches;
co_await caches.start(args...);  // T(args...) на каждом
caches.local().get(key);         // экземпляр текущего актора
co_await caches.invoke_on(2, [](cache& c) { c.clear(); });
co_await caches.stop();          // обязательно перед разрушением
```

- `per_actor_shutdown(T&) -> task<void>` (ищется по ADL) - асинхронная остановка экземпляра в `stop`
- база `per_actor_peer<T>` даёт `container()` - доступ к экземплярам других акторов
- аргумент `std::ref(other)` (`per_actor<U>`) передаётся как `U&` текущего актора, `per_actor_param{fn}` - как
  `fn()` на каждом

## Переменные актора

`actor::locals()` - словарь ключ -> значение любого типа, у каждого актора свой. Разрушается в конце `run`,
после фоновых задач

```cpp
btye::actor& a = btye::local_actor_ref();
cache& c = a.get_or_emplace_local<cache>("my.cache", args...);  // создаётся при первом обращении
cache* p = a.get_local<cache>("my.cache");                       // nullptr - нет
```

## Фоновые задачи

`run_in_background(task)` запускает задачу на текущем акторе, никто её не ждёт. Исключение пишется в лог. Пока
идёт фоновая задача, акторы не завершаются

## Блокирующие вызовы

`co_await btye::offload(fn)` выполняет `fn()` в пуле потоков, результат - на исходном акторе. Для того, что
нельзя делать на акторе: чтение файлов, тяжёлые вычисления, блокирующие библиотеки

## Ожидание и отмена

- `co_await btye::sleep(d)` - `true` время вышло, `false` отменено остановкой актора
- `sleep(d, abort_source)` - то же, ещё отменяется `abort_source::request_abort()`
- `actor::stop_source()` отменяется в начале остановки

## Остановка

`request_stop()` или Ctrl+C: на всех акторах `stopped() == true` и отменяется `stop_source()`, затем фазы
`stop_phases_e` по порядку, каждая на всех акторах:

- `first`
- `servers` - серверы перестают принимать запросы и дожидаются текущих
- `clients` - пулы клиентов
- `last`

Свой обработчик фазы - `actor::on_stop(phase, handler)`, вызывается пока жива возвращённая подписка

`wait_stopped()` возвращается после всех фаз. Фоновые задачи могут идти и после неё: если они пользуются тем, что
`main` разрушает, перед разрушением нужен `stop_background()`

```cpp
btye::per_actor<cache> caches;  // им пользуются фоновые задачи
...
co_await btye::wait_stopped();
co_await btye::stop_background();
co_await caches.stop();
co_return 0;
```
