#include "pa4.h"
namespace multicore {

    MemoryOrderDecision decide_memory_action(
        const MemoryRequest& /* request */,
        const MemoryOrderUnit& /* order_unit */,
        const SystemConfig& /* configuration */) {

        // PA4-ORDER-01: Choose the TSO action. The framework moves requests between queues.
        not_implemented("PA4-ORDER-01");

    }

}

