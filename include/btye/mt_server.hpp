#pragma once

#include <concepts>
#include <exception>
#include <memory>

#include <anyany/anyany.hpp>
#include <hidi/h2server.hpp>
#include <kelcoro/road.hpp>

#include "btye/app.hpp"
#include "btye/invoke.hpp"
#include "btye/io.hpp"
#include "btye/per_actor.hpp"

namespace btye {

// аргумент per_actor::start: серверу на каждом акторе - local_io_ref() этого актора
inline constexpr per_actor_param local_io_ref_param{[] { return local_io_ref(); }};

// выбор актора для соединения по кругу среди всех
inline aa::move_only_fn<size_t() noexcept> round_robin() {
  return [n = size_t(0)]() mutable noexcept { return n++ % all_actors().size(); };
}

// Многопоточный сервер: экземпляр S на каждом акторе. Слушает только актор 0, принятые им соединения
// раздаются акторам, которых выбирает `choose` (по умолчанию все по кругу), и обрабатываются там.
// S - наследник hidi::h2server, конструктор S(ssl, options, контекст актора, args...).
// run/stop у S не вызывать: циклами управляет btye::run
// Остановка - одна на сервер, кто бы её ни начал: остановка приложения = shutdown
// деструктор = обрыв соединений Остальные ждут её конца
template <std::derived_from<hidi::h2server> S>
struct mt_server {
 private:
  // per_actor::stop вызвал бы S::stop, а h2server::stop блокирует поток до остановки сервера. Поэтому в
  // per_actor лежит обёртка без stop
  struct holder {
    S server;

    template <typename... Args>
    explicit holder(Args&&... args) : server(std::forward<Args>(args)...) {
    }
  };

  // живёт, пока жив mt_server или идёт его остановка
  struct impl {
    per_actor<holder> servers;
    // вызывается только на акторе 0, где идёт раздача
    aa::move_only_fn<size_t() noexcept> choose;
    // актор start: servers запускаются и останавливаются только с него
    size_t home = 0;
    // закрыта, пока идёт остановка: остальные ждут её конца
    dd::road stopping;

    explicit impl(aa::move_only_fn<size_t() noexcept> c) noexcept : choose(std::move(c)) {
    }
  };

  std::shared_ptr<impl> m;
  // остановка приложения останавливает сервер в фазе servers
  stop_subscription on_stop;

  // мягко (h2server::shutdown) или нет (terminate), затем разрушение серверов. Возвращается на актор `home`
  static dd::task<void> stop(std::shared_ptr<impl> m, bool graceful, hidi::deadline_t d) {
    co_await jump_on_actor(m->home);
    if (m->stopping.closed()) {  // останавливает другой: дождались его конца
      (void)co_await m->stopping.wait_open();
      co_return;
    }
    if (!m->servers.started())  // не запущен или уже остановлен
      co_return;
    m->stopping.close();
    std::exception_ptr err;
    try {
      if (graceful)
        co_await m->servers.invoke_on_all([d](holder& h) { return h.server.shutdown(d); });
      else
        co_await m->servers.invoke_on_all([](holder& h) { return h.server.terminate(); });
    } catch (...) {
      err = std::current_exception();
    }
    // и при ошибке: серверы разрушаются, ждущие просыпаются
    co_await m->servers.stop();
    m->stopping.open(get_actor(m->home));
    if (err)
      std::rethrow_exception(err);
  }

  // вызывается на акторе 0 для каждого принятого соединения: привязывает соединение к контексту выбранного
  // актора и отдаёт серверу на нём. Если привязать не удалось, соединение закрывается
  static void distribute(std::shared_ptr<impl> m, hidi::any_connection_t con) {
    size_t id = m->choose();
    try {
      hidi::any_io_context& io = get_actor(id).ioctx();
      aa::invoke<hidi::rebind_context_m>(io)(con, *&io);
    } catch (...) {
      return;
    }
    // задача встаёт в очередь актора раньше задач остановки (они начинаются позже на акторе 0), поэтому
    // сервер на нём ещё жив
    run_in_background([](std::shared_ptr<impl> m, size_t id, hidi::any_connection_t con) -> dd::task<void> {
      co_await jump_on_actor(id);
      m->servers.local().server.serve(std::move(con));
    }(std::move(m), id, std::move(con)));
  }

 public:
  // `choose` возвращает номер актора для очередного соединения, вызывается на акторе 0
  explicit mt_server(aa::move_only_fn<size_t() noexcept> choose = round_robin())
      : m(std::make_shared<impl>(std::move(choose))) {
    assert(m->choose);
  }

  mt_server(mt_server&&) = delete;
  void operator=(mt_server&&) = delete;

  // если сервер работает, он останавливается в фоне без ожидания запросов (terminate). Только на акторе
  ~mt_server() {
    on_stop = {};
    // если остановка уже идёт, её задача сама держит impl
    if (m->servers.started() && !m->stopping.closed())
      run_in_background(stop(std::move(m), /*graceful=*/false, hidi::deadline_t::never()));
  }

  // S(ssl, options, local_io_ref(), args...) на всех акторах, см. per_actor::start. `args` передаются по
  // ссылке и должны жить до завершения start (при co_await server.start(...) это так)
  // start и разрушение mt_server - на одном акторе, start - до остановки приложения и один раз
  template <typename... Args>
  dd::task<void> start(hidi::server_ssl_context_ptr ssl = nullptr, hidi::h2server_options options = {},
                       Args&&... args) {
    m->home = local_actor_ref().id();
    co_await m->servers.start(std::move(ssl), std::move(options), local_io_ref_param,
                              std::forward<Args>(args)...);
    on_stop = local_actor_ref().on_stop(
        stop_phases_e::servers, [m = m] { return stop(m, /*graceful=*/true, hidi::deadline_t::never()); });
  }

  // Слушает `e` на акторе 0, возвращает адрес, на котором слушает (нужен при порте 0). Только на акторе 0,
  // после start. Можно вызывать несколько раз с разными адресами
  hidi::internet_address listen0(hidi::server_endpoint e) {
    assert(local_actor_ref().id() == 0 && "listen0 must be called on the actor 0");
    S& s = m->servers.local().server;
    // weak: сервер, а с ним и колбек, живёт внутри impl
    s.set_accept_callback([w = std::weak_ptr(m)](hidi::any_connection_t con) {
      if (std::shared_ptr<impl> p = w.lock())
        distribute(std::move(p), std::move(con));
    });
    return s.listen(std::move(e));
  }

  // listen0 с любого актора: выполняется на акторе 0, адрес или исключение возвращаются на текущий актор
  dd::task<hidi::internet_address> listen(hidi::server_endpoint e) {
    co_return co_await invoke_on(0, as_task([&] { return listen0(e); }));
  }

  // мягкая остановка серверов на всех акторах (h2server::shutdown), затем их разрушение. С любого актора,
  // возвращается на него. Если остановка уже идёт - ждёт её конца, если сервер остановлен - ничего не делает
  dd::task<void> shutdown(hidi::deadline_t d = hidi::deadline_t::never()) {
    actor& from = local_actor_ref();
    co_await stop(m, /*graceful=*/true, d);
    co_await jump_on(from);
  }

  // экземпляр S текущего актора
  S& local() noexcept {
    return m->servers.local().server;
  }
};

}  // namespace btye
