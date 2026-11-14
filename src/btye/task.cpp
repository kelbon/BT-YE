#include "btye/task.hpp"

#include <cassert>
#include <cstdlib>

#include "btye/actor.hpp"

namespace btye {

noexport::trace_leaves& trace_ctx::leaves(actor& a) noexcept {
  return a.trace_leaves;
}

void trace_ctx::link_to(actor& a) noexcept {
  a.trace_leaves.push_back(*this);
  home = &a;
}

void trace_ctx::start() noexcept {
  if (home) {
    // запустили раньше (.start) и теперь ждут: наш стек уже на акторе
    if (owner)
      owner->unlink();
    return;
  }
  actor& a = local_actor_ref();
  if (owner && owner->is_linked()) {
    // занимаем место owner
    assert(owner->home == &a);
    owner->swap_nodes(*this);
    home = &a;
  } else {
    link_to(a);  // корень (владельца нет или он без трассировки) или второй и следующие дети when_all
  }
}

static void check_home(const trace_ctx& c) noexcept {
  assert(c.home == local_actor() && "traced task continued on another thread not through jump_on");
  if (c.home != local_actor())
    std::abort();
}

void trace_ctx::end() noexcept {
  if (!is_linked())
    return;
  check_home(*this);
  unlink();
}

void trace_ctx::on_await_location(dd::source_location l) noexcept {
  if (!is_linked())
    link_to(local_actor_ref());  // ребёнок закончился, задача снова лист
  else
    check_home(*this);
  awaiting = l;
}

namespace noexport {

bool traced_jump_on::await_ready() const noexcept {
  return local_actor() == &a;
}

void traced_jump_on::await_suspend(std::coroutine_handle<> h) {
  // этот co_await только что поставил задачу в список её актора (on_await_location)
  assert(c.is_linked() && c.home == local_actor());
  c.unlink();
  task = h;
  a.attach(this);
}

void traced_jump_on::await_resume() noexcept {
  if (!c.is_linked())  // await_ready: уже была здесь и не уходила из списка
    c.link_to(a);
}

}  // namespace noexport

}  // namespace btye
