#pragma once

#include <chrono>
#include <memory>
#include <optional>
#include <unordered_map>

#include <anyany/anyany.hpp>
#include <hidi/h2client.hpp>
#include <kelcoro/gate.hpp>
#include <kelcoro/task.hpp>
#include <svarg/backend/cxx/support/any_client.hpp>

namespace btye {

// нетипизированный клиент
using raw_client = svarg::cxx_support::any_client;

struct client_pool_options {
  hidi::h2client_options eachclient_options;
  // неиспользуемый клиент удаляется через idle_timeout (в среднем)
  hidi::duration_t idle_timeout = std::chrono::seconds(30);
};

struct client_params {
  // nullptr = клиенты без tls
  hidi::client_ssl_context_ptr ssl = nullptr;
  std::optional<hidi::internet_address> src_addr = std::nullopt;

  bool operator==(const client_params& o) const noexcept {
    return ssl.p == o.ssl.p && src_addr == o.src_addr;
  }
};

// co_await stop() обязателен перед деструктором
struct local_client_pool {
 private:
  struct key {
    hidi::endpoint dest;
    client_params params;

    bool operator==(const key&) const = default;
  };
  struct key_hash {
    size_t operator()(const key&) const noexcept;
  };
  struct entry {
    std::shared_ptr<hidi::h2client> client;
    bool used = true;
  };

  client_pool_options options;
  std::unordered_map<key, entry, key_hash> clients;
  hidi::any_timer gc_timer;
  bool stopped = false;
  // удаляемые сборкой клиенты, пока идёт их graceful_stop
  dd::gate retiring;

  void collect() noexcept;

 public:
  explicit local_client_pool(client_pool_options = {});

  local_client_pool(local_client_pool&&) = delete;
  void operator=(local_client_pool&&) = delete;

  ~local_client_pool();

  // выданный клиент жив пока жив any_client или идёт его запрос
  // выданный клиент соединяется при первом запросе (и переподключается при запросе после обрыва)
  // создаёт не более одного клиента на одинаковые (dest + params)
  // после stop бросает исключение
  [[nodiscard]] raw_client get_client_to(hidi::endpoint dest, client_params params = {});

  // запрещает выдачу новых клиентов и запросы в существующих
  // ждёт запросы которые уже идут. Повторный вызов noop
  dd::task<void> stop();

  // сколько клиентов в пуле
  [[nodiscard]] size_t size() const noexcept {
    return clients.size();
  }
};

// берёт ссылку на клиента из пула текущего актора
[[nodiscard]] raw_client client_to(hidi::endpoint dest, client_params = {});

}  // namespace btye
