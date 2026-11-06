#pragma once

#include <anyany/anyany.hpp>
#include <kelcoro/task.hpp>
#include <hidi/utils/any_io_context.hpp>

namespace btye {

using actor_context_factory_t = aa::move_only_fn<hidi::any_io_context()>;

// TODO опции может принимать
actor_context_factory_t default_actor_context_factory();
// TODO хелперы для создания клиента и сервера - они должны ссылаться на контекст ioctx на котором созданы
// (причём если это asio то напрямую чтобы не городить обёртки)
// видимо нужны обёртки самих клиента и сервера. И mt_server тоже. И уже там логика с accept
struct app_options {
  // TODO
  size_t actors_count = 1;  // TODO default number actors функцией (+- все цпу)
  bool bind2cpu = false;    // биндить ли акторов к тредам
  // TODO какой-то выбор io, видимо через фактори
  // TODO? размер стека файбера/потока + ставить ли имена потокам (ну типа актор1 актор2)
  // TODO? опции сталл детектора, включать ли его, что считать сталлом мб ещё что-то
};

struct actor_ref {
  // TODO здесь именно jump_on (attach), номер, тредайди, ioctx, actor-local переменные(безопасно вставляемые
  // и удаляемые не ломая ссылки.. Может быть разрешать вставлять с одного потока так что всё будет легче)?.
  // Например для реализации sharded без тредлокалов, функции проверяют что на правильном треде находятся
  // (которые ограничены тредом). Мб отдельно schedule таску тредсейф
};
// TODO client pool?
//
struct actors {
  // TODO набор тредов и латч, jump_on_shard(i)

  // request_stop() - ставит атомик
  // join_actors() - ждёт латч после выставленного стопа (запрашивает если ещё не запрошено)
  // TODO разрешать ли request_stop и тд на всех акторах или только на одном главном?
  // TODO accept на главном акторе только делать и распределять
  // теоретически можно сделать обёртку вокруг передаваемого any_io_context и в нём make_acceptor подменять
  // так что я буду собирать на одном потоке и затем распределять. Под мьютексом или ещё как неособо важно
};

struct app {
  // TODO
  // инициализировать акторов, запустить функцию так чтобы она могла делать invoke_on_others и так
  // далее, то есть чётко определить где она запускается

  int run(aa::move_only_fn<dd::task<int>()>, actor_context_factory_t = default_actor_context_factory());
};

}  // namespace btye
