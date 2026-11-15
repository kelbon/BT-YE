#include <print>

#include <btye/app.hpp>
#include <btye/mt_server.hpp>

#include "store.hpp"

int main() {
  // актор на каждое ядро, Ctrl+C останавливает приложение
  return btye::run({}, []() -> btye::task<int> {
    btye::mt_server<kv_store> server;
    co_await server.start();
    server.listen_home({.addr = {boost::asio::ip::address_v4::loopback(), 8080}});
    std::println("kv: 127.0.0.1:8080");
    co_await btye::wait_stopped();
    co_return 0;
  });
}
