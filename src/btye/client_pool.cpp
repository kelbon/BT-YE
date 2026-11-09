#include "btye/client_pool.hpp"
#include "btye/actor.hpp"

#include <boost/container_hash/hash.hpp>
#include <kelcoro/job.hpp>

#include <functional>
#include <stdexcept>
#include <variant>
#include <vector>

namespace btye {

namespace {

// any_client пула: держит клиента до конца запроса
struct pooled_client {
  std::shared_ptr<hidi::h2client> client;

  static dd::task<int> send(std::shared_ptr<hidi::h2client> c, hidi::client_request_context& ctx,
                            hidi::on_header_fn_ptr on_header, hidi::on_data_part_fn_ptr on_data_part,
                            hidi::http_request req, hidi::deadline_t deadline) {
    co_return co_await c->send_request(ctx, on_header, on_data_part, std::move(req), deadline);
  }

  dd::task<int> send_request(hidi::client_request_context& ctx, hidi::on_header_fn_ptr on_header,
                             hidi::on_data_part_fn_ptr on_data_part, hidi::http_request req,
                             hidi::deadline_t deadline) {
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
  if (k.params.src) {
    boost::hash_combine(h, std::hash<hidi::ip_addr>{}(k.params.src->address()));
    boost::hash_combine(h, k.params.src->port());
  }
  return h;
}

local_client_pool::local_client_pool(aa::move_only_fn<hidi::any_io_context()> make_io_,
                                     client_pool_options o)
    : make_io(std::move(make_io_)), options(std::move(o)), gc_timer(make_io().create_timer()) {
  gc_timer.set_callback([this](bool canceled) {
    if (!canceled)
      collect();
  });
  gc_timer.arm_periodic(options.idle_timeout / 2);
}

local_client_pool::~local_client_pool() {
  assert(stopped && clients.empty() && "local_client_pool must be stopped before destruction");
}

svarg::cxx_support::any_client local_client_pool::get(hidi::endpoint dest, client_params params) {
  if (stopped)
    throw std::runtime_error("btye: client pool is stopped");
  auto [it, inserted] = clients.try_emplace(key{std::move(dest), std::move(params)});
  entry& e = it->second;
  if (inserted) {
    try {
      const key& k = it->first;
      e.client = std::make_shared<hidi::h2client>(k.dest, k.params.ssl, options.client, make_io());
      e.client->set_local(k.params.src);
    } catch (...) {
      clients.erase(it);
      throw;
    }
  }
  e.used = true;
  return svarg::cxx_support::any_client(pooled_client{e.client});
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

dd::task<void> local_client_pool::stop() {
  if (stopped)
    co_return;
  stopped = true;
  gc_timer.cancel();
  std::vector<std::shared_ptr<hidi::h2client>> all;
  for (auto& [_, e] : clients)
    all.push_back(std::move(e.client));
  clients.clear();
  for (std::shared_ptr<hidi::h2client>& c : all)
    co_await c->graceful_stop();
  co_await retiring.close();
}

svarg::cxx_support::any_client client_to(hidi::endpoint dest, client_params params) {
  actor& a = local_actor_ref();
  if (!a.clients)
    throw std::logic_error("btye: default client pool is disabled (app_options::clients)");
  return a.clients->get(std::move(dest), std::move(params));
}

}  // namespace btye
