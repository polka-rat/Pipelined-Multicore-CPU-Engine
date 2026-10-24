#include "pa4.h"
namespace multicore {

    std::optional<unsigned> select_memory_request(
        const std::vector<unsigned>& eligible_requests/* eligible_requests */,
        const MemoryController& controller/* controller */,
        Cycle cycle/* cycle */,
        const SystemConfig& configuration/* configuration */) {

        if (eligible_requests.empty()) return std::nullopt;

        unsigned oldest = eligible_requests.front();
        if (configuration.scheduler != "custom" || controller.banks.empty()) {
            // find the oldest req
            for (unsigned i: eligible_requests) {
                const Packet& packetOldest = controller.queue[oldest];
                const Packet& packetI = controller.queue[i];
                if (packetOldest.controller_arrival > packetI.controller_arrival)
                oldest = i;
                else if (packetOldest.controller_arrival == packetI.controller_arrival) {
                    if (packetOldest.id > packetI.id)
                    oldest = i;
                    else if (packetOldest.id == packetI.id) {
                        if (oldest > i)
                        oldest = i;
                    }
                }
            }
            return oldest;
        }
            
        // Custom policy: First Ready First Come First Served
        
        
        // PA4-MC-01: Implement FCFS and your policy. Return an eligible queue index.
        not_implemented("PA4-MC-01");

    }

}

