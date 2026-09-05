#pragma once
#include "cache/cache_controller.h"
#include "cache/replacement.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>


namespace pipesim::cache {
namespace cache_controller_internal {
enum class OwnerState { kUnsent, kInFlight, kResponding, kAckReady };
struct AddressFields { std::uint32_t block=0,set=0,tag=0,offset=0; };
struct PipelineEntry { bool valid=false; CacheRequest request; std::uint64_t arrival_order=0; AddressFields address; };
struct OutputEntry { bool valid=false; CacheResponse response; std::uint64_t arrival_order=0; };
struct MshrEntry { bool valid=false; std::uint32_t block_address=0,set=0,tag=0,reserved_way=0; std::uint64_t arrival_order=0; OwnerState state=OwnerState::kUnsent; CacheRequest lower_request; std::vector<MshrWaiter> waiters; };
struct WritebackEntry { bool valid=false; std::uint32_t address=0,size=0; std::vector<std::uint8_t> data; std::uint64_t arrival_order=0; OwnerState state=OwnerState::kUnsent; CacheRequest lower_request; std::vector<WritebackAckWaiter> waiters; std::uint32_t demand_bypasses=0; };
enum class ResponseOwner { kNone,kOutput,kMshr,kWriteback };
struct ResponseChoice { ResponseOwner owner=ResponseOwner::kNone; std::size_t entry=0,waiter=0; CacheResponse response; std::uint64_t arrival_order=0; };
enum class RequestOwner { kNone,kMshr,kWriteback };
struct RequestChoice { RequestOwner owner=RequestOwner::kNone; std::size_t entry=0; CacheRequest request; std::uint64_t arrival_order=0; bool demand_blocked_by_writeback=false; std::optional<std::size_t> oldest_eligible_writeback; };
enum class LookupKind { kEmpty,kHit,kMshrMerge,kMshrAllocate,kWritebackForward,kHold };
enum class LookupStall { kNone,kOutput,kMshrWaiterFull,kMshrFull,kReservedWay,kWritebackOrder,kWritebackWaiterFull,kWritebackFull };
struct LookupDecision { LookupKind kind=LookupKind::kEmpty; LookupStall stall=LookupStall::kNone; int hit_way=-1,mshr=-1,victim_way=-1; };
struct WritebackPlan { bool allowed=false; std::vector<std::size_t> merge_entries; int survivor=-1; LookupStall stall=LookupStall::kNone; };
inline bool RangesOverlap(std::uint32_t a,std::uint32_t as,std::uint32_t b,std::uint32_t bs){const auto ae=static_cast<std::uint64_t>(a)+as,be=static_cast<std::uint64_t>(b)+bs;return a<be&&b<ae;}
inline bool ResponseLess(const ResponseChoice&a,const ResponseChoice&b){return std::tie(a.arrival_order,a.response.id)<std::tie(b.arrival_order,b.response.id);}
inline TransactionSource SourceForRole(CacheRole role){switch(role){case CacheRole::kL1I:return TransactionSource::kL1I;case CacheRole::kL1D:return TransactionSource::kL1D;case CacheRole::kL2I:return TransactionSource::kL2I;case CacheRole::kL2D:return TransactionSource::kL2D;}throw std::runtime_error("invalid cache role");}
inline const char* OwnerStateName(OwnerState state){switch(state){case OwnerState::kUnsent:return "UNSENT";case OwnerState::kInFlight:return "IN_FLIGHT";case OwnerState::kResponding:return "RESPONDING";case OwnerState::kAckReady:return "ACK_READY";}return "UNKNOWN";}
}
using namespace cache_controller_internal;

class CacheController::Impl {
 public:
  Impl(CacheRole role, const CacheConfig& config,
       TransactionIdService* transaction_ids)
      : role_(role),
        config_(config),
        transaction_ids_(transaction_ids),
        current_lines_(config.sets,
                       std::vector<CacheLine>(config.ways)),
        next_lines_(current_lines_),
        current_mshrs_(config.mshr_entries),
        next_mshrs_(current_mshrs_),
        current_writebacks_(config.writeback_entries),
        next_writebacks_(current_writebacks_),
        replacement_(MakeReplacementPolicy(config.replacement, config.sets,
                                           config.ways)) {
    if (transaction_ids_ == nullptr) {
      throw std::runtime_error("cache controller transaction-id service is null");
    }
    Reset();
  }

  void Reset() {
    for (auto& set : current_lines_) {
      for (CacheLine& line : set) {
        line = CacheLine();
        line.data.assign(config_.line_size, 0);
      }
    }
    next_lines_ = current_lines_;
    current_index_ = PipelineEntry();
    current_lookup_ = PipelineEntry();
    current_output_ = OutputEntry();
    next_index_ = current_index_;
    next_lookup_ = current_lookup_;
    next_output_ = current_output_;
    current_mshrs_.assign(config_.mshr_entries, MshrEntry());
    next_mshrs_ = current_mshrs_;
    current_writebacks_.assign(config_.writeback_entries, WritebackEntry());
    next_writebacks_ = current_writebacks_;
    current_arrival_ = 0;
    next_arrival_ = 0;
    stats_ = CacheStats();
    next_stats_ = stats_;
    replacement_->Reset();
    begun_ = false;
    operated_ = false;
  }

  void BeginCycle() {
    if (begun_) {
      throw std::runtime_error("cache BeginCycle called twice");
    }
    next_lines_ = current_lines_;
    next_index_ = current_index_;
    next_lookup_ = current_lookup_;
    next_output_ = current_output_;
    next_mshrs_ = current_mshrs_;
    next_writebacks_ = current_writebacks_;
    next_arrival_ = current_arrival_;
    next_stats_ = stats_;
    replacement_->BeginCycle();
    begun_ = true;
    operated_ = false;
  }

  // TODO(A2-CACHE-02): local address mapping using this controller's
  // line_size and sets. Return block base, set, tag, offset. See SPEC.md/
  // ABI.md for the exact formula.
  AddressFields MapAddress(std::uint32_t address) const {
    
     AddressFields fields;

    fields.offset = address % config_.line_size;
    fields.block = address - fields.offset;

    const std::uint32_t block_number = address / config_.line_size;
    fields.set = block_number % config_.sets;
    fields.tag = block_number / config_.sets;

    return fields;
  }

  void ValidateInbound(const CacheRequest& request) const {
    ValidateRequestPayload(request);
    if (!RangeFitsLine(request.address, request.size, config_.line_size)) {
      throw std::runtime_error(std::string(CacheRoleName(role_)) +
                               " request crosses a local cache line");
    }
    switch (role_) {
      case CacheRole::kL1I:
      case CacheRole::kL2I:
        if (request.type != RequestType::kRead) {
          throw std::runtime_error("instruction cache received a write request");
        }
        break;
      case CacheRole::kL1D:
        if (request.type != RequestType::kRead &&
            request.type != RequestType::kWrite) {
          throw std::runtime_error("L1D received an illegal request type");
        }
        break;
      case CacheRole::kL2D:
        if (request.type != RequestType::kRead &&
            request.type != RequestType::kWriteback) {
          throw std::runtime_error("L2D received an illegal request type");
        }
        break;
    }
    if ((role_ == CacheRole::kL1I || role_ == CacheRole::kL1D) &&
        request.origin_id != request.id) {
      throw std::runtime_error("L1 CPU request origin must equal link id");
    }
    if ((role_ == CacheRole::kL1I || role_ == CacheRole::kL1D) &&
        (request.size != 4 || request.address % 4 != 0)) {
      throw std::runtime_error("CPU cache request must be one aligned word");
    }
    if ((role_ == CacheRole::kL1D || role_ == CacheRole::kL2D) &&
        request.type != RequestType::kWriteback && request.generation != 0) {
      throw std::runtime_error("data demand carries a fetch generation");
    }
  }

  int FindHit(const AddressFields& address) const {
    int hit = -1;
    for (std::uint32_t way = 0; way < config_.ways; ++way) {
      const CacheLine& line = current_lines_[address.set][way];
      if (line.valid && line.tag == address.tag) {
        if (hit >= 0) throw std::runtime_error("duplicate tags in one cache set");
        hit = static_cast<int>(way);
      }
    }
    return hit;
  }

  int FindMshrByBlock(std::uint32_t block) const {
    int found = -1;
    for (std::size_t i = 0; i < current_mshrs_.size(); ++i) {
      if (current_mshrs_[i].valid &&
          current_mshrs_[i].block_address == block) {
        if (found >= 0) throw std::runtime_error("duplicate MSHR block owner");
        found = static_cast<int>(i);
      }
    }
    return found;
  }

  int FindMshrByLowerId(std::uint64_t id) const {
    int found = -1;
    for (std::size_t i = 0; i < current_mshrs_.size(); ++i) {
      if (current_mshrs_[i].valid &&
          current_mshrs_[i].lower_request.id == id) {
        if (found >= 0) throw std::runtime_error("duplicate MSHR lower id");
        found = static_cast<int>(i);
      }
    }
    return found;
  }

  int FindWritebackByLowerId(std::uint64_t id) const {
    int found = -1;
    for (std::size_t i = 0; i < current_writebacks_.size(); ++i) {
      if (current_writebacks_[i].valid &&
          current_writebacks_[i].lower_request.id == id) {
        if (found >= 0) throw std::runtime_error("duplicate writeback lower id");
        found = static_cast<int>(i);
      }
    }
    return found;
  }

  int FindFreeMshr() const {
    for (std::size_t i = 0; i < current_mshrs_.size(); ++i) {
      if (!current_mshrs_[i].valid) return static_cast<int>(i);
    }
    return -1;
  }

  int FindFreeWriteback() const {
    for (std::size_t i = 0; i < current_writebacks_.size(); ++i) {
      if (!current_writebacks_[i].valid) return static_cast<int>(i);
    }
    return -1;
  }

  std::vector<bool> EligibleWays(std::uint32_t set) const {
    std::vector<bool> eligible(config_.ways, false);
    for (std::uint32_t way = 0; way < config_.ways; ++way) {
      eligible[way] = !current_lines_[set][way].reserved;
    }
    return eligible;
  }

  int PeekVictim(std::uint32_t set) const {
    const std::vector<bool> eligible = EligibleWays(set);
    for (std::uint32_t way = 0; way < config_.ways; ++way) {
      if (eligible[way] && !current_lines_[set][way].valid) return way;
    }
    return replacement_->PeekVictim(set, eligible);
  }

  // TODO(A2-CACHE-08): choose/reserve a deterministic victim among
  // currently-eligible, currently-invalid-first ways; fall back to the
  // configured replacement policy. See SPEC.md's three-step victim
  // priority.
  int SelectVictim(std::uint32_t set) {
    (void)set;
    throw std::runtime_error("CacheController::SelectVictim: TODO(A2-CACHE-08)");
  }

  // TODO(A2-CACHE-11): compute the transitive UNSENT overlap closure for a
  // prospective writeback of [address, address+size); hold
  // (kWritebackOrder) on any IN_FLIGHT or same-cycle-granted overlap;
  // enforce the bounded combined waiter count; choose the oldest survivor
  // by (arrival_order, id).
  WritebackPlan PlanWriteback(std::uint32_t address, std::uint32_t size,
                              std::size_t new_waiters,
                              int granted_writeback = -1) const {
    (void)address;
    (void)size;
    (void)new_waiters;
    (void)granted_writeback;
    throw std::runtime_error("CacheController::PlanWriteback: TODO(A2-CACHE-11)");
  }

  // TODO(A2-CACHE-05): true iff a granted upper response this cycle would
  // drain current OUTPUT (used to plan same-cycle OUTPUT reuse).
  bool OutputWillDrain(bool response_granted) const {
    return current_output_.valid && response_granted;
  }

  // TODO(A2-CACHE-07/08/10): the parallel LOOKUP decision -- hit, MSHR
  // merge/allocate, writeback-forward, or hold with the specific stall
  // reason, in the priority SPEC.md/ABI.md define. Must not mutate state.
  // Existing helpers: FindHit, FindMshrByBlock, FindFreeMshr, PeekVictim, FindFreeWriteback, PlanWriteback.
  LookupDecision DecideLookup(bool output_space,
                              int granted_writeback = -1) const {
    (void)output_space;
    (void)granted_writeback;
    throw std::runtime_error("CacheController::DecideLookup: TODO(A2-CACHE-07)");
  }

  // TODO(A2-CACHE-05): plan three-stage backpressure from current_* state
  // and the resolved grant flags only. accept_lower_response must be true
  // iff lower_response is a recognized, correctly-typed, echo-valid owner
  // (MSHR fill or writeback ack) currently IN_FLIGHT. An owned demand fill
  // takes the combined LOOKUP port for the cycle. accept_upper_request must
  // reflect whether INDEX is free or about to be vacated into LOOKUP this
  // cycle. Must not mutate state.
  // Existing helpers: FindMshrByLowerId, FindWritebackByLowerId, ValidateEcho, ValidateInbound, OutputWillDrain, DecideLookup.
  CacheStageIntent StageIntent(const CacheRequest* upper_request,
                               const CacheResponse* lower_response,
                               bool response_granted,
                               bool lower_request_granted) const {
    (void)upper_request;
    (void)lower_response;
    (void)response_granted;
    (void)lower_request_granted;
    throw std::runtime_error("CacheController::StageIntent: TODO(A2-CACHE-05)");
  }

  // TODO(A2-CACHE-08): construct a waiter (initialize both response flags
  // false, response_data empty) that records identity/order enough to
  // reconstruct an upward CacheResponse later. See ABI.md's MshrWaiter
  // invariants.
  MshrWaiter MakeMshrWaiter(const PipelineEntry& pipeline) const {
    (void)pipeline;
    throw std::runtime_error("CacheController::MakeMshrWaiter: TODO(A2-CACHE-08)");
  }

  // TODO(A2-CACHE-10)
  WritebackAckWaiter MakeWritebackWaiter(
      const PipelineEntry& pipeline) const {
    (void)pipeline;
    throw std::runtime_error(
        "CacheController::MakeWritebackWaiter: TODO(A2-CACHE-10)");
  }

  std::uint64_t AllocateArrival() {
    if (next_arrival_ == std::numeric_limits<std::uint64_t>::max()) {
      throw std::runtime_error("cache arrival-order counter overflow");
    }
    return next_arrival_++;
  }

  // TODO(A2-CACHE-07): construct the response for a committed hit
  // (kReadData with the read bytes, kWriteAck, or kWritebackAck).
  CacheResponse ResponseForHit(const PipelineEntry& pipeline,
                               const CacheLine& line) const {
    (void)pipeline;
    (void)line;
    throw std::runtime_error("CacheController::ResponseForHit: TODO(A2-CACHE-07)");
  }

  // TODO(A2-CACHE-04): consider OUTPUT, the oldest ready-unsent waiter of
  // each responding MSHR, and the oldest unsent waiter of each ACK_READY
  // writeback; select the minimum (arrival_order, response id). Must not
  // mutate state.
  ResponseChoice SelectResponse() const {
   // throw std::runtime_error("CacheController::SelectResponse: TODO(A2-CACHE-04)");
    ResponseChoice best;

    auto consider = [&](const ResponseChoice& candidate) {
        if (best.owner == ResponseOwner::kNone ||
            ResponseLess(candidate, best)) {
            best = candidate;
        }
    };

    // Current OUTPUT-stage response.
    if (current_output_.valid) {
        ResponseChoice candidate;
        candidate.owner = ResponseOwner::kOutput;
        candidate.response = current_output_.response;
        candidate.arrival_order = current_output_.arrival_order;
        consider(candidate);
    }

    // Oldest ready, unsent waiter from each responding MSHR.
    for (std::size_t i = 0; i < current_mshrs_.size(); ++i) {
        const MshrEntry& mshr = current_mshrs_[i];

        if (!mshr.valid || mshr.state != OwnerState::kResponding) {
            continue;
        }

        int oldest = -1;

        for (std::size_t j = 0; j < mshr.waiters.size(); ++j) {
            const MshrWaiter& waiter = mshr.waiters[j];

            if (!waiter.response_ready || waiter.response_sent) {
                continue;
            }

            if (oldest < 0 ||
                std::tie(waiter.arrival_order, waiter.request_id) <
                    std::tie(mshr.waiters[oldest].arrival_order,
                             mshr.waiters[oldest].request_id)) {
                oldest = static_cast<int>(j);
            }
        }

        if (oldest >= 0) {
            const MshrWaiter& waiter =
                mshr.waiters[static_cast<std::size_t>(oldest)];

            ResponseChoice candidate;
            candidate.owner = ResponseOwner::kMshr;
            candidate.entry = i;
            candidate.waiter = static_cast<std::size_t>(oldest);
            candidate.arrival_order = waiter.arrival_order;

            candidate.response.id = waiter.request_id;
            candidate.response.origin_id = waiter.origin_id;
            candidate.response.sequence = waiter.sequence;
            candidate.response.generation = waiter.generation;
            candidate.response.address = waiter.address;
            candidate.response.size = waiter.size;

            if (waiter.type == RequestType::kRead) {
                candidate.response.type = ResponseType::kReadData;
                candidate.response.data = waiter.response_data;
            } else {
                candidate.response.type = ResponseType::kWriteAck;
                candidate.response.data.clear();
            }

            consider(candidate);
        }
    }

    // Oldest unsent acknowledgement waiter from each ACK_READY writeback.
    for (std::size_t i = 0; i < current_writebacks_.size(); ++i) {
        const WritebackEntry& writeback = current_writebacks_[i];

        if (!writeback.valid ||
            writeback.state != OwnerState::kAckReady) {
            continue;
        }

        int oldest = -1;

        for (std::size_t j = 0; j < writeback.waiters.size(); ++j) {
            const WritebackAckWaiter& waiter = writeback.waiters[j];

            if (waiter.response_sent) {
                continue;
            }

            if (oldest < 0 ||
                std::tie(waiter.arrival_order, waiter.request_id) <
                    std::tie(writeback.waiters[oldest].arrival_order,
                             writeback.waiters[oldest].request_id)) {
                oldest = static_cast<int>(j);
            }
        }

        if (oldest >= 0) {
            const WritebackAckWaiter& waiter =
                writeback.waiters[static_cast<std::size_t>(oldest)];

            ResponseChoice candidate;
            candidate.owner = ResponseOwner::kWriteback;
            candidate.entry = i;
            candidate.waiter = static_cast<std::size_t>(oldest);
            candidate.arrival_order = waiter.arrival_order;

            candidate.response.id = waiter.request_id;
            candidate.response.origin_id = waiter.origin_id;
            candidate.response.sequence = waiter.sequence;
            candidate.response.generation = waiter.generation;
            candidate.response.address = waiter.address;
            candidate.response.size = waiter.size;
            candidate.response.type = ResponseType::kWritebackAck;
            candidate.response.data.clear();

            consider(candidate);
        }
    }
    return best;
  }

  // TODO(A2-CACHE-06): true iff request's range overlaps any
  // non-ACK_READY (i.e. UNSENT or IN_FLIGHT) writeback entry.
  bool ConflictsWithLowerIncompleteWriteback(const CacheRequest& request) const {
    (void)request;
    throw std::runtime_error(
        "CacheController::ConflictsWithLowerIncompleteWriteback: "
        "TODO(A2-CACHE-06)");
  }

  // TODO(A2-CACHE-06/11): select the oldest eligible UNSENT demand MSHR
  // (excluding any that overlap an incomplete writeback) and the oldest
  // eligible UNSENT writeback, sorted by (arrival_order, lower id). A
  // demand wins unless the writeback buffer has no free entry or the
  // oldest eligible writeback has already been bypassed by eight demand
  // grants, in which case that writeback must be selected instead. Must
  // not mutate state or allocate an id.
  // Existing helpers: ConflictsWithLowerIncompleteWriteback, FindFreeWriteback.
  RequestChoice SelectLowerRequest() const {
    throw std::runtime_error(
        "CacheController::SelectLowerRequest: TODO(A2-CACHE-06)");
  }

  // TODO(A2-CACHE-04): available (already implemented, call from here):
  // SelectResponse() (above) does the candidate selection; this method
  // just extracts its response if any.
  std::optional<CacheResponse> ResponseIntent() const {
    const ResponseChoice choice = SelectResponse();

    if (choice.owner == ResponseOwner::kNone) {
        return std::nullopt;
    }

    return choice.response;
  }

  // TODO(A2-CACHE-06): available (already implemented, call from here):
  // SelectLowerRequest() (above) does the candidate selection; this
  // method just extracts its request if any.
  std::optional<CacheRequest> LowerRequestIntent() const {
    throw std::runtime_error(
        "CacheController::LowerRequestIntent: TODO(A2-CACHE-06)");
  }

  // TODO(A2-CACHE-12): mark the candidate ResponseIntent() offered as sent;
  // free an MSHR (unpinning its way) or writeback entry once every one of
  // its waiters has been sent.
  // Existing helpers: SelectResponse, ResponseIntent.
  void ApplyResponseGrant() {
    throw std::runtime_error(
        "CacheController::ApplyResponseGrant: TODO(A2-CACHE-12)");
  }

  void ValidateEcho(const CacheResponse& response,
                    const CacheRequest& request,
                    ResponseType expected_type) const {
    if (response.id != request.id ||
        response.origin_id != request.origin_id ||
        response.sequence != request.sequence ||
        response.generation != request.generation ||
        response.address != request.address || response.size != request.size ||
        response.type != expected_type) {
      throw std::runtime_error("lower response metadata does not echo request");
    }
  }

  // TODO(A2-CACHE-09/10): for an owned fill -- install the complete line
  // into its already-reserved way, replay every waiter once in arrival
  // order at byte granularity (capturing each load's bytes at its own
  // replay point, applying each store's bytes), mark dirty if any waiter
  // wrote, call replacement_->OnInsert exactly once. For an owned
  // writeback ack -- transition ACK_READY (or free immediately if it had
  // no waiters).
  // Existing helpers: FindMshrByLowerId, FindWritebackByLowerId, ValidateEcho.
  void ApplyLowerResponse(const CacheResponse& response) {
    (void)response;
    throw std::runtime_error(
        "CacheController::ApplyLowerResponse: TODO(A2-CACHE-09)");
  }

  // TODO(A2-CACHE-10/11): insert a new writeback entry, or merge into the
  // transitive-overlap survivor chosen by a matching PlanWriteback() call
  // -- oldest entry/ID survives, bytes applied in entry-age order with the
  // newly-accepted bytes winning any overlap (this call's data is newest),
  // combined waiters bounded and sorted by (arrival_order, request id).
  void InsertWriteback(std::uint32_t address,
                       const std::vector<std::uint8_t>& data,
                       std::uint64_t arrival_order,
                       const std::optional<WritebackAckWaiter>& waiter,
                       int granted_writeback = -1,
                       std::optional<std::uint64_t> preallocated_id =
                           std::nullopt) {
    (void)address;
    (void)data;
    (void)arrival_order;
    (void)waiter;
    (void)granted_writeback;
    (void)preallocated_id;
    throw std::runtime_error(
        "CacheController::InsertWriteback: TODO(A2-CACHE-11)");
  }

  // TODO(A2-CACHE-07): apply a committed hit -- write store/writeback bytes
  // at the request's offset, mark dirty, build the response (read the
  // pre-write line for a load, the post-write line otherwise), call
  // replacement_->OnHit, and update hits/writeback_hits exactly once.
  // Existing helpers: ResponseForHit.
  void ApplyHit(const LookupDecision& decision) {
    (void)decision;
    throw std::runtime_error("CacheController::ApplyHit: TODO(A2-CACHE-07)");
  }

  // TODO(A2-CACHE-08): append a waiter to the existing MSHR; update
  // mshr_merges exactly once; clear LOOKUP.
  void ApplyMshrMerge(int mshr_index) {
    (void)mshr_index;
    throw std::runtime_error(
        "CacheController::ApplyMshrMerge: TODO(A2-CACHE-08)");
  }

  // TODO(A2-CACHE-08): allocate the new primary MSHR at the planned victim
  // way, reserving it; if the victim line is valid+dirty, insert its
  // writeback (allocating a second id in the same batch when a new
  // writeback owner is created, via transaction_ids_->ReserveBatch);
  // create one full-local-line lower kRead with one retained id; update
  // evictions/dirty_evictions/primary_misses exactly once each as
  // applicable; clear LOOKUP.
  // Existing helpers: SelectVictim, PeekVictim, MakeMshrWaiter, InsertWriteback.
  void ApplyMshrAllocation(const LookupDecision& decision,
                           int granted_writeback) {
    (void)decision;
    (void)granted_writeback;
    throw std::runtime_error(
        "CacheController::ApplyMshrAllocation: TODO(A2-CACHE-08)");
  }

  // TODO(A2-CACHE-10): forward an upper writeback into InsertWriteback with
  // its own ack waiter; clear LOOKUP.
  // Existing helpers: MakeWritebackWaiter.
  void ApplyWritebackForward(int granted_writeback) {
    (void)granted_writeback;
    throw std::runtime_error(
        "CacheController::ApplyWritebackForward: TODO(A2-CACHE-10)");
  }

  // TODO(A2-CACHE-13): increment exactly the one stats counter
  // corresponding to `stall` (kNone/kOutput count nothing). See SPEC.md's
  // stall-precedence chain for which counter fires when several reasons
  // could apply.
  void CountLookupStall(LookupStall stall) {
    (void)stall;
    throw std::runtime_error(
        "CacheController::CountLookupStall: TODO(A2-CACHE-13)");
  }

  // TODO(A2-CACHE-12): transition the granted MSHR/writeback UNSENT ->
  // IN_FLIGHT. Granting a demand bumps the oldest eligible writeback's
  // demand_bypasses (capped at 8); granting a writeback resets
  // demand_bypasses to 0 and counts writebacks_sent.
  void ApplyLowerRequestGrant() {
    throw std::runtime_error(
        "CacheController::ApplyLowerRequestGrant: TODO(A2-CACHE-12)");
  }

  // TODO(A2-CACHE-12): the single mutating call per cycle. Apply only the
  // transfers `grants` authorizes, computed from ONE consistent snapshot of
  // current_* state: upper-response removal (ApplyResponseGrant), owned
  // fill/writeback-ack installation (ApplyLowerResponse), at most one
  // parallel LOOKUP decision or its exact stall (DecideLookup +
  // ApplyHit/ApplyMshrMerge/ApplyMshrAllocation/ApplyWritebackForward/
  // CountLookupStall -- note a fill takes the LOOKUP port, so it must not
  // additionally run), back-to-front INDEX->LOOKUP movement plus admission
  // of upper_request into INDEX, and the granted lower-request owner
  // transition (ApplyLowerRequestGrant). Update every counter exactly
  // once, at its defined event point. Throw for any grant referring to a
  // nonexistent candidate, or any owner/echo mismatch -- see ABI.md's
  // error taxonomy.
  // Existing helpers: ResponseIntent, LowerRequestIntent, StageIntent, ValidateInbound, AllocateArrival, MapAddress.
  void Operate(std::uint64_t current_cycle,
               const CacheRequest* upper_request,
               const CacheResponse* lower_response,
               const CacheOperateGrants& grants) {
    (void)current_cycle;
    (void)upper_request;
    (void)lower_response;
    (void)grants;
    if (!begun_ || operated_) {
      throw std::runtime_error("cache Operate lifecycle violation");
    }
    throw std::runtime_error("CacheController::Operate: TODO(A2-CACHE-12)");
  }

  void Commit() {
    if (!begun_ || !operated_) {
      throw std::runtime_error("cache Commit lifecycle violation");
    }
    replacement_->Commit();
    current_lines_ = std::move(next_lines_);
    current_index_ = std::move(next_index_);
    current_lookup_ = std::move(next_lookup_);
    current_output_ = std::move(next_output_);
    current_mshrs_ = std::move(next_mshrs_);
    current_writebacks_ = std::move(next_writebacks_);
    current_arrival_ = next_arrival_;
    stats_ = next_stats_;
    begun_ = false;
    operated_ = false;
  }

  CacheSnapshot Snapshot() const {
    CacheSnapshot snapshot;
    const PipelineEntry entries[2] = {current_index_, current_lookup_};
    for (int i = 0; i < 2; ++i) {
      snapshot.pipeline[i].valid = entries[i].valid;
      if (entries[i].valid) {
        snapshot.pipeline[i].request_id = entries[i].request.id;
        snapshot.pipeline[i].address = entries[i].request.address;
        snapshot.pipeline[i].state = i == 0 ? "INDEX" : "LOOKUP";
      }
    }
    snapshot.pipeline[2].valid = current_output_.valid;
    if (current_output_.valid) {
      snapshot.pipeline[2].request_id = current_output_.response.id;
      snapshot.pipeline[2].address = current_output_.response.address;
      snapshot.pipeline[2].state = "OUTPUT";
    }
    for (const MshrEntry& entry : current_mshrs_) {
      MshrSnapshot row;
      row.waiter_capacity = config_.mshr_waiters_per_entry;
      if (entry.valid) {
        row.valid = true;
        row.block_address = entry.block_address;
        row.set = entry.set;
        row.tag = entry.tag;
        row.reserved_way = static_cast<int>(entry.reserved_way);
        row.lower_id = entry.lower_request.id;
        row.arrival_order = entry.arrival_order;
        row.state = OwnerStateName(entry.state);
        row.waiters = entry.waiters;
      }
      snapshot.mshrs.push_back(std::move(row));
    }
    for (const WritebackEntry& entry : current_writebacks_) {
      WritebackSnapshot row;
      row.waiter_capacity = config_.writeback_waiters_per_entry;
      if (entry.valid) {
        row.valid = true;
        row.address = entry.address;
        row.size = entry.size;
        row.lower_id = entry.lower_request.id;
        row.arrival_order = entry.arrival_order;
        row.demand_bypasses = entry.demand_bypasses;
        row.state = OwnerStateName(entry.state);
        row.data = entry.data;
        row.waiters = entry.waiters;
      }
      snapshot.writebacks.push_back(std::move(row));
    }
    for (std::uint32_t set = 0; set < config_.sets; ++set) {
      const std::vector<std::uint32_t> replacement =
          replacement_->DebugState(set);
      snapshot.replacement.push_back({set, replacement});
      for (std::uint32_t way = 0; way < config_.ways; ++way) {
        std::uint64_t owner = kNoOriginId;
        for (const MshrEntry& mshr : current_mshrs_) {
          if (mshr.valid && mshr.set == set && mshr.reserved_way == way) {
            owner = mshr.lower_request.id;
          }
        }
        snapshot.lines.push_back(
            {set, way, current_lines_[set][way], owner});
      }
    }
    return snapshot;
  }

  bool HasWork() const {
    if (current_index_.valid || current_lookup_.valid || current_output_.valid) {
      return true;
    }
    return std::any_of(current_mshrs_.begin(), current_mshrs_.end(),
                       [](const MshrEntry& entry) { return entry.valid; }) ||
           std::any_of(current_writebacks_.begin(), current_writebacks_.end(),
                       [](const WritebackEntry& entry) { return entry.valid; });
  }

  CacheRole role_;
  CacheConfig config_;
  TransactionIdService* transaction_ids_;
  std::vector<std::vector<CacheLine>> current_lines_;
  std::vector<std::vector<CacheLine>> next_lines_;
  PipelineEntry current_index_;
  PipelineEntry current_lookup_;
  OutputEntry current_output_;
  PipelineEntry next_index_;
  PipelineEntry next_lookup_;
  OutputEntry next_output_;
  std::vector<MshrEntry> current_mshrs_;
  std::vector<MshrEntry> next_mshrs_;
  std::vector<WritebackEntry> current_writebacks_;
  std::vector<WritebackEntry> next_writebacks_;
  std::uint64_t current_arrival_ = 0;
  std::uint64_t next_arrival_ = 0;
  CacheStats stats_;
  CacheStats next_stats_;
  std::unique_ptr<ReplacementPolicy> replacement_;
  bool begun_ = false;
  bool operated_ = false;
};
} // namespace pipesim::cache
