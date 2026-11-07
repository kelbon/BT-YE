#include "btye/io.hpp"

#include <memory>

namespace btye {

namespace noexport {

// any_io_context, который пересылает всё контексту актора, не владея им (local_io_ref)
struct actor_io {
  hidi::any_io_context* ctx;

  hidi::any_timer create_timer() {
    return ctx->create_timer();
  }
  void attach(dd::task_node* n) {
    ctx->attach(n);
  }
  bool poll_one() {
    return ctx->poll_one();
  }
  size_t poll() {
    return ctx->poll();
  }
  size_t run() {
    return ctx->run();
  }
  void stop() {
    ctx->stop();
  }
  bool stopped() {
    return ctx->stopped();
  }
  void restart() {
    ctx->restart();
  }
  bool running_in_this_thread() {
    return ctx->running_in_this_thread();
  }
  void start_task() {
    ctx->start_task();
  }
  void end_task() {
    ctx->end_task();
  }
  dd::task<hidi::any_connection_t> create_connection_client(hidi::remote_and_local_endpoints e,
                                                            hidi::deadline_t d,
                                                            hidi::tcp_connection_options o,
                                                            hidi::client_ssl_context_ptr ssl) {
    return ctx->create_connection_client(std::move(e), d, std::move(o), std::move(ssl));
  }
  hidi::any_acceptor create_acceptor(hidi::internet_address a, bool reuse, hidi::tcp_connection_options o,
                                     hidi::server_ssl_context_ptr ssl) {
    return ctx->create_acceptor(a, reuse, std::move(o), std::move(ssl));
  }
  // `other` - тоже actor_io: соединение привязывается к контексту, на который он ссылается
  static void rebind_context(hidi::any_connection_t& con, hidi::any_io_context_ref other) {
    hidi::any_io_context& to = *aa::any_cast<actor_io&>(other).ctx;
    aa::invoke<hidi::rebind_context_m>(to)(con, *&to);
  }
};

}  // namespace noexport

hidi::any_io_context local_io_ref() {
  return noexport::actor_io{std::addressof(local_actor_ref().ioctx())};
}

hidi::any_timer create_timer() {
  return local_actor_ref().ioctx().create_timer();
}

}  // namespace btye
