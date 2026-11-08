#pragma once

#include <concepts>

#include <anyany/anyany.hpp>
#include <hidi/h2server.hpp>

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
// run/stop у S не вызывать: циклами управляет btye::run, а остановка - shutdown
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
  per_actor<holder> servers;
  // вызывается только на акторе 0, где идёт раздача
  aa::move_only_fn<size_t() noexcept> choose;

  // вызывается на акторе 0 для каждого принятого соединения: привязывает соединение к контексту выбранного
  // актора и отдаёт серверу на нём. Если привязать не удалось, соединение закрывается
  void distribute(hidi::any_connection_t con) {
    size_t id = choose();
    try {
      hidi::any_io_context& io = get_actor(id).ioctx();
      aa::invoke<hidi::rebind_context_m>(io)(con, *&io);
    } catch (...) {
      return;
    }
    run_in_background([](mt_server& self, size_t id, hidi::any_connection_t con) -> dd::task<void> {
      co_await jump_on_actor(id);
      self.servers.local().server.serve(std::move(con));
    }(*this, id, std::move(con)));
  }

 public:
  // `choose` возвращает номер актора для очередного соединения, вызывается на акторе 0
  explicit mt_server(aa::move_only_fn<size_t() noexcept> choose = round_robin()) : choose(std::move(choose)) {
    assert(this->choose);
  }

  // S(ssl, options, local_io_ref(), args...) на всех акторах, см. per_actor::start. `args` передаются по
  // ссылке и должны жить до завершения start (при co_await server.start(...) это так)
  template <typename... Args>
  dd::task<void> start(hidi::server_ssl_context_ptr ssl = nullptr, hidi::h2server_options options = {},
                       Args&&... args) {
    co_await servers.start(std::move(ssl), std::move(options), local_io_ref_param,
                           std::forward<Args>(args)...);
  }

  // Слушает `e` на акторе 0, возвращает адрес, на котором слушает (нужен при порте 0). Только на акторе 0,
  // после start. Можно вызывать несколько раз с разными адресами
  hidi::internet_address listen0(hidi::server_endpoint e) {
    assert(local_actor_ref().id() == 0 && "listen0 must be called on the actor 0");
    S& s = servers.local().server;
    s.set_accept_callback([this](hidi::any_connection_t con) { distribute(std::move(con)); });
    return s.listen(std::move(e));
  }

  // listen0 с любого актора: выполняется на акторе 0, адрес или исключение возвращаются на текущий актор
  dd::task<hidi::internet_address> listen(hidi::server_endpoint e) {
    co_return co_await invoke_on(0, as_task([&] { return listen0(e); }));
  }

  // мягкая остановка серверов на всех акторах (h2server::shutdown), затем их разрушение
  dd::task<void> shutdown(hidi::deadline_t d = hidi::deadline_t::never()) {
    co_await servers.invoke_on_all([d](holder& h) { return h.server.shutdown(d); });
    co_await servers.stop();
  }

  // экземпляр S текущего актора
  S& local() noexcept {
    return servers.local().server;
  }
};

}  // namespace btye
