#include "store.hpp"

#include <functional>
#include <optional>

#include <btye/invoke.hpp>

using svarg::cxx_support::make_response;

static size_t owner(std::string_view key) {
  return std::hash<std::string_view>{}(key) % btye::all_actors().size();
}

btye::task<hidi::http_response> kv_store::handle(kv::getValue op, hidi::http_request, hidi::request_context) {
  size_t id = owner(op.path.key);
  std::optional<std::string> value =
      co_await container().invoke_on(id, [key = std::move(op.path.key)](kv_store& s) -> std::optional<std::string> {
        auto it = s.values.find(key);
        return it == s.values.end() ? std::nullopt : std::optional(it->second);
      });
  if (!value)
    co_return make_response(kv::getValue::response_404{});
  co_return make_response(kv::getValue::response_200{.body = std::move(*value)});
}

btye::task<hidi::http_response> kv_store::handle(kv::putValue op, hidi::http_request, hidi::request_context) {
  size_t id = owner(op.path.key);
  co_await container().invoke_on(id, [key = std::move(op.path.key), value = std::move(op.body)](kv_store& s) {
    s.values.insert_or_assign(key, value);
  });
  co_return make_response(kv::putValue::response_204{});
}

btye::task<hidi::http_response> kv_store::handle(kv::deleteValue op, hidi::http_request, hidi::request_context) {
  size_t id = owner(op.path.key);
  bool erased = co_await container().invoke_on(id, [key = std::move(op.path.key)](kv_store& s) {
    return s.values.erase(key) != 0;
  });
  if (!erased)
    co_return make_response(kv::deleteValue::response_404{});
  co_return make_response(kv::deleteValue::response_204{});
}

btye::task<hidi::http_response> kv_store::handle(kv::getSize, hidi::http_request, hidi::request_context) {
  // на каждом акторе - число его ключей, затем сумма
  size_t count = co_await btye::map_reduce_on_all([&c = container()] { return c.local().values.size(); },
                                                  std::plus<>{});
  kv::getSize::response_200 rsp;
  rsp.body.count = count;
  co_return make_response(rsp);
}
