#include "pa4.h"
namespace multicore {

    bool handle_directory_message(MulticoreSystem& /* system */, const Packet& /* packet */) {

        // PA4-COH-03: Accept or queue acquisition requests; match replies to the active transaction.
        not_implemented("PA4-COH-03");

    }

    void advance_directory_transaction(MulticoreSystem& /* system */, uint32_t /* line_address */) {

        // PA4-COH-04: Advance one line transaction using the shared LLC interface.
        not_implemented("PA4-COH-04");

    }

}

