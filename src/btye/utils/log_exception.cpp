#include "btye/utils/log_exception.hpp"

#include <print>

namespace btye {

void log_exception(std::string_view where, std::exception_ptr e) noexcept {
  try {
    std::rethrow_exception(std::move(e));
  } catch (const std::exception& e) {
    std::println(stderr, "btye: exception from {}: {}", where, e.what());
  } catch (...) {
    std::println(stderr, "btye: unknown exception from {}", where);
  }
}

}  // namespace btye
