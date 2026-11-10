#include "btye/actor.hpp"

namespace btye {

stop_subscription actor::on_stop(stop_phases_e p, aa::move_only_fn<dd::task<void>()> fn) {
  assert(local_actor() == this && "on_stop must be called on the actor's thread");
  if (is_stopped)
    throw std::runtime_error("on_stop called after the stop started");
  stop_subscription sub(std::move(fn));
  stop_handlers[size_t(p)].push_back(sub);
  return sub;
}

}  // namespace btye
