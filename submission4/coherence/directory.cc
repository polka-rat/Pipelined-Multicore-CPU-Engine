#include "pa4.h"
namespace multicore {

    namespace {

        constexpr const char* Start = "start";
        constexpr const char* WaitRecallS = "wait_recall_s";
        constexpr const char* WaitRecallI = "wait_recall_i";
        constexpr const char* WaitInvalidations = "wait_invalidations";
        constexpr const char* WaitGrantAck = "wait_grant_ack";

        bool is_request(MessageType type) {

            return type == MessageType::GetS || type == MessageType::GetM ||
                type == MessageType::PutS || type == MessageType::PutM;

        }

        bool matches_active_request(const DirectoryEntry& entry, const Packet& packet) {

            return entry.active && entry.request.transaction_id == packet.transaction_id &&
                entry.request.requester == packet.requester;

        }

        std::string directory_prefix(const MulticoreSystem& system, uint32_t line_address) {

            return "directory." + std::to_string(system.home(line_address) - system.configuration.cores);

        }

        void start_transaction(DirectoryEntry& entry, const Packet& packet, Cycle cycle) {

            entry.active = true;
            entry.request = packet;
            entry.pending_acknowledgements.clear();
            entry.phase = Start;
            entry.started = cycle;

        }

    }  // namespace

    bool handle_directory_message(MulticoreSystem& system, const Packet& packet) {

        const uint32_t line_address = packet.line_address;
        const std::string prefix = directory_prefix(system, line_address);

        if (is_request(packet.type)) {

            if (packet.source != packet.requester || packet.requester < 0 ||
                packet.requester >= system.configuration.cores) {

                throw std::runtime_error("Directory request has an invalid requester");

            }

            auto found = system.next.directory.find(line_address);

            if (found == system.next.directory.end()) {

                auto inserted = system.next.directory.emplace(line_address, DirectoryEntry{});
                found = inserted.first;

            }

            DirectoryEntry& entry = found->second;

            if (entry.active) {

                if (entry.waiting.size() >= system.configuration.router_depth) {

                    // Returning false keeps the endpoint packet in its inbox.  No
                    // state or counter update has occurred, so this is a retry.
                    return false;

                }

                entry.waiting.push_back(packet);
                system.event(prefix, "requests_accepted");
                system.event(prefix, "conflicts_queued");
                return true;

            }

            start_transaction(entry, packet, system.cycle);
            system.event(prefix, "requests_accepted");
            system.event(prefix, "transactions_started");
            return true;

        }

        auto found = system.next.directory.find(line_address);

        if (found == system.next.directory.end()) {

            throw std::runtime_error("Directory reply has no line entry");

        }

        DirectoryEntry& entry = found->second;

        if (packet.type == MessageType::Ack) {

            if (!matches_active_request(entry, packet) ||
                entry.pending_acknowledgements.find(packet.source) ==
                    entry.pending_acknowledgements.end()) {

                throw std::runtime_error("Directory Ack does not match an outstanding snoop");

            }

            if (entry.phase == WaitRecallS || entry.phase == WaitRecallI) {

                // The directory must retain owner data before it lets another
                // requester observe the line.  Inv acknowledgements deliberately
                // do not use their payload.
                entry.data = packet.data;

            }

            else if (entry.phase != WaitInvalidations) {

                throw std::runtime_error("Directory received Ack in an invalid transaction phase");

            }

            entry.pending_acknowledgements.erase(packet.source);
            system.event(prefix, "acks_received");
            return true;

        }

        if (packet.type == MessageType::GrantAck) {

            if (!matches_active_request(entry, packet) || packet.source != entry.request.requester ||
                entry.phase != WaitGrantAck) {

                throw std::runtime_error("Directory GrantAck does not match an active grant");

            }

            entry.active = false;
            entry.pending_acknowledgements.clear();
            entry.phase = "idle";
            system.event(prefix, "transactions_completed");
            system.event(prefix, "completed_latency_sum", system.cycle - entry.started);
            return true;

        }

        throw std::runtime_error("Unexpected packet at home directory");

    }

    void advance_directory_transaction(MulticoreSystem& /* system */, uint32_t /* line_address */) {

        // PA4-COH-04: Advance one line transaction using the shared LLC interface.
        not_implemented("PA4-COH-04");

    }

}

