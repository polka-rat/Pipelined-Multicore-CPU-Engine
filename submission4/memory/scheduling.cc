#include "pa4.h"
namespace multicore {

    std::optional<unsigned> select_memory_request(
        const std::vector<unsigned>& /* eligible_requests */,
        const MemoryController& /* controller */,
        Cycle /* cycle */,
        const SystemConfig& /* configuration */) {

        // PA4-MC-01: Implement FCFS and your policy. Return an eligible queue index.
        not_implemented("PA4-MC-01");

    }

}

