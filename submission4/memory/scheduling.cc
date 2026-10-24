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
        std::optional<unsigned> selected;
        for (unsigned i: eligible_requests) {
            const Packet& packet = controller.queue[i];
            const uint64_t line = packet.line_address / 64;
            const unsigned bank = line % (controller.banks.size());
            const uint64_t row = line / (controller.banks.size());
            if (controller.banks[bank].open_row < 0) continue;
            if (controller.banks[bank].open_row != row) continue;
            if (!selected) selected = i;
            else {
                const Packet& packetSelected = controller.queue[*selected];
                if (packetSelected.controller_arrival > packet.controller_arrival)
                selected = i;
                else if (packetSelected.controller_arrival == packet.controller_arrival) {
                    if (packetSelected.id > packet.id)
                    selected = i;
                    else if (packetSelected.id == packet.id) {
                        if (*selected > i)
                        selected = i;
                    }
                }
            }
        }
        
        // PA4-MC-01: Implement FCFS and your policy. Return an eligible queue index.
        not_implemented("PA4-MC-01");

    }

}

