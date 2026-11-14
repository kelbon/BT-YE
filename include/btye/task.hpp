#pragma once

#include <coroutine>
#include <type_traits>

#include <boost/intrusive/list.hpp>
#include <kelcoro/executor_interface.hpp>
#include <kelcoro/task.hpp>

#ifndef BTYE_TRACE_TASKS
  #define BTYE_TRACE_TASKS 0
#endif

namespace btye {

struct actor;
struct trace_ctx;

namespace noexport {

template <typename P>
concept traced_promise =
    requires(P& p) { requires std::is_same_v<std::remove_cvref_t<decltype(p.ctx)>, trace_ctx>; };

using leaf_hook =
    boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::auto_unlink>>;

using trace_leaves = boost::intrusive::list<trace_ctx, boost::intrusive::base_hook<leaf_hook>,
                                            boost::intrusive::constant_time_size<false>>;

struct jump_on_actor_t;

// jump_on для traced_task: переносит её в список актора, на который она переезжает
struct traced_jump_on : dd::task_node {
  actor& a;
  trace_ctx& c;

  traced_jump_on(actor& a, trace_ctx& c) noexcept : a(a), c(c) {
  }

  bool await_ready() const noexcept;
  void await_suspend(std::coroutine_handle<> h);
  void await_resume() noexcept;
};

}  // namespace noexport

// traced_task может создаваться или исполняться только на акторах
struct trace_ctx : dd::null_context, noexport::leaf_hook {
  trace_ctx* owner = nullptr;
  // nullptr до старта, меняется при переезде (jump)
  actor* home = nullptr;
  // из какой функции создали корутину
  dd::source_location created;
  // где был первый .resume
  dd::source_location started;
  // последний co_await (возможно идёт сейчас, а возможно уже нет)
  dd::source_location awaiting;

  trace_ctx() = default;
  trace_ctx(trace_ctx&&) = delete;
  void operator=(trace_ctx&&) = delete;

  ~trace_ctx() {
    end();  // handle.destroy(), on_end не вызывался
  }

  static noexport::trace_leaves& leaves(actor& a) noexcept;

  void on_create_location(dd::source_location l) noexcept {
    created = l;
  }
  void on_start_location(dd::source_location l) noexcept {
    started = l;
  }
  void on_await_location(dd::source_location l) noexcept;

  template <typename T>
  decltype(auto) transform(T&& v) noexcept {
    if constexpr (std::is_same_v<std::remove_cvref_t<T>, noexport::jump_on_actor_t>)
      return noexport::traced_jump_on(v.a, *this);
    else
      return std::forward<T>(v);
  }

  template <typename P1, typename P2>
  static void on_owner_setted(std::coroutine_handle<P1> owner, std::coroutine_handle<P2> me) noexcept {
    if constexpr (noexport::traced_promise<P1>)
      me.promise().ctx.owner = &owner.promise().ctx;
  }
  template <typename P>
  static void on_start(std::coroutine_handle<P> h) noexcept {
    h.promise().ctx.start();
  }
  template <typename P>
  static void on_end(std::coroutine_handle<P> h) noexcept {
    h.promise().ctx.end();
  }

 private:
  friend struct noexport::traced_jump_on;

  void link_to(actor& a) noexcept;
  void start() noexcept;
  void end() noexcept;
};

// всегда должна создаваться/работать на акторе
template <typename T>
using traced_task = dd::task<T, trace_ctx>;

using task_context = std::conditional_t<bool(BTYE_TRACE_TASKS), trace_ctx, dd::null_context>;

// задача btye: везде в API btye и для кода пользователя
template <typename T>
using task = dd::task<T, task_context>;

}  // namespace btye
