#include "pa4.h"
namespace multicore {

    int select_output_port(
        const RoutingContext& /* context */,
        const SystemConfig& /* configuration */) {

        // PA4-NOC-01: Implement XY/YX routing. Return Local only at the destination.
        not_implemented("PA4-NOC-01");

    }

    std::optional<unsigned> select_router_input(
        const std::vector<ArbitrationCandidate>& /* candidates */,
        unsigned /* round_robin_cursor */,
        const SystemConfig& /* configuration */) {

        // PA4-NOC-02: Implement round-robin and your policy. Return an eligible input index.
        not_implemented("PA4-NOC-02");

    }

}

