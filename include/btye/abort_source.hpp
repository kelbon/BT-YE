#pragma once

#include <anyany/anyany.hpp>

#include "btye/subscription.hpp"

namespace btye {

// Отмена на одном акторе (используется только с его потока). request_abort вызывает колбеки всех подписок,
// после этого aborted() навсегда true
struct abort_source {
  // Подписка на отмену, см. subscribe. Отписывается при разрушении
  using subscription = noexport::subscription<aa::move_only_fn<void() noexcept>>;

 private:
  noexport::subscriptions_list<subscription> subscriptions;
  bool is_aborted = false;

 public:
  abort_source() = default;
  abort_source(abort_source&&) = delete;
  void operator=(abort_source&&) = delete;
  // оставшиеся подписки отвязываются без вызова колбеков
  ~abort_source() = default;

  [[nodiscard]] bool aborted() const noexcept {
    return is_aborted;
  }

  // `cb` вызывается один раз при request_abort(), пока подписка жива. Если уже отменено - сразу, и
  // возвращается пустая подписка
  [[nodiscard]] subscription subscribe(aa::move_only_fn<void() noexcept> cb) {
    if (is_aborted) {
      cb();
      return subscription();
    }
    subscription s(std::move(cb));
    subscriptions.push_back(s);
    return s;
  }

  // колбеки вызываются в порядке подписки. Колбек может разрушить любую подписку. Повторный вызов ничего не
  // делает
  void request_abort() noexcept {
    is_aborted = true;
    while (!subscriptions.empty()) {
      subscription& s = subscriptions.front();
      subscriptions.pop_front();
      s.fn();
    }
  }
};

}  // namespace btye
