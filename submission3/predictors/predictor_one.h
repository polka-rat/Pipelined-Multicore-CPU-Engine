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
  void ObserveInstruction(const Instruction& instruction,
                          const BranchPrediction& prediction) override;
  void Update(const BranchResult& result,
              const BranchPrediction& prediction) override;
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

  struct TaggedLookup {
    std::size_t index = 0;
    std::uint16_t tag = 0;
    bool hit = false;
  };

  // Performing XOR using newest history_length bits to get output_bits bits.
  static std::uint32_t FoldHistory(std::uint64_t history,
                                   std::size_t history_length,
                                   unsigned output_bits);

  std::size_t TaggedIndex(std::size_t table, std::uint32_t pc) const;
  std::uint16_t TaggedTag(std::size_t table, std::uint32_t pc) const;
  TaggedLookup LookupTagged(std::size_t table, std::uint32_t pc) const;

  void updateUsefulBits(TaggedTableEntry & entry, bool actual_taken, bool pred, bool altpred);
  void updatePredCounter(TaggedTableEntry& entry, bool actual_taken);


struct BranchTableEntry{
  bool valid = false;
  std::uint32_t pc = 0;
  std::uint32_t target = 0;
  BranchType type = BranchType::kNotBranch;
};

static constexpr std::size_t kBranchTableEntries = 1024;
std::array<BranchTableEntry, kBranchTableEntries> branchTable_;

};

}

