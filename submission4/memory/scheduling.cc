#include "pa4.h"

namespace multicore {

std::optional<unsigned> select_memory_request(
    const std::vector<unsigned>& eligible_requests,
    const MemoryController& controller,
    Cycle cycle,
    const SystemConfig& configuration) {
  if (eligible_requests.empty()) {
    return std::nullopt;
  }

  // Earlier controller arrival wins.
  // Equal arrivals are ordered by queue index.
  const auto older = [&](unsigned a, unsigned b) {
    const Cycle arrival_a = controller.queue[a].controller_arrival;
    const Cycle arrival_b = controller.queue[b].controller_arrival;

    return arrival_a < arrival_b ||
           (arrival_a == arrival_b && a < b);
  };

  unsigned oldest = eligible_requests.front();

  for (unsigned index : eligible_requests) {
    if (older(index, oldest)) {
      oldest = index;
    }
  }

  if (configuration.scheduler != "custom") {
    return oldest;
  }

  // Custom policy: prefer open-row hits.
  // Aging prevents a stream of hits from starving older requests.
  constexpr Cycle kMaxWait = 32;
  const Cycle arrival =
      controller.queue[oldest].controller_arrival;

  if (cycle >= arrival && cycle - arrival >= kMaxWait) {
    return oldest;
  }

  std::optional<unsigned> selected;

  for (unsigned index : eligible_requests) {
    const Packet& packet = controller.queue[index];

    const uint64_t local_line =
        (packet.line_address / 64) / configuration.controllers;

    const unsigned bank = local_line % configuration.banks;
    const uint64_t row =
        local_line / configuration.banks / 16;

    const int open_row = controller.banks.at(bank).open_row;

    if (open_row < 0 ||
        static_cast<uint64_t>(open_row) != row) {
      continue;
    }

    if (!selected || older(index, *selected)) {
      selected = index;
    }
  }

  return selected ? selected : std::optional<unsigned>(oldest);
}

}  // namespace multicore
