#pragma once

#include <cassert>
#include <utility>

#include <boost/intrusive/list.hpp>

namespace btye::noexport {

template <typename F>
struct subscription
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
  F fn;

  subscription() = default;

  // встаёт в список источника на место `o`
  subscription(subscription&& o) noexcept : fn(std::move(o.fn)) {
    swap_nodes(o);
  }
  subscription& operator=(subscription&& o) noexcept {
    if (this != &o) {
      unlink();
      fn = std::move(o.fn);
      swap_nodes(o);
    }
    return *this;
  }

  explicit subscription(F f) noexcept : fn(std::move(f)) {
    assert(fn && "subscription to an empty callback");
  }
};

// при разрушении списка оставшиеся подписки отвязываются
template <typename S>
using subscriptions_list = boost::intrusive::list<S, boost::intrusive::constant_time_size<false>>;

}  // namespace btye::noexport
