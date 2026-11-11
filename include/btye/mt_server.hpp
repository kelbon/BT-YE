#pragma once

#include <concepts>
#include <exception>
#include <memory>

#include <anyany/anyany.hpp>
#include <hidi/h2server.hpp>
#include <kelcoro/road.hpp>

#include "btye/invoke.hpp"
#include "btye/io.hpp"
#include "btye/per_actor.hpp"

namespace btye {

inline constexpr per_actor_param local_io_ref_param{[] { return local_io_ref(); }};

inline aa::move_only_fn<size_t() noexcept> round_robin() {
  return [n = size_t(local_actor()->id())]() mutable noexcept { return n++ % all_actors().size(); };
}

// создаёт S на каждом акторе
// listen делает один и раздаёт акторам соединения, крутятся сервера на своих акторах
template <std::derived_from<hidi::h2server> S>
struct mt_server {
 private:
  // живёт, пока жив mt_server или идёт его остановка
  struct impl {
    per_actor<S> servers;
    // вызывается только на home, где идёт раздача
    aa::move_only_fn<size_t() noexcept> choose;
    // актор start: servers запускаются и останавливаются только с него
    size_t home = size_t(-1);
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
    if (m->stopping.closed()) {
      (void)co_await m->stopping.wait_open();
      co_return;
    }
    if (!m->servers.started())  // не запущен или уже остановлен
      co_return;
    m->stopping.close();
    on_scope_exit {
      m->stopping.open(get_actor(m->home));
    };
    std::exception_ptr err;
    try {
      if (graceful)
        co_await m->servers.invoke_on_all([d](S& s) { return s.shutdown(d); });
      else
        co_await m->servers.invoke_on_all([](S& s) { return s.terminate(); });
    } catch (...) {
      err = std::current_exception();
    }
    // и при ошибке: серверы разрушаются, ждущие просыпаются
    co_await m->servers.stop();
    if (err)
      BTYE_RETURN_EXCEPTION(std::move(err));
  }

  // вызывается на home для каждого принятого соединения: привязывает соединение к контексту выбранного
  // актора и отдаёт серверу на нём. Если привязать не удалось, соединение закрывается
  static void distribute(std::shared_ptr<impl> m, hidi::any_connection_t con) {
    size_t id = m->choose();
    try {
      hidi::any_io_context& io = get_actor(id).ioctx();
      aa::invoke<hidi::rebind_context_m>(io)(con, *&io);
    } catch (...) {
      return;
    }
    run_in_background([](std::shared_ptr<impl> m, size_t id, hidi::any_connection_t con) -> dd::task<void> {
      co_await jump_on_actor(id);
      m->servers.local().serve(std::move(con));
    }(std::move(m), id, std::move(con)));
  }

 public:
  // `choose` вызывается всегда на home акторе
  explicit mt_server(aa::move_only_fn<size_t() noexcept> choose = round_robin())
      : m(std::make_shared<impl>(std::move(choose))) {
    assert(m->choose);
  }

  mt_server(mt_server&&) = delete;
  void operator=(mt_server&&) = delete;

  // если сервер работает, он останавливается в фоне без ожидания запросов (terminate). Только на акторе
  ~mt_server() {
    assert((!m->servers.started() || local_actor_ref().id() == m->home) &&
           "mt_server is destroyed on another actor than start");
    on_stop = {};
    // если остановка уже идёт, её задача сама держит impl
    if (m->servers.started() && !m->stopping.closed())
      run_in_background(stop(std::move(m), /*graceful=*/false, hidi::deadline_t::never()));
  }

  // вызывает конструктор каждого с (ssl, options, local_io_ref(), args...)
  // назначает home актор для сервера (там делается listen_home / accept)
  // больше одного успешного .start звать нельзя
  template <typename... Args>
  dd::task<void> start(hidi::server_ssl_context_ptr ssl = nullptr,
                       hidi::h2server_options options = hidi::h2server_options::hardened(), Args&&... args) {
    assert(m->home == size_t(-1) && "start already called");
    co_await m->servers.start(std::move(ssl), std::move(options), local_io_ref_param,
                              std::forward<Args>(args)...);
    m->home = local_actor_ref().id();
    m->servers.local().set_accept_callback([w = std::weak_ptr(m)](hidi::any_connection_t con) {
      if (std::shared_ptr<impl> p = w.lock())
        distribute(std::move(p), std::move(con));
    });
    on_stop = local_actor_ref().on_stop(
        stop_phases_e::servers, [m = m] { return stop(m, /*graceful=*/true, hidi::deadline_t::never()); });
  }

  // можно вызывать только на home после start
  // возвращает выданный адрес
  hidi::internet_address listen_home(hidi::server_endpoint e) {
    assert(m->home != size_t(-1) && "start not called");
    assert(local_actor_ref().id() == m->home && "listen_home must be called from home actor");

    return m->servers.local().listen(std::move(e));
  }

  // listen_home, но с любого актора
  dd::task<hidi::internet_address> listen(hidi::server_endpoint e) {
    co_return co_await invoke_on(m->home, [&] { return listen_home(e); });
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
    return m->servers.local();
  }

  // назначается при первом вызове listen_home, до этого size_t(-1)
  size_t home() const noexcept {
    return m->home;
  }
};

}  // namespace btye
