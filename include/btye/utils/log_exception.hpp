#pragma once

#include <exception>
#include <string_view>

namespace btye {

void log_exception(std::string_view where, std::exception_ptr e) noexcept;

}
