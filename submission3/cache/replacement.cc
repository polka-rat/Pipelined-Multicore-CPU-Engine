#include "cache/replacement.h"

#include "cache/cache_types.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace pipesim {
namespace cache {
namespace {

[[maybe_unused]] void CheckCoordinates(std::uint32_t set, std::uint32_t way,
                      std::uint32_t sets, std::uint32_t ways) {
  if (set >= sets || way >= ways) {
    throw std::runtime_error("replacement-policy coordinate out of range");
  }
}

// TODO(A2-REPL-01): Tree-PLRU. Implement Reset/BeginCycle/OnHit/OnInsert/
// PeekVictim/SelectVictim/DebugState per SPEC.md/ABI.md.
//
// Worked example (current/next commit discipline only -- the same shape
// as tutorial.cc's ValueRegister::Commit): Commit() below is fully wired.
// Every other method here needs the same current_/planned_ separation, but
// with the actual tree-bit read/update logic, which is not given.
class TreePlru final : public ReplacementPolicy {
 public:
  TreePlru(std::uint32_t sets, std::uint32_t ways)
      : sets_(sets), ways_(ways),
        current_(sets, std::vector<bool>(ways - 1)),
        planned_(sets, std::vector<bool>(ways - 1)) {
    if (!IsPowerOfTwo(ways_)) {
      throw std::runtime_error("Tree-PLRU requires power-of-two ways");
    }
  }

  void Reset() override {
    throw std::runtime_error("TreePlru::Reset: TODO(A2-REPL-01)");
  }

  void BeginCycle() override {
    throw std::runtime_error("TreePlru::BeginCycle: TODO(A2-REPL-01)");
  }

  void OnHit(std::uint32_t set, std::uint32_t way) override {
    (void)set;
    (void)way;
    throw std::runtime_error("TreePlru::OnHit: TODO(A2-REPL-01)");
  }

  void OnInsert(std::uint32_t set, std::uint32_t way) override {
    (void)set;
    (void)way;
    throw std::runtime_error("TreePlru::OnInsert: TODO(A2-REPL-01)");
  }

  int SelectVictim(std::uint32_t set,
                   const std::vector<bool>& eligible) override {
    (void)set;
    (void)eligible;
    throw std::runtime_error("TreePlru::SelectVictim: TODO(A2-REPL-01)");
  }

  int PeekVictim(std::uint32_t set,
                 const std::vector<bool>& eligible) const override {
    (void)set;
    (void)eligible;
    throw std::runtime_error("TreePlru::PeekVictim: TODO(A2-REPL-01)");
  }

  std::vector<std::uint32_t> DebugState(std::uint32_t set) const override {
    (void)set;
    throw std::runtime_error("TreePlru::DebugState: TODO(A2-REPL-01)");
  }

  // Worked example: the general current/next commit discipline (compare
  // tutorial.cc's ValueRegister::Commit). Every other method above must
  // maintain the same current_/planned_ separation.
  void Commit() override {
    if (!begun_) throw std::runtime_error("Tree-PLRU Commit without begin");
    current_ = planned_;
    begun_ = false;
    event_planned_ = false;
  }

 private:
  std::uint32_t sets_;
  std::uint32_t ways_;
  std::vector<std::vector<bool>> current_;
  std::vector<std::vector<bool>> planned_;
  bool begun_ = false;
  bool event_planned_ = false;
};

// TODO(A2-REPL-02): SRRIP. Implement Reset/BeginCycle/OnHit/OnInsert/
// PeekVictim/SelectVictim/DebugState per SPEC.md/ABI.md (2-bit RRPV,
// init 3, hit 0, insertion 2, saturating aging, lowest-way tie-break).
class Srrip final : public ReplacementPolicy {
 public:
  Srrip(std::uint32_t sets, std::uint32_t ways)
      : sets_(sets), ways_(ways),
        current_(sets, std::vector<std::uint8_t>(ways, 3)),
        planned_(sets, std::vector<std::uint8_t>(ways, 3)) {}

  void Reset() override {
    throw std::runtime_error("Srrip::Reset: TODO(A2-REPL-02)");
  }

  void BeginCycle() override {
    throw std::runtime_error("Srrip::BeginCycle: TODO(A2-REPL-02)");
  }

  void OnHit(std::uint32_t set, std::uint32_t way) override {
    (void)set;
    (void)way;
    throw std::runtime_error("Srrip::OnHit: TODO(A2-REPL-02)");
  }

  void OnInsert(std::uint32_t set, std::uint32_t way) override {
    (void)set;
    (void)way;
    throw std::runtime_error("Srrip::OnInsert: TODO(A2-REPL-02)");
  }

  int SelectVictim(std::uint32_t set,
                   const std::vector<bool>& eligible) override {
    (void)set;
    (void)eligible;
    throw std::runtime_error("Srrip::SelectVictim: TODO(A2-REPL-02)");
  }

  int PeekVictim(std::uint32_t set,
                 const std::vector<bool>& eligible) const override {
    (void)set;
    (void)eligible;
    throw std::runtime_error("Srrip::PeekVictim: TODO(A2-REPL-02)");
  }

  std::vector<std::uint32_t> DebugState(std::uint32_t set) const override {
    (void)set;
    throw std::runtime_error("Srrip::DebugState: TODO(A2-REPL-02)");
  }

  void Commit() override {
    if (!begun_) throw std::runtime_error("SRRIP Commit without begin");
    current_ = planned_;
    begun_ = false;
    event_planned_ = false;
  }

 private:
  std::uint32_t sets_;
  std::uint32_t ways_;
  std::vector<std::vector<std::uint8_t>> current_;
  std::vector<std::vector<std::uint8_t>> planned_;
  bool begun_ = false;
  bool event_planned_ = false;
};

}  // namespace

// TODO(A2-REPL-03): return the requested concrete policy for the supplied
// sets/ways, or fail on an impossible enumerator. Worked example below.
std::unique_ptr<ReplacementPolicy> MakeReplacementPolicy(
    ReplacementKind kind, std::uint32_t sets, std::uint32_t ways) {
  switch (kind) {
    case ReplacementKind::kTreePlru:
      return std::unique_ptr<ReplacementPolicy>(new TreePlru(sets, ways));
    case ReplacementKind::kSrrip:
      return std::unique_ptr<ReplacementPolicy>(new Srrip(sets, ways));
  }
  throw std::runtime_error("unknown replacement policy");
}

}  // namespace cache
}  // namespace pipesim
