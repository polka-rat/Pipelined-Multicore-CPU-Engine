#include "pa4.h"
namespace multicore {

    namespace {

        constexpr const char* AwaitGetS = "await_gets";
        constexpr const char* AwaitGetM = "await_getm";
        constexpr const char* AwaitPutS = "await_puts";
        constexpr const char* AwaitPutM = "await_putm";

        bool is_eviction_phase(const PrivateCache& cache) {

            return cache.phase == AwaitPutS || cache.phase == AwaitPutM;

        }

        MessageType acquisition_for(const MemoryRequest& request) {
            return request.operation == MemoryOperation::Load ? MessageType::GetS : MessageType::GetM;
        }

        const char* acquisition_phase(MessageType type) {
            return type == MessageType::GetS ? AwaitGetS : AwaitGetM;
        }

        void count_accepted(MulticoreSystem& system, int core, const MemoryRequest& request) {
            const std::string prefix = "cache." + std::to_string(core);

            switch (request.operation) {
                case MemoryOperation::Load:
                    system.event(prefix, "loads_accepted");
                    break;
                case MemoryOperation::Store:
                    system.event(prefix, "stores_accepted");
                    break;
                case MemoryOperation::Exchange:
                    system.event(prefix, "atomics_accepted");
                    break;
                case MemoryOperation::Fence:
                    throw std::runtime_error("Fence reached coherence cache");

            }
        }

        void count_permission_result(MulticoreSystem& system, int core,
                                     const MemoryRequest& request, bool hit,
                                     bool upgrade) {
            const std::string prefix = "cache." + std::to_string(core);

            switch (request.operation) {
                case MemoryOperation::Load:
                    system.event(prefix, hit ? "load_permission_hits" : "read_misses");
                    break;
                case MemoryOperation::Store:
                    system.event(prefix, hit ? "store_permission_hits" : "write_misses");
                    if (upgrade) system.event(prefix, "upgrades");
                    break;
                case MemoryOperation::Exchange:
                    system.event(prefix, hit ? "atomic_permission_hits" : "atomic_misses");
                    break;
                case MemoryOperation::Fence:
                    throw std::runtime_error("Fence reached coherence cache");
            }

        }

        uint32_t complete_local_access(MulticoreSystem& system, int core,
                                       CacheLine& line, const MemoryRequest& request) {
            const unsigned word = (request.address % 64) / 4;

            switch (request.operation) {
                case MemoryOperation::Load:
                    return line.data[word];
                case MemoryOperation::Store:
                    line.data[word] = request.value;
                    return 0;
                case MemoryOperation::Exchange: {
                    const uint32_t old = line.data[word];
                    line.data[word] = request.value;
                    return old;
                }
                case MemoryOperation::Fence:
                    throw std::runtime_error("Fence reached coherence cache");

            }
            (void)system;
            (void)core;
            throw std::runtime_error("Unknown memory operation");
        }

        bool matches_pending(const PrivateCache& cache, int core, const Packet& packet,
                             uint32_t line_address) {
            return cache.pending && packet.requester == core &&
                packet.transaction_id == cache.transaction_id &&
                packet.line_address == line_address;
        }

    }  // namespace

    bool accept_cache_request(MulticoreSystem& system, int core_id, const MemoryRequest& request) {
        if (request.core != core_id) {
            throw std::runtime_error("Cache request delivered to the wrong core");
        }
        if (request.operation == MemoryOperation::Fence) {
            throw std::runtime_error("Fence reached coherence cache");
        }

        auto& cache = system.next.caches.at(core_id);

        if (cache.pending) {
            return false;
        }

        const uint32_t line_address = system.line_address(request.address);
        CacheLine& line = cache.lines.at(system.slot(request.address));
        const bool tag_match = line.state != CacheState::I && line.tag == line_address;
        const bool permission_hit = tag_match && (request.operation == MemoryOperation::Load || line.state == CacheState::M);

        if (permission_hit) {
            cache.pending = request;
            cache.transaction_id = request.id;
            count_accepted(system, core_id, request);
            count_permission_result(system, core_id, request, true, false);
            const uint32_t value = complete_local_access(system, core_id, line, request);
            line.transient = false;
            system.cache_complete(core_id, value);
            cache.transaction_id = 0;
            cache.evicted_line_address.reset();
            return true;
        }

        const MessageType get = acquisition_for(request);
        const bool upgrade = request.operation == MemoryOperation::Store && tag_match &&
            line.state == CacheState::S;
        const bool needs_eviction = line.state != CacheState::I && !tag_match;

        // A direct-mapped victim remains resident (and answerable to a recall)
        // until the home acknowledges its Put.  The new acquisition starts only
        // after that acknowledgement, so it can safely reuse this slot.
        if (needs_eviction) {
            const MessageType put = line.state == CacheState::M ? MessageType::PutM : MessageType::PutS;
            if (!system.can_send(core_id, put)) {
                return false;
            }

            const Packet release = system.message(put, core_id, system.home(line.tag), line.tag,
                                                  request.id, line.data, core_id);
            if (!system.send(release)) {
                return false;
            }

            cache.pending = request;
            cache.transaction_id = request.id;
            cache.evicted_line_address = line.tag;
            cache.phase = put == MessageType::PutM ? AwaitPutM : AwaitPutS;
            line.transient = true;

            count_accepted(system, core_id, request);
            count_permission_result(system, core_id, request, false, false);
            if (put == MessageType::PutM) {
                const std::string prefix = "cache." + std::to_string(core_id);
                system.event(prefix, "dirty_evictions");
                system.event(prefix, "writebacks_sent");
            }

            return true;

        }

        if (!system.can_send(core_id, get)) {
            return false;
        }

        const Packet acquire = system.message(get, core_id, system.home(line_address), line_address,
                                              request.id, {}, core_id);
        if (!system.send(acquire)) {
            return false;
        }

        cache.pending = request;
        cache.transaction_id = request.id;
        cache.phase = acquisition_phase(get);
        // An S-to-M upgrade keeps the old shared copy available for an incoming
        // invalidation, but the copy must not be used by another local access.
        line.transient = tag_match;

        count_accepted(system, core_id, request);
        count_permission_result(system, core_id, request, false, upgrade);

        return true;
    }

    bool handle_cache_message(MulticoreSystem& system, int core_id, const Packet& packet) {
        auto& cache = system.next.caches.at(core_id);
        const uint32_t line_address = packet.line_address;
        CacheLine& line = cache.lines.at(system.slot(line_address));

        if (packet.type == MessageType::DataS || packet.type == MessageType::DataM) {
            if (!matches_pending(cache, core_id, packet, line_address)) {
                throw std::runtime_error("Cache grant does not match its pending transaction");
            }

            const auto request = *cache.pending;
            const MessageType expected = acquisition_for(request);

            if ((expected == MessageType::GetS && packet.type != MessageType::DataS) ||
                (expected == MessageType::GetM && packet.type != MessageType::DataM)) {
                throw std::runtime_error("Cache received a grant with the wrong permission");
            }

            if (!system.can_send(core_id, MessageType::GrantAck)) {
                return false;
            }

            line = {packet.type == MessageType::DataS ? CacheState::S : CacheState::M,
                    line_address, packet.data, false};
            const uint32_t value = complete_local_access(system, core_id, line, request);
            const Packet acknowledgement = system.message(MessageType::GrantAck, core_id, packet.source,
                                                          line_address, packet.transaction_id, {}, core_id);

            // The access is architecturally complete before the home observes
            // GrantAck; this is the point at which the permission is usable.
            system.cache_complete(core_id, value);
            cache.transaction_id = 0;
            cache.evicted_line_address.reset();

            if (!system.send(acknowledgement)) {
                throw std::runtime_error("GrantAck capacity changed after preflight");
            }

            return true;
        }

        if (packet.type == MessageType::RecallS || packet.type == MessageType::RecallI) {

            if (!system.can_send(core_id, MessageType::Ack)) {
                return false;
            }

            const bool resident = line.tag == line_address && line.state == CacheState::M;
            const bool retained_victim = cache.evicted_line_address &&
                *cache.evicted_line_address == line_address && line.tag == line_address;
            const CacheLineData data = (resident || retained_victim) ? line.data : CacheLineData{};
            const Packet acknowledgement = system.message(MessageType::Ack, core_id, packet.source,
                                                          line_address, packet.transaction_id, data,
                                                          packet.requester);

            if (!system.send(acknowledgement)) {
                throw std::runtime_error("Recall Ack capacity changed after preflight");
            }

            if (resident) {
                line.state = packet.type == MessageType::RecallS ? CacheState::S : CacheState::I;
                line.transient = is_eviction_phase(cache);
            }

            system.event("cache." + std::to_string(core_id), "acks_sent");
            return true;
        }

        if (packet.type == MessageType::Inv) {
            if (!system.can_send(core_id, MessageType::Ack)) {
                return false;
            }

            const bool resident = line.tag == line_address && line.state != CacheState::I;
            const bool retained_victim = cache.evicted_line_address &&
                *cache.evicted_line_address == line_address && line.tag == line_address;
            const Packet acknowledgement = system.message(MessageType::Ack, core_id, packet.source,
                                                          line_address, packet.transaction_id, {},
                                                          packet.requester);

            if (!system.send(acknowledgement)) {
                throw std::runtime_error("Invalidation Ack capacity changed after preflight");
            }

            if (resident || retained_victim) {
                line.state = CacheState::I;
                line.transient = retained_victim ||
                    (cache.pending && cache.phase == AwaitGetM);
                system.event("cache." + std::to_string(core_id), "invalidations_applied");
            }

            const std::string prefix = "cache." + std::to_string(core_id);
            system.event(prefix, "invalidations_received");
            system.event(prefix, "acks_sent");
            return true;
        }

        if (packet.type == MessageType::PutAck) {
            if (!cache.pending || !is_eviction_phase(cache) ||
                !cache.evicted_line_address || *cache.evicted_line_address != line_address ||
                packet.transaction_id != cache.transaction_id || packet.requester != core_id) {

                throw std::runtime_error("PutAck does not match a retained cache victim");
            }

            const MessageType get = acquisition_for(*cache.pending);
            if (!system.can_send(core_id, get)) {
                return false;
            }

            const Packet acquire = system.message(get, core_id, system.home(cache.pending->address),
                                                  system.line_address(cache.pending->address),
                                                  cache.transaction_id, {}, core_id);

            if (!system.send(acquire)) {
                throw std::runtime_error("Get capacity changed after preflight");
            }

            // The Put is now durable at the home/LLC.  Its slot can be reused
            // by the pending request; clear it even if a recall already made it I.
            line = {};
            cache.evicted_line_address.reset();
            cache.phase = acquisition_phase(get);
            return true;
        }

        throw std::runtime_error("Unexpected packet at private cache");
    }

}

