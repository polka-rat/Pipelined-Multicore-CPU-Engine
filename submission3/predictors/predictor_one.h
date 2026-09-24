#pragma once

#include "branch_prediction.h"

#include <array>
#include <cstddef>
#include <cstdint>

namespace pipesim {

class PredictorOne final : public BranchPredictor {
 public:
  std::string Name() const override;
  void Reset() override;
  BranchPrediction Predict(std::uint32_t pc) override;
  void ObserveInstruction(const Instruction& instruction, const BranchPrediction& prediction) override;
  void Update(const BranchResult& result, const BranchPrediction& prediction) override;
  std::uint64_t StorageBits() const override;
  const PredictorMetrics& Metrics() const override;

 private:
  PredictorMetrics metrics_;

  //Added
  static constexpr std::size_t kBaseTableEntries = 1024;
  static constexpr std::size_t kTableEntries = 256;
  static constexpr std::size_t kNumTaggedTables = 4;

  static constexpr unsigned kIndexBits = 8;
  static constexpr unsigned kTagBits = 9;
  static constexpr std::uint16_t kTagMask = (1u << kTagBits) - 1u;

  // alternate clearing usefulness bit planes every 256K branches. Aging follows training.
  static constexpr std::uint32_t kUsefulnessAgingPeriod = 1u << 18;

  /* information layout: 
  [0:2]-> provider + 1, 
  [3:5] -> alternate + 1 (base is 0),
  [6] -> provider_taken, 
  [7] -> alternate_taken, 
  [8] -> metadata_valid,
  [9:16] -> provider_entry index, 
  [17:25] -> provider_tag,
  [26:57] -> four 8-bit folded histories, 
  [58] -> allocation context valid.
  BTB misses retain allocation context but leave bit 8 clear.
  */
  static constexpr unsigned kProviderIndexShift = 9;
  static constexpr unsigned kProviderTagShift = 17;
  static constexpr unsigned kAllocationHistoryShift = 26;
  static constexpr unsigned kAllocationContextValidBit = kAllocationHistoryShift + kNumTaggedTables * kIndexBits;
  static_assert(kNumTaggedTables <= 7);
  static_assert(kAllocationContextValidBit < 64);

  struct TaggedTableEntry {
    bool valid = false;
    std::uint16_t tag = 0;       
    std::int8_t pred_counter = 0; // Logical 3-bit counter: [-4, 3]
    std::uint8_t useful = 0;  // 2 bit counter: [0,3]

  };

  std::array<std::array<TaggedTableEntry, kTableEntries>, kNumTaggedTables> taggedTables_;
  static constexpr std::array<std::size_t, kNumTaggedTables> tagged_tables_hist_len={8,16,32,64};
  std::array<std::uint8_t, kBaseTableEntries> base_;

  std::uint64_t global_history_ = 0;
  std::uint16_t path_history_ = 0;
  std::uint32_t allocation_random_state_ = 1;
  // Logical aging state: 18-bit interval counter plus one phase bit.
  std::uint32_t usefulness_aging_count_ = 0;
  bool clear_usefulness_high_bit_ = true;

  struct TaggedLookup {
    std::size_t index = 0;
    std::uint16_t tag = 0;
    bool hit = false;
  };

  // Performing XOR using newest history_length bits to get output_bits bits.
  static std::uint32_t FoldHistory(std::uint64_t history, std::size_t history_length, unsigned output_bits);

  // Keep prediction and allocation on exactly the same hash functions.
  static std::size_t IndexFromFoldedHistory(std::uint32_t pc, std::uint32_t folded_history);
  static std::uint16_t TagFromFoldedHistory(std::uint32_t pc, std::uint32_t folded_history);
  std::uint32_t NextAllocationRandom();
  void AllocateTagged(const BranchResult& result, std::uint64_t information, int provider);

  std::size_t TaggedIndex(std::size_t table, std::uint32_t pc) const;
  std::uint16_t TaggedTag(std::size_t table, std::uint32_t pc) const;
  TaggedLookup LookupTagged(std::size_t table, std::uint32_t pc) const;

  void updateUsefulBits(TaggedTableEntry & entry, bool actual_taken, bool pred, bool altpred);
  void updatePredCounter(TaggedTableEntry& entry, bool actual_taken);
  void AgeUsefulness();


struct BranchTableEntry{
  bool valid = false;
  std::uint32_t pc = 0;
  std::uint32_t target = 0;
  BranchType type = BranchType::kNotBranch;
};

static constexpr std::size_t kBranchTableEntries = 1024;
std::array<BranchTableEntry, kBranchTableEntries> branchTable_;


//* SC related variables and functions

static constexpr std::size_t kSCNumEntries = 128;       // No. of entries in a bias table
// static constexpr std::size_t kSCNumGehlEntries = 128;       // No. of entries in a GeHL table

static constexpr std::size_t kSCNumBiasTables = 2;     // No. of Bias tables
static constexpr std::size_t kSCNumGehlTables = 4;     // No. of GeHL tables

// Total number of tables
static constexpr std::size_t kSCNumTables = kSCNumBiasTables + kSCNumGehlTables;
std::array<std::array<std::int8_t, kSCNumEntries>, kSCNumTables> SCTables_;

// Geometrically increasing history lengths for each GeHL
static constexpr std::array<std::size_t, kSCNumGehlTables> kSCGehlHistoryLenArray = {4, 8, 16, 32};

static constexpr int kCounterBits = 6;
// static constexpr int kGlobalCounterBits = 5;
static constexpr int kCounterMax = 31;   
static constexpr int kCounterMin = -32;
// static constexpr int kGlobalCounterMax = 15; 
// static constexpr int kGlobalCounterMin = -16;  

// Get bias table index
std::size_t GetSCIndex(std::uint32_t pc, bool isTAGETake, std::uint64_t information, std::size_t table);
// std::size_t GetSCGlobalIndex(std::uint32_t pc, std::uint32_t foldedHistory, std::size_t table);
int GetSCSum(std::uint32_t pc, bool isTAGETaken, std::uint64_t information);
};

}
