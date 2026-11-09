#include "btye/offload_pool.hpp"

#include <latch>

#include "btye/utils/thread_tune.hpp"

namespace btye {

offload_pool::offload_pool(offload_pool_options opts) : tp(std::max<size_t>(1, opts.threads_count)) {
  if (!opts.name_threads)
    return;
  std::span workers = tp.workers_range();
  size_t n = workers.size();
  std::latch all_started(n);
  std::latch all_ended(n);
  // задачи забивают пул (он нерасширяемый) и так каждая попадает на свой поток
  // имена в каком-то случайном порядке
  for (size_t i = 0; i < n; ++i) {
    dd::schedule_to(tp, [i, &all_started, &all_ended] {
      all_started.arrive_and_wait();
      btye::set_current_thread_name(std::format("blocking_{}", i));
      all_ended.count_down();
    });
  }
  all_ended.wait();
}

}  // namespace btye
