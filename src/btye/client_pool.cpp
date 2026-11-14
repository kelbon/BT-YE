#include "btye/client_pool.hpp"
#include "btye/actor.hpp"
#include "btye/io.hpp"

#include <boost/container_hash/hash.hpp>
#include <kelcoro/job.hpp>

#include <functional>
#include <stdexcept>
#include <variant>

namespace btye {

namespace {

// any_client пула: держит клиента до конца запроса
struct pooled_client {
  std::shared_ptr<hidi::h2client> client;
  dbg_actor_ref home;

  static dd::task<int> send(std::shared_ptr<hidi::h2client> c, hidi::client_request_context& ctx,
                            hidi::on_header_fn_ptr on_header, hidi::on_data_part_fn_ptr on_data_part,
                            hidi::http_request req, hidi::deadline_t deadline) {
    co_return co_await c->send_request(ctx, on_header, on_data_part, std::move(req), deadline);
  }

  dd::task<int> send_request(hidi::client_request_context& ctx, hidi::on_header_fn_ptr on_header,
                             hidi::on_data_part_fn_ptr on_data_part, hidi::http_request req,
                             hidi::deadline_t deadline) {
    home.check();
    return send(client, ctx, on_header, on_data_part, std::move(req), deadline);
  }
};

// клиента, удалённого сборкой, разрушает последняя ссылка после graceful_stop
dd::job retire(dd::gate::holder, std::shared_ptr<hidi::h2client> c) {
  co_await c->graceful_stop();
}

}  // namespace

size_t local_client_pool::key_hash::operator()(const key& k) const noexcept {
  size_t h = 0;
  std::visit([&](const auto& a) { boost::hash_combine(h, std::hash<std::remove_cvref_t<decltype(a)>>{}(a)); },
             k.dest.addr);
  boost::hash_combine(h, k.dest.port);
  boost::hash_combine(h, k.params.ssl.p.get());
  if (k.params.src_addr) {
    boost::hash_combine(h, std::hash<hidi::ip_addr>{}(k.params.src_addr->address()));
    boost::hash_combine(h, k.params.src_addr->port());
  }
  return h;
}

local_client_pool::local_client_pool(client_pool_options o)
    : options(std::move(o)), gc_timer(create_timer()) {
  gc_timer.set_callback([this](bool canceled) {
    if (!canceled)
      collect();
  });
  // так в среднем будет выходить idle_timeout
  gc_timer.arm_periodic(options.idle_timeout * 2 / 3);
}

local_client_pool::~local_client_pool() {
  assert(stopped && clients.empty() && "local_client_pool must be stopped before destruction");
}

raw_client local_client_pool::get_client_to(hidi::endpoint dest, client_params params) {
  if (stopped)
    throw std::runtime_error("btye: client pool is stopped");
  auto [it, inserted] = clients.try_emplace(key{std::move(dest), std::move(params)});
  entry& e = it->second;
  if (inserted) {
    const key& k = it->first;
    on_scope_failure(eraseclient) {
      clients.erase(it);
    };
    e.client =
        std::make_shared<hidi::h2client>(k.dest, k.params.ssl, options.eachclient_options, local_io_ref());
    e.client->set_local(k.params.src_addr);
    eraseclient.no_longer_needed();
  }
  e.used = true;
  return raw_client(pooled_client{e.client});
}

void local_client_pool::collect() noexcept {
  if (stopped)
    return;
  for (auto it = clients.begin(); it != clients.end();) {
    entry& e = it->second;
    // кроме пула никто не держит: нет ни выданных any_client, ни запросов
    if (e.client.use_count() == 1) {
      if (!e.used && !e.client->connecting()) {
        retire(retiring.hold(), std::move(e.client));
        it = clients.erase(it);
        continue;
      }
      e.used = false;
    }
    ++it;
  }
}

task<void> local_client_pool::stop() {
  if (stopped)
    co_return;
  stopped = true;
  gc_timer.cancel();
  auto c = std::move(clients);
  for (auto& [_, v] : c)
    co_await v.client->graceful_stop();
  co_await retiring.close();
}

raw_client client_to(hidi::endpoint dest, client_params params) {
  actor& a = local_actor_ref();
  if (!a.clients)
    throw std::logic_error("btye: no client pool available");
  return a.clients->get_client_to(std::move(dest), std::move(params));
}

}  // namespace btye
