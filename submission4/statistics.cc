#include "pa4.h"
namespace multicore {

    void count_event(
        MulticoreSystem& /* system */,
        const std::string& /* component */,
        const std::string& /* event */,
        uint64_t /* amount */) {

        // PA4-COUNT-01: Update the declared counters for each accepted event.
        not_implemented("PA4-COUNT-01");

    }

    void sample_occupancy_and_stalls(MulticoreSystem& /* system */) {

        // PA4-COUNT-02: Sample current occupancy and stalls once per cycle.
        // Implement this after checking your PA3 migration with the arithmetic workload.
    }

}

