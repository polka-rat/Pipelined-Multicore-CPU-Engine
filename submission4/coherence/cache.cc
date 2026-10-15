#include "pa4.h"
namespace multicore {

    bool accept_cache_request(
        MulticoreSystem& /* system */,
        int /* core_id */,
        const MemoryRequest& /* request */) {

        // PA4-COH-01: Implement permission checks, victim release, acquisition and local completion.
        // Return true only when this CPU request is accepted.
        not_implemented("PA4-COH-01");

    }

    bool handle_cache_message(
        MulticoreSystem& /* system */,
        int /* core_id */,
        const Packet& /* packet */) {

        // PA4-COH-02: Handle grants, recalls, invalidations and eviction acknowledgements.
        // A false return must leave staged state unchanged and send no messages.
        not_implemented("PA4-COH-02");

    }

}

