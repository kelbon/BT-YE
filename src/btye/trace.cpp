#include "btye/trace.hpp"

#include <cstdio>
#include <format>
#include <span>

#include "btye/actor.hpp"

namespace btye {

static void print_strerr(const std::string& s) noexcept {
  std::fputs(s.c_str(), stderr);
  std::fflush(stderr);
}

static std::string actor_trace(actor& a) {
  noexport::trace_leaves& leaves = trace_ctx::leaves(a);
  std::string s = std::format("btye: actor {}, task stacks: {}\n", a.id(), leaves.size());
  for (trace_ctx& leaf : leaves)
    s += make_trace(leaf);
  return s;
}

dd::generator<const trace_ctx&> trace_up(const trace_ctx& c) {
  for (const trace_ctx* t = &c; t; t = t->owner)
    co_yield *t;
}

std::string make_trace(const trace_ctx& c) {
  std::string s = c.home ? std::format("task stack (actor {}):\n", c.home->id()) : "task stack:\n";
  size_t i = 0;
  for (const trace_ctx& t : trace_up(c)) {
    s += std::format("  #{} {} [ctx {}]\n", i++, t.created.line() ? t.created.function_name() : "?",
                     static_cast<const void*>(&t));
    // место известно после первого co_await задачи
    if (t.awaiting.line())
      s += std::format("     at {}:{}\n", t.awaiting.file_name(), t.awaiting.line());
  }
  const trace_ctx* root = &c;
  while (root->owner)
    root = root->owner;
  if (root->started.line())
    return s + std::format("  ^ root, started at {}:{}\n", root->started.file_name(), root->started.line());
  return s + "  ^ root\n";
}

std::string local_actor_trace() {
  return actor_trace(local_actor_ref());
}

}  // namespace btye

extern "C" void btye_print_actor(size_t id) {
  std::span actors = btye::all_actors();
  if (id < actors.size())
    btye::print_strerr(btye::actor_trace(actors[id]));
  else
    btye::print_strerr(std::format("btye: no actor {}, actors count: {}\n", id, actors.size()));
}
