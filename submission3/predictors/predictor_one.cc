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
  branchTable_.fill(BranchTableEntry{});

  for (auto& table : taggedTables_) {
    table.fill(TaggedTableEntry{});
  }

}

BranchPrediction PredictorOne::Predict(std::uint32_t pc) {
  // (void)pc;
  metrics_.lookups++;

  BranchPrediction prediction {};
  const std::uint32_t bt_ind = (pc >> 2) & (kBranchTableEntries - 1);
  const BranchTableEntry& bt_entry = branchTable_[bt_ind];

  if(!bt_entry.valid || bt_entry.pc != pc){
    metrics_.branches_not_found++;
    metrics_.predicted_not_taken++;
    return prediction;
  }
  else{
    metrics_.branches_found++;
    prediction.branch_found = true;
    prediction.target = bt_entry.target;
  }


  const std::size_t base_ind = (pc >>2) & (kBaseTableEntries -1);
  const bool base_taken = base_[base_ind] >=2; 

  bool provider_taken = base_taken;
  bool alternate_taken = base_taken;

  int pred=-1,altpred=-1;

  bool strong_pred = base_[base_ind] == 0 || base_[base_ind] == 3;
  bool unconditional = (bt_entry.type == BranchType::kUnconditional);

  for(int i=3;i>=0;i--){
    PredictorOne::TaggedLookup lookUp = PredictorOne::LookupTagged(i, pc);

    if(lookUp.hit){
      if(pred==-1){
        pred=i;
        provider_taken = taggedTables_[pred][lookUp.index].pred_counter>=0;
        strong_pred = (abs(taggedTables_[pred][lookUp.index].pred_counter) >=3 );
      }
      else{
        altpred=i;
        alternate_taken = taggedTables_[altpred][lookUp.index].pred_counter>=0;
        break;
      }
    }
  }

  prediction.taken = bt_entry.type == BranchType::kUnconditional ? true : provider_taken;

  prediction.confidence = (strong_pred || unconditional) ? 1 : 0;

  (prediction.taken) ? metrics_.predicted_taken++ : metrics_.predicted_not_taken++;

  prediction.information = static_cast<std::uint64_t>(pred + 1) | (static_cast<std::uint64_t>(altpred + 1) << 3) | (static_cast<std::uint64_t>(provider_taken) << 6) | (static_cast<std::uint64_t>(alternate_taken) << 7) | (std::uint64_t{1} << 8);
  return prediction;

  // throw std::runtime_error("PredictorOne::Predict: TODO(PA3-PREDICTOR-ONE)");
}

void PredictorOne::ObserveInstruction(
    const Instruction& instruction,
    const BranchPrediction& prediction) {
  // (void)instruction;
  // (void)prediction;

  bool is_branch = (instruction.opcode == Opcode::kBeq || instruction.opcode == Opcode::kBne || instruction.opcode == Opcode::kJ);

  if(prediction.branch_found && !is_branch) metrics_.false_branch_predictions++;
  if(!prediction.branch_found && is_branch) metrics_.missed_branches++;

  // throw std::runtime_error("PredictorOne::ObserveInstruction: TODO(PA3-PREDICTOR-ONE)");
  
}

void PredictorOne::Update(const BranchResult& result,
                          const BranchPrediction& prediction) {
  const std::uint16_t address_bit = (result.pc >> 2) & 1u;
  path_history_ = static_cast<std::uint16_t>((static_cast<std::uint32_t>(path_history_) << 1) | address_bit);
  global_history_ = static_cast<std::uint64_t>((global_history_ << 1) | std::uint64_t(result.taken));

  // throw std::runtime_error("PredictorOne::Update: TODO(PA3-PREDICTOR-ONE)");
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
  if (history_length > 64 || output_bits == 0 || output_bits > 32) throw std::invalid_argument("invalid history fold width");

  std::uint32_t folded = 0;
  for (std::size_t bit = 0; bit < history_length; ++bit) {
    folded ^= static_cast<std::uint32_t>((history >> bit) & 1u) << (bit % output_bits);
  }
  return folded;
}

std::size_t PredictorOne::TaggedIndex(std::size_t table,
                                     std::uint32_t pc) const {
  const std::size_t length = tagged_tables_hist_len.at(table);
  const std::uint32_t word_pc = pc >> 2;


  const std::uint32_t hash = word_pc ^ (word_pc >> kIndexBits)^ FoldHistory(global_history_, length, kIndexBits);

  return hash & (kTableEntries - 1);
}

std::uint16_t PredictorOne::TaggedTag(std::size_t table,
                                     std::uint32_t pc) const {
  const std::size_t length = tagged_tables_hist_len.at(table);
  const std::uint32_t word_pc = pc >> 2;

  // Using a different mixing function from the index to avoid equal indices resulting in equal tags. Partial-tag collisions can still occur.
  const std::uint32_t hash = word_pc ^ (word_pc >> kTagBits) ^ (FoldHistory(global_history_, length, kTagBits - 1) << 1);

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
