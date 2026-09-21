#include "predictors/predictor_one.h"

#include <stdexcept>

namespace pipesim {

std::string PredictorOne::Name() const {
  return "PAPER_ONE";
}

void PredictorOne::Reset() {
  metrics_ = PredictorMetrics();

  global_history_ = 0;
  path_history_ = 0;
  base_.fill(0);
  for (auto& table : taggedTables_) {
    table.fill(TaggedTableEntry{});
  }
}

BranchPrediction PredictorOne::Predict(std::uint32_t pc) {
  (void)pc;
  throw std::runtime_error("PredictorOne::Predict: TODO(PA3-PREDICTOR-ONE)");
}

void PredictorOne::ObserveInstruction(
    const Instruction& instruction,
    const BranchPrediction& prediction) {
  (void)instruction;
  (void)prediction;
  throw std::runtime_error("PredictorOne::ObserveInstruction: TODO(PA3-PREDICTOR-ONE)");
}

void PredictorOne::Update(const BranchResult& result,
                          const BranchPrediction& prediction) {
  (void)result;
  (void)prediction;

  const std::uint16_t address_bit = (result.pc >> 2) & 1u;
  path_history_ = static_cast<std::uint16_t>(
    (static_cast<std::uint32_t>(path_history_) << 1) | address_bit);

  global_history_ = static_cast<std::uint64_t>((global_history_ << 1) | std::uint64_t(result.taken));
  throw std::runtime_error("PredictorOne::Update: TODO(PA3-PREDICTOR-ONE)");
}

std::uint64_t PredictorOne::StorageBits() const {
  throw std::runtime_error("PredictorOne::StorageBits: TODO(PA3-PREDICTOR-ONE)");
}

const PredictorMetrics& PredictorOne::Metrics() const {
  return metrics_;
}

void PredictorOne::updateUsefulBits(PredictorOne::TaggedTableEntry & entry, bool actual_taken, bool pred, bool altpred) {
  if(pred==altpred) return;

  if(pred==actual_taken) {
    if(entry.useful<3) entry.useful++;
  }
  else {
    if(entry.useful>0) entry.useful--;
  }
}

void PredictorOne::updatePredCounter(TaggedTableEntry& entry, bool actual_taken){
  if(actual_taken){
    if(entry.pred_counter<3) entry.pred_counter++;
  }
  else{
    if(entry.pred_counter>-4) entry.pred_counter--;
  }
}

std::uint32_t PredictorOne::FoldHistory(std::uint64_t history,
                                       std::size_t history_length,
                                       unsigned output_bits) {
  if (history_length > 64 || output_bits == 0 || output_bits > 32) {
    throw std::invalid_argument("invalid history fold width");
  }
  std::uint32_t folded = 0;
  for (std::size_t bit = 0; bit < history_length; ++bit) {
    folded ^= static_cast<std::uint32_t>((history >> bit) & 1u)
              << (bit % output_bits);
  }
  return folded;
}

std::size_t PredictorOne::TaggedIndex(std::size_t table,
                                     std::uint32_t pc) const {
  const std::size_t length = tagged_tables_hist_len.at(table);
  const std::uint32_t word_pc = pc >> 2;
  // Simple XOR-fold hashes chosen for this implementation, not a literal
  // reproduction of a specific TAGE-SC-L variant's indexing functions.
  const std::uint32_t hash = word_pc ^ (word_pc >> kIndexBits)
      ^ FoldHistory(global_history_, length, kIndexBits)
      ^ FoldHistory(path_history_, 16, kIndexBits);
  return hash & (kTableEntries - 1);
}

std::uint16_t PredictorOne::TaggedTag(std::size_t table,
                                     std::uint32_t pc) const {
  const std::size_t length = tagged_tables_hist_len.at(table);
  const std::uint32_t word_pc = pc >> 2;
  // Use a different mixing function from the index: equal indices should
  // not force equal tags. Partial-tag collisions can still occur.
  const std::uint32_t hash = word_pc ^ (word_pc >> kTagBits)
      ^ FoldHistory(global_history_, length, kTagBits)
      ^ (FoldHistory(global_history_, length, kTagBits - 1) << 1)
      ^ FoldHistory(path_history_, 16, kTagBits);
  return static_cast<std::uint16_t>(hash & kTagMask);
}

PredictorOne::TaggedLookup PredictorOne::LookupTagged(
    std::size_t table, std::uint32_t pc) const {
  TaggedLookup lookup;
  lookup.index = TaggedIndex(table, pc);
  lookup.tag = TaggedTag(table, pc);
  // Read exactly one entry; do not search the table for another matching tag.
  const TaggedTableEntry& entry = taggedTables_.at(table)[lookup.index];
  lookup.hit = entry.valid && entry.tag == lookup.tag;
  return lookup;
}
}
