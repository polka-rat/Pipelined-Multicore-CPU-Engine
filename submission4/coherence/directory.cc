#include "pa4.h"
namespace multicore {

    namespace {

        constexpr const char* Start = "start";
        constexpr const char* WaitRecallS = "wait_recall_s";
        constexpr const char* WaitRecallI = "wait_recall_i";
        constexpr const char* WaitInvalidations = "wait_invalidations";
        constexpr const char* WaitLlcRead = "wait_llc_read";
        constexpr const char* WaitLlcWriteGrant = "wait_llc_write_grant";
        constexpr const char* WaitLlcWritePut = "wait_llc_write_put";
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

    void advance_directory_transaction(MulticoreSystem& system, uint32_t line_address) {

        // Only entries present in current are passed here.  In particular, a
        // transaction inserted by handle_directory_message this cycle must not
        // issue its first protocol action until the following cycle.
        const auto current_entry = system.current.directory.find(line_address);

        if (current_entry == system.current.directory.end()) {

            return;

        }

        auto staged_entry = system.next.directory.find(line_address);

        if (staged_entry == system.next.directory.end()) {

            throw std::runtime_error("Directory entry disappeared during a transaction");

        }

        DirectoryEntry& entry = staged_entry->second;
        const int home_tile = system.home(line_address);
        const std::string prefix = directory_prefix(system, line_address);

        const auto finish_release = [&] {

            entry.active = false;
            entry.pending_acknowledgements.clear();
            entry.phase = "idle";
            system.event(prefix, "transactions_completed");
            system.event(prefix, "completed_latency_sum", system.cycle - entry.started);

        };

        const auto send_put_ack = [&] {

            if (!system.can_send(home_tile, MessageType::PutAck)) {

                return false;

            }

            const Packet acknowledgement = system.message(MessageType::PutAck, home_tile,
                                                           entry.request.requester, line_address,
                                                           entry.request.transaction_id, {},
                                                           entry.request.requester);

            if (!system.send(acknowledgement)) {

                throw std::runtime_error("PutAck capacity changed after preflight");

            }

            finish_release();
            return true;

        };

        const auto send_grant = [&] {

            const bool shared = entry.request.type == MessageType::GetS;
            const MessageType grant = shared ? MessageType::DataS : MessageType::DataM;

            if (!system.can_send(home_tile, grant)) {

                return false;

            }

            const Packet reply = system.message(grant, home_tile, entry.request.requester,
                                                line_address, entry.request.transaction_id,
                                                entry.data, entry.request.requester);

            if (!system.send(reply)) {

                throw std::runtime_error("Data grant capacity changed after preflight");

            }

            if (shared) {

                // A recalled M owner has just been downgraded to S.  Preserve it
                // as a sharer before adding the newly granted reader.
                if (entry.state == CacheState::M && entry.owner >= 0) {

                    entry.sharers.insert(entry.owner);

                }

                entry.state = CacheState::S;
                entry.owner = -1;
                entry.sharers.insert(entry.request.requester);

            }

            else {

                entry.state = CacheState::M;
                entry.owner = entry.request.requester;
                entry.sharers.clear();
                system.event(prefix, "ownership_grants");

            }

            entry.phase = WaitGrantAck;
            return true;

        };

        const auto request_llc = [&](bool write, const CacheLineData& data, const char* phase) {

            if (!system.llc_request(home_tile, {
                line_address, entry.request.transaction_id, entry.request.requester, write, data, 0
            })) {

                return false;

            }

            entry.phase = phase;
            return true;

        };

        const auto send_recall = [&](MessageType type, const char* phase) {

            if (entry.owner < 0 || entry.owner >= system.configuration.cores) {

                throw std::runtime_error("Modified directory line has no valid owner");

            }

            if (!system.can_send(home_tile, type)) {

                return false;

            }

            const Packet recall = system.message(type, home_tile, entry.owner, line_address,
                                                 entry.request.transaction_id, {},
                                                 entry.request.requester);

            if (!system.send(recall)) {

                throw std::runtime_error("Recall capacity changed after preflight");

            }

            entry.pending_acknowledgements.insert(entry.owner);
            entry.phase = phase;
            system.event(prefix, "recalls_sent");
            return true;

        };

        // A completed GrantAck may expose the next queued request in the same
        // endpoint pass.  Start it here, but deliberately do not advance it.
        if (!entry.active) {

            if (!entry.waiting.empty()) {

                const Packet request = entry.waiting.front();
                entry.waiting.pop_front();
                start_transaction(entry, request, system.cycle);
                system.event(prefix, "transactions_started");

            }

            return;

        }

        if (entry.started == system.cycle) {

            return;

        }

        if (entry.phase == Start) {

            switch (entry.request.type) {

                case MessageType::GetS:
                    if (entry.state == CacheState::M) {

                        // A GetS cannot use LLC data while a core has M: it may
                        // be stale.  Recall the owner first and write its data
                        // back through the supplied LLC interface.
                        if (entry.owner == entry.request.requester) {

                            throw std::runtime_error("M owner issued a redundant GetS");

                        }

                        (void)send_recall(MessageType::RecallS, WaitRecallS);

                    }

                    else {

                        (void)request_llc(false, {}, WaitLlcRead);

                    }

                    return;

                case MessageType::GetM:
                    if (entry.state == CacheState::M) {

                        if (entry.owner == entry.request.requester) {

                            throw std::runtime_error("M owner issued a redundant GetM");

                        }

                        (void)send_recall(MessageType::RecallI, WaitRecallI);
                        return;

                    }

                    if (entry.state == CacheState::S) {

                        // Keep unsent invalidation targets in sharers and sent
                        // targets in pending_acknowledgements.  This lets us obey
                        // the one-new-snoop-per-home-per-cycle restriction without
                        // adding another transient container to DirectoryEntry.
                        entry.sharers.erase(entry.request.requester);
                        entry.phase = WaitInvalidations;
                        return;

                    }

                    (void)request_llc(false, {}, WaitLlcRead);
                    return;

                case MessageType::PutS: {

                    if (!system.can_send(home_tile, MessageType::PutAck)) {

                        return;

                    }

                    // A queued/retried PutS can be stale after a newer owner was
                    // granted.  Only alter metadata if this core is still a
                    // recorded sharer.
                    if (entry.state == CacheState::S &&
                        entry.sharers.find(entry.request.requester) != entry.sharers.end()) {

                        entry.sharers.erase(entry.request.requester);

                        if (entry.sharers.empty()) {

                            entry.state = CacheState::I;
                            entry.owner = -1;

                        }

                    }

                    (void)send_put_ack();
                    return;

                }

                case MessageType::PutM:
                    // Only the currently recorded M owner may update the LLC.
                    // An old queued PutM is acknowledged but never allowed to
                    // overwrite a line subsequently modified by another owner.
                    if (entry.state == CacheState::M &&
                        entry.owner == entry.request.requester) {

                        (void)request_llc(true, entry.request.data, WaitLlcWritePut);

                    }

                    else {

                        (void)send_put_ack();

                    }

                    return;

                default:
                    throw std::runtime_error("Directory started a nonrequest packet");

            }

        }

        if (entry.phase == WaitRecallS || entry.phase == WaitRecallI) {

            if (!entry.pending_acknowledgements.empty()) {

                return;

            }

            (void)request_llc(true, entry.data, WaitLlcWriteGrant);
            return;

        }

        if (entry.phase == WaitInvalidations) {

            if (!entry.sharers.empty()) {

                const int sharer = *entry.sharers.begin();

                if (!system.can_send(home_tile, MessageType::Inv)) {

                    return;

                }

                const Packet invalidation = system.message(MessageType::Inv, home_tile, sharer,
                                                           line_address, entry.request.transaction_id,
                                                           {}, entry.request.requester);

                if (!system.send(invalidation)) {

                    throw std::runtime_error("Invalidation capacity changed after preflight");

                }

                entry.sharers.erase(sharer);
                entry.pending_acknowledgements.insert(sharer);
                system.event(prefix, "invalidations_sent");
                return;

            }

            if (!entry.pending_acknowledgements.empty()) {

                return;

            }

            (void)request_llc(false, {}, WaitLlcRead);
            return;

        }

        if (entry.phase == WaitLlcRead) {

            // Do not consume a visible LLC reply until reply-channel capacity is
            // available for the corresponding DataS/DataM packet.
            const MessageType grant = entry.request.type == MessageType::GetS ?
                MessageType::DataS : MessageType::DataM;

            if (!system.can_send(home_tile, grant)) {

                return;

            }

            const auto reply = system.llc_reply(home_tile, line_address,
                                                entry.request.transaction_id,
                                                entry.request.requester);

            if (!reply) {

                return;

            }

            entry.data = reply->data;

            if (!send_grant()) {

                throw std::runtime_error("Grant capacity changed after LLC-reply preflight");

            }

            return;

        }

        if (entry.phase == WaitLlcWriteGrant) {

            const MessageType grant = entry.request.type == MessageType::GetS ?
                MessageType::DataS : MessageType::DataM;

            if (!system.can_send(home_tile, grant)) {

                return;

            }

            const auto reply = system.llc_reply(home_tile, line_address,
                                                entry.request.transaction_id,
                                                entry.request.requester);

            if (!reply) {

                return;

            }

            entry.data = reply->data;

            if (!send_grant()) {

                throw std::runtime_error("Grant capacity changed after LLC-reply preflight");

            }

            return;

        }

        if (entry.phase == WaitLlcWritePut) {

            if (!system.can_send(home_tile, MessageType::PutAck)) {

                return;

            }

            const auto reply = system.llc_reply(home_tile, line_address,
                                                entry.request.transaction_id,
                                                entry.request.requester);

            if (!reply) {

                return;

            }

            entry.data = reply->data;
            entry.state = CacheState::I;
            entry.owner = -1;
            entry.sharers.clear();

            if (!send_put_ack()) {

                throw std::runtime_error("PutAck capacity changed after LLC-reply preflight");

            }

            return;

        }

        if (entry.phase == WaitGrantAck) {

            return;

        }

        throw std::runtime_error("Unknown directory transaction phase");

    }

}

