#include "btye/sleep.hpp"

#include <hidi/asio/awaiters.hpp>

#include "btye/actor.hpp"

namespace btye {

dd::task<bool> sleep(hidi::any_timer& t, hidi::duration_t d) {
  actor& a = local_actor_ref();
  if (a.stopped())
    co_return false;
  hidi::io_error_code ec;
  co_await hidi::net.sleep(t, d, ec);
  co_return !a.stopped();
}

}  // namespace btye
