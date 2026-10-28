#include "pa4.h"

namespace multicore {

MemoryOrderDecision decide_memory_action(
    const MemoryRequest& request,
    const MemoryOrderUnit& order_unit,
    const SystemConfig& configuration) {
  // The framework normally calls us only when nothing is in flight.
  if (order_unit.inflight) {
    return {MemoryAction::Wait, 0};
  }

  switch (request.operation) {
    case MemoryOperation::Store:
      if (order_unit.stores.size() >= configuration.store_entries) {
        return {MemoryAction::Wait, 0};
      }
      return {MemoryAction::BufferStore, 0};

    case MemoryOperation::Load:
      // Search newest to oldest for the latest same-word store.
      for (auto it = order_unit.stores.rbegin();
           it != order_unit.stores.rend(); ++it) {
        if (it->address == request.address) {
          return {MemoryAction::Forward, it->value};
        }
      }
      return {MemoryAction::Issue, 0};

    case MemoryOperation::Fence:
      return {
          order_unit.stores.empty()
              ? MemoryAction::CompleteFence
              : MemoryAction::Wait,
          0};

    case MemoryOperation::Exchange:
      return {
          order_unit.stores.empty()
              ? MemoryAction::Issue
              : MemoryAction::Wait,
          0};
  }

  throw std::runtime_error("Unknown memory operation");
}

}  // namespace multicore
