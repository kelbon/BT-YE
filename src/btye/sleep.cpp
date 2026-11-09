#include "btye/sleep.hpp"

#include <hidi/asio/awaiters.hpp>

#include "btye/actor.hpp"
#include "btye/io.hpp"

namespace btye {

static dd::task<bool> do_sleep(hidi::duration_t d, abort_source* as) {
  actor& a = local_actor_ref();
  auto aborted = [&] { return a.stopped() || (as && as->aborted()); };
  if (aborted())
    co_return false;
  hidi::any_timer t = create_timer();
  abort_source::subscription on_stop = a.stop_source().subscribe([&t] noexcept { t.cancel(); });
  abort_source::subscription on_abort;
  if (as)
    on_abort = as->subscribe([&t] noexcept { t.cancel(); });
  hidi::io_error_code ec;
  co_await hidi::net.sleep(t, d, ec);
  co_return !aborted();
}

dd::task<bool> sleep(hidi::duration_t d) {
  return do_sleep(d, nullptr);
}

dd::task<bool> sleep(hidi::duration_t d, abort_source& as) {
  return do_sleep(d, &as);
}

}  // namespace btye
