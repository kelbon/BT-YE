#pragma once

#include <string>
#include <unordered_map>

#include <btye/per_actor.hpp>

#include "kv/server.hpp"

// Ключи поделены между акторами: ключом владеет актор hash(key) % числа акторов. Сервер на каждом акторе
// хранит свои ключи, за чужими идёт к серверу владельца через container().invoke_on
struct kv_store : kv::server, btye::per_actor_peer<kv_store> {
  std::unordered_map<std::string, std::string> values;

  using kv::server::server;

  btye::task<hidi::http_response> handle(kv::getValue, hidi::http_request, hidi::request_context) override;
  btye::task<hidi::http_response> handle(kv::putValue, hidi::http_request, hidi::request_context) override;
  btye::task<hidi::http_response> handle(kv::deleteValue, hidi::http_request, hidi::request_context) override;
  btye::task<hidi::http_response> handle(kv::getSize, hidi::http_request, hidi::request_context) override;
};
