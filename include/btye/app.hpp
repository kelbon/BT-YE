#pragma once

#include <optional>

#include <anyany/anyany.hpp>
#include <kelcoro/task.hpp>
#include <hidi/utils/any_io_context.hpp>

#include "btye/actor.hpp"
#include "btye/client_pool.hpp"

namespace btye {

// вызывается на потоке каждого актора, никогда одновременно
// исключение из фабрики вызывает std::terminate
// когда контексты созданы на всех акторах, фабрика разрушается (до запуска `main`)
using actor_context_factory_t = aa::move_only_fn<hidi::any_io_context() noexcept>;

actor_context_factory_t default_actor_context_factory();

struct app_options {
  // 0 - std::thread::hardware_concurrency() (минимум 1)
  size_t actors_count = 0;
  // SIGINT/SIGTERM (Windows - Ctrl+C, Ctrl+Break) вызывают request_stop. Когда остановка началась, повторный
  // сигнал обрабатывается системой как обычно (обычно завершает процесс)
  bool handle_interrupt = true;
  // при падении (SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS) печатает сигнал и стек в stderr, затем процесс
  // падает как без обработчика. Обработчики ставятся на время run
  bool handle_crash = true;
  // пул клиентов по умолчанию на каждом акторе (client_to), останавливается в фазе clients. nullopt - без него
  std::optional<client_pool_options> clients = client_pool_options{};
  // TODO bind2cpu - биндить ли акторов к тредам
  // TODO? размер стека файбера/потока + ставить ли имена потокам (ну типа актор1 актор2)
  // TODO? опции сталл детектора, включать ли его, что считать сталлом мб ещё что-то
};

// TODO client pool?

/*
Одновременно может выполняться только один `run`

- вызывающий поток - актор 0
- `main` запускается на 0 акторе, когда контексты всех акторов созданы и фабрика разрушена
-  пока идёт `main` циклы всех акторов живы. Она может использовать jump_on
- если `main` выбрасывает исключение код возврата = 1
- контекст разрушается на потоке актора, когда закончились циклы всех акторов
- `run` возвращается, когда закончились циклы всех акторов, возвращает то что вернул `main`
*/
int run(app_options, aa::move_only_fn<dd::task<int>()> main,
        actor_context_factory_t factory = default_actor_context_factory());

/*
Останавливает акторов. Можно вызывать только с потока актора
Повторный вызов noop
Остановка: на всех акторах stopped() == true и отмена stop_source(), затем фазы (stop_phases_e) по порядку
*/
void request_stop() noexcept;

// Ждёт конца остановки: прошли все фазы на всех акторах. Возвращается на актор, где вызвана. Только на акторе.
// Не из обработчика фазы (никогда не дождётся)
// Так `main` переживает объекты, которые останавливаются в фазах:
//   co_await server.start(); ...; co_await btye::wait_stopped(); co_return 0;
dd::task<void> wait_stopped();

}  // namespace btye
