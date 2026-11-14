#pragma once

#include <optional>

#include <anyany/anyany.hpp>
#include <kelcoro/task.hpp>
#include <hidi/utils/any_io_context.hpp>

#include "btye/actor.hpp"
#include "btye/client_pool.hpp"
#include "btye/offload_pool.hpp"

namespace btye {

namespace noexport {

// санитайзер ставит свои обработчики падений, его отчёт подробнее нашего
inline constexpr bool sanitizer_handles_crash =
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
    true;
#elif defined(__has_feature)
  #if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) || \
      __has_feature(memory_sanitizer) || __has_feature(hwaddress_sanitizer)
    true;
  #else
    false;
  #endif
#else
    false;
#endif

}  // namespace noexport

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
  // падает как без обработчика. Обработчики ставятся на время run. По умолчанию выключено под ASan, TSan,
  // MSan, HWASan: у них свои обработчики
  bool handle_crash = !noexport::sanitizer_handles_crash;
  // настройки клиент пула на каждом акторе (nullopt - без него)
  std::optional<client_pool_options> connection_pool = client_pool_options{};
  // пул для блокирующих задач. nullopt = не создавать
  std::optional<offload_pool_options> offload_pool = offload_pool_options{};
  // даёт имена потокам - actor_1, _2 и так далее
  // главному потоку имя не меняет. Имена в Блокирующем пуле отдельная настройка
  bool name_threads = false;
  // TODO? опции сталл детектора, включать ли его, что считать сталлом мб ещё что-то
};

/*
Одновременно может выполняться только один `run`

- вызывающий поток - актор 0
- `main` запускается на 0 акторе, когда контексты всех акторов созданы и фабрика разрушена
-  пока идёт `main` циклы всех акторов живы. Она может использовать jump_on
- если `main` выбрасывает исключение код возврата = 1
- контекст разрушается на потоке актора, когда закончились циклы всех акторов
- `run` возвращается, когда закончились циклы всех акторов, возвращает то что вернул `main`
*/
int run(app_options, aa::move_only_fn<task<int>()> main,
        actor_context_factory_t factory = default_actor_context_factory());

/*
Останавливает акторов. Можно вызывать только с потока актора
Повторный вызов noop
Остановка: на всех акторах stopped() == true и отмена stop_source(), затем фазы (stop_phases_e) по порядку
*/
void request_stop() noexcept;

// ждёт полного завершения текущего run. Нельзя вызывать из on_stop акторов (дедлок)
//   co_await server.start(); ...; co_await btye::wait_stopped(); co_return 0;
task<void> wait_stopped();

// можно вызывать пока работает `run`
// запускает задачу 'f' на пуле для блокирующих задач (если он есть, иначе исключение)
template <typename F>
task<std::invoke_result_t<F&>> offload(F f) {
  return local_actor_ref().offpool().offload(std::move(f));
}

}  // namespace btye
