#pragma once

#include <hidi/asio/io_impl.hpp>

namespace btye::noexport {

// asio часть контекста, если он hidi::asio_io или hidi::asio_ref_io (точный тип), иначе nullptr
inline hidi::asio_ref_io* asio_of(hidi::any_io_context& ctx) noexcept {
  if (hidi::asio_io* io = aa::any_cast<hidi::asio_io>(&ctx))
    return io;
  return aa::any_cast<hidi::asio_ref_io>(&ctx);
}

}  // namespace btye::noexport
