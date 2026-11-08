#pragma once

#include <cassert>
#include <utility>

#include <boost/intrusive/list.hpp>

namespace btye::noexport {

// Колбек `F` в интрузивном списке источника `Owner` (только он создаёт и вызывает). Отписывается при
// разрушении. Пустая (по умолчанию или после перемещения из неё) ни на что не подписана
template <typename F, typename Owner>
struct subscription
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>> {
 private:
  F fn;

  friend Owner;

  explicit subscription(F f) noexcept : fn(std::move(f)) {
    assert(fn && "subscription to an empty callback");
  }

 public:
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
};

// при разрушении списка оставшиеся подписки отвязываются
template <typename S>
using subscriptions_list = boost::intrusive::list<S, boost::intrusive::constant_time_size<false>>;

}  // namespace btye::noexport
