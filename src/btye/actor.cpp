#include "btye/actor.hpp"

#include <kelcoro/job.hpp>

#include "btye/utils/log_exception.hpp"

namespace btye {

dd::job do_background_task(dd::gate::holder h, dd::task<void> t) {
  actor& home = local_actor_ref();
  try {
    co_await std::move(t);
  } catch (...) {
    log_exception("background task", std::current_exception());
  }
  co_await jump_on(home);
}

void actor::run_background(dd::task<void> t) {
  (void)do_background_task(background_gate.hold(), std::move(t));
}

stop_subscription actor::on_stop(stop_phases_e p, aa::move_only_fn<dd::task<void>()> fn) {
  assert(local_actor() == this && "on_stop must be called on the actor's thread");
  if (is_stopped)
    throw std::runtime_error("on_stop called after the stop started");
  stop_subscription sub(std::move(fn));  // TODO здесь надёга на nrvo, вероятно надо лучше сделать
  stop_handlers[size_t(p)].push_back(sub);
  return sub;
}

void run_in_background(dd::task<void> t) {
  local_actor_ref().run_background(std::move(t));
}

}  // namespace btye
