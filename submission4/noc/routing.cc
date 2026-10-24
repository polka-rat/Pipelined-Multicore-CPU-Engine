#include "pa4.h"
namespace multicore {

    int select_output_port(
        const RoutingContext& context /* context */,
        const SystemConfig& configuration /* configuration */) {

        // PA4-NOC-01: Implement XY/YX routing. Return Local only at the destination.
        
        int x = context.current_router.x;
        int y = context.current_router.y;
        int destX = context.destination_router.x;
        int destY = context.destination_router.y;
        
        if (x == destX && y == destY) return Local;

        if (configuration.routing == "XY") {
            if (x < destX) return PositiveX;
            if (x > destX) return NegativeX;
            if (y > destY) return NegativeY;
            if (y < destY) return PositiveY;
        } else {
            if (y < destY) return PositiveY;
            if (y > destY) return NegativeY;
            if (x < destX) return PositiveX;
            if (x > destX) return NegativeX;
        }

        // not_implemented("PA4--NOC-01");

    }

    std::optional<unsigned> select_router_input(
        const std::vector<ArbitrationCandidate>& candidates/* candidates */,
        unsigned round_robin_cursor/* round_robin_cursor */,
        const SystemConfig& configuration /* configuration */) {

        if (candidates.empty()) return std::nullopt;
        if (configuration.arbitration == "rr") {
            const ArbitrationCandidate* selectedCandidate = nullptr;
            for (const auto& candidate: candidates) {
                if (candidate.input >= round_robin_cursor) {
                    if (selectedCandidate == nullptr || candidate.input < selectedCandidate->input)
                        selectedCandidate = &candidate;
                }
            }
            if (selectedCandidate == nullptr) {
                // wrap around if nothing is found
                for (const auto& candidate: candidates) {
                    if (selectedCandidate == nullptr || candidate.input < selectedCandidate->input)
                        selectedCandidate = &candidate;
                }
            }
            return selectedCandidate->input;
        }
        
        // PA4-NOC-02: Implement round-robin and your policy. Return an eligible input index.
        not_implemented("PA4-NOC-02");

    }

}

