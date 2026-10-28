#include "pa4.h"

namespace multicore {

void count_event(MulticoreSystem& system,
                 const std::string& component,
                 const std::string& event,
                 uint64_t amount) {
  const std::string key = component + "." + event;

  if (event.size() >= 4 &&
      event.compare(event.size() - 4, 4, "_max") == 0) {
    system.counters.maximum(key, amount);
  } else {
    system.counters.add(key, amount);
  }
}

void sample_occupancy_and_stalls(MulticoreSystem& system) {
  const auto sample = [&](const std::string& prefix, uint64_t size) {
    system.counters.add(prefix + ".queue_occupancy_sum", size);
    system.counters.maximum(prefix + ".queue_occupancy_max", size);
    system.counters.add(prefix + ".samples");
  };

  const auto& state = system.current;

  for (std::size_t tile = 0;
       tile < state.network.buffers.size(); ++tile) {
    const std::string prefix = "noc." + std::to_string(tile);
    uint64_t total = 0;

    for (std::size_t q = 0;
         q < state.network.buffers[tile].size(); ++q) {
      const uint64_t size = state.network.buffers[tile][q].size();
      const std::string queue =
          prefix + ".queue." + std::to_string(q);

      system.counters.add(queue + ".occupancy_sum", size);
      system.counters.maximum(queue + ".occupancy_max", size);
      system.counters.add(queue + ".samples");
      total += size;
    }

    sample(prefix, total);
  }

  for (std::size_t m = 0; m < state.controllers.size(); ++m) {
    sample("mc." + std::to_string(m),
           state.controllers[m].queue.size());
  }

  for (std::size_t h = 0; h < state.llc.size(); ++h) {
    sample("llc." + std::to_string(h),
           state.llc[h].requests.size());
  }

  for (std::size_t core = 0; core < state.order.size(); ++core) {
    const std::string prefix = "order." + std::to_string(core);
    const auto& unit = state.order[core];

    sample(prefix, unit.stores.size());

    if (!state.cpu[core]) continue;

    const MemoryOperation operation = state.cpu[core]->operation;

    if (system.configuration.memory_model == "TSO" &&
        operation == MemoryOperation::Store &&
        unit.stores.size() >= system.configuration.store_entries) {
      system.counters.add(prefix + ".buffer_full_cycles");
    }

    if (operation == MemoryOperation::Fence &&
        (!unit.stores.empty() || unit.inflight)) {
      system.counters.add(prefix + ".fence_wait_cycles");
    }

    if (unit.inflight ||
        ((operation == MemoryOperation::Fence ||
          operation == MemoryOperation::Exchange) &&
         !unit.stores.empty())) {
      system.counters.add(prefix + ".ordering_stall_cycles");
    }
  }
}

}  // namespace multicore
