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
  allocation_random_state_ = 1;
  usefulness_aging_count_ = 0;
  clear_usefulness_high_bit_ = true;
  base_.fill(0);
  branchTable_.fill(BranchTableEntry{});

  for (auto& table : taggedTables_) {
    table.fill(TaggedTableEntry{});
  }

  // Reset SC components too
  for (auto& table : SCTables_) {
    table.fill(0);
  }

  SCChoice_.fill(0);
  loop_.fill(LoopEntry{});
  loop_metrics_ = LoopMetrics{};

}

BranchPrediction PredictorOne::Predict(std::uint32_t pc) {
  // (void)pc;
  metrics_.lookups++;

  BranchPrediction prediction {};
  // Save context even on a BTB miss: decode may later reveal a branch.
  // This hashes state only; it does not claim that the PC is a branch.
  for (std::size_t table = 0; table < kNumTaggedTables; ++table) {
    const auto folded = FoldHistory(global_history_,
        tagged_tables_hist_len[table], kIndexBits);
    prediction.information |= static_cast<std::uint64_t>(folded)
        << (kAllocationHistoryShift + table * kIndexBits);
  }
  prediction.information |= std::uint64_t{1} << kAllocationContextValidBit;
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
  TaggedLookup provider_lookup{};

  bool strong_pred = base_[base_ind] == 0 || base_[base_ind] == 3;
  bool unconditional = (bt_entry.type == BranchType::kUnconditional);

  for(int i=static_cast<int>(kNumTaggedTables)-1;i>=0;i--){
    PredictorOne::TaggedLookup lookUp = PredictorOne::LookupTagged(i, pc);

    if(lookUp.hit){
      if(pred==-1){
        pred=i;
        provider_lookup = lookUp;
        provider_taken = taggedTables_[pred][lookUp.index].pred_counter>=0;
        const auto counter = taggedTables_[pred][lookUp.index].pred_counter;
        strong_pred = counter == -4 || counter == 3;
      }
      else{
        altpred=i;
        alternate_taken = taggedTables_[altpred][lookUp.index].pred_counter>=0;
        break;
      }
    }
  }

  //* SC part
  bool final_taken = provider_taken;
  if (!unconditional) {
    const int scSum = GetSCSum(pc, provider_taken, prediction.information);
    const bool scPred = scSum >= 0;
    const int scMagnitude = scSum < 0 ? -scSum : scSum;
    const bool scLowMargin = scMagnitude < kSCThreshold;

    unsigned bucket = 0;
    // Tage and SC conflict and Tage is more confident
    if (scPred != provider_taken && strong_pred && scLowMargin) {
      // 3 range of low margin
      bucket = scMagnitude < kSCThreshold / 3 ? 1 : scMagnitude < 2 * kSCThreshold / 3 ? 2 : 3;
      final_taken = SCChoice_[bucket - 1] >= 0 ? scPred : provider_taken;
    }
    prediction.information |= static_cast<std::uint64_t>(scPred) << kSCMetaBit;
    prediction.information |= static_cast<std::uint64_t>(scLowMargin) << kSCLowMarginBit;
    prediction.information |= static_cast<std::uint64_t>(bucket) << kSCChoiceBucketShift;
  }

  // prediction.taken = bt_entry.type == BranchType::kUnconditional ? true : provider_taken;
  if (!unconditional){
    ++loop_metrics_.table_reads;
    const LoopEntry& entry = loop_[LoopIndex(pc)];
    if (entry.valid && entry.tag == LoopTag(pc) && entry.trip != 0 &&
        entry.confidence >= 7 && entry.current <= entry.trip) {
      final_taken = entry.current == entry.trip ? !entry.direction : entry.direction;
      prediction.information |= std::uint64_t{1} << kLoopSelectedBit;
    }
  } 
  prediction.taken = bt_entry.type == BranchType::kUnconditional ? true : final_taken;

  prediction.confidence = (strong_pred || unconditional) ? 1 : 0;

  (prediction.taken) ? metrics_.predicted_taken++ : metrics_.predicted_not_taken++;

  prediction.information |=
      static_cast<std::uint64_t>(pred + 1)
      | (static_cast<std::uint64_t>(altpred + 1) << 3)
      | (static_cast<std::uint64_t>(provider_taken) << 6)
      | (static_cast<std::uint64_t>(alternate_taken) << 7)
      | (std::uint64_t{1} << 8)
      | (static_cast<std::uint64_t>(provider_lookup.index) << kProviderIndexShift)
      | (static_cast<std::uint64_t>(provider_lookup.tag) << kProviderTagShift);
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

void PredictorOne::Update(const BranchResult& result, const BranchPrediction& prediction) {
  if (result.type != BranchType::kConditional && result.type != BranchType::kUnconditional) {
    throw std::invalid_argument("PredictorOne::Update requires a branch");
  }

  const auto info = prediction.information;

  // Recover SC meta and low margin for sc confidence
  const unsigned choiceBucket = static_cast<unsigned>((info >> kSCChoiceBucketShift) & 0x3u);
  const bool scPred = ((info >> kSCMetaBit) & 1u) != 0;
  const bool scLowMargin = ((info >> kSCLowMarginBit) & 1u) != 0;

  const bool has_metadata = ((info >> 8) & 1u) != 0;

  const int provider = static_cast<int>(info & 7u) - 1;
  const int alternate = static_cast<int>((info >> 3) & 7u) - 1;

  const bool provider_taken = ((info >> 6) & 1u) != 0;
  const bool alternate_taken = ((info >> 7) & 1u) != 0;

  if (has_metadata && (provider >= static_cast<int>(kNumTaggedTables) || alternate >= static_cast<int>(kNumTaggedTables) || (alternate >= 0 && alternate >= provider))) {
    throw std::invalid_argument("invalid saved TAGE provider metadata");
  }

  const std::size_t provider_index = (info >> kProviderIndexShift) & (kTableEntries - 1);
  const std::uint16_t provider_tag = static_cast<std::uint16_t>((info >> kProviderTagShift) & kTagMask);

  metrics_.updates++;

  if (result.type == BranchType::kConditional) {
    const bool loop_selected = ((info >> kLoopSelectedBit) & 1u) != 0;
    if (loop_selected) {
      ++loop_metrics_.selected;
      if (prediction.taken == result.taken) ++loop_metrics_.correct;
      else ++loop_metrics_.incorrect;
    }
    UpdateLoop(result.pc, result.taken);
  }

  BranchTableEntry bt_entry = {true, result.pc, result.target, result.type };
  PredictorOne::branchTable_[(result.pc >> 2) & (kBranchTableEntries - 1)] = bt_entry;

  const bool predicted_taken  = prediction.branch_found && prediction.taken;

  if(result.taken != predicted_taken){
    metrics_.direction_mispredictions++;
  }
  else if(result.taken && result.target != prediction.target){
    metrics_.target_mispredictions++;
  }
  else{
    metrics_.correct_predictions++;
  }

  if (result.type == BranchType::kConditional) {
    // SC counter updation logic
    if (has_metadata && choiceBucket != 0) {
      auto& choice = SCChoice_[choiceBucket - 1];

      if (scPred == result.taken) {
        if (choice < kSCChoiceMax) choice++;
      } else {
        if (choice > kSCChoiceMin) choice--;
      }
    }

    bool isSCWrong = scPred != result.taken;
    if (isSCWrong || scLowMargin) {
      for (std::size_t table = 0; table < kSCNumTables; table++) {
        const std::size_t index = GetSCIndex(result.pc, provider_taken, info, table);
        auto& counter = SCTables_[table][index];
        if (result.taken) {
          if (counter < kCounterMax) {
            counter++;
          }
        } else {
          if (counter > kCounterMin) {
            counter--;
          }
        }
      }
    } 

    if (!has_metadata || provider == -1) {

      // A BTB miss has no provider metadata. Train only the PC-indexed base.
      auto& counter = base_[(result.pc >> 2) & (kBaseTableEntries - 1)];
      if (result.taken) {
        if (counter < 3) ++counter;
      } else {
        if (counter > 0) --counter;
      }
    } else {
      auto& entry = taggedTables_[provider][provider_index];

      // History may have changed since prediction. Using the saved address and skipped training if the entry has since been invalidated/replaced.
      if (entry.valid && entry.tag == provider_tag) {
        updateUsefulBits(entry, result.taken, provider_taken, alternate_taken);
        updatePredCounter(entry, result.taken);
      }
    }

    // Done so that TAGE learns from its mistake
    const bool tage_taken = has_metadata && provider_taken;
    // changed from predicted_taken to tage_taken, whihc was the original tage prediction
    if (result.taken != tage_taken) {
      // Target-only errors and unconditional jumps do not allocate.
      AllocateTagged(result, info, has_metadata ? provider : -1);
    }
  }

  AgeUsefulness();

  // appending the outcome after training.
  const std::uint16_t address_bit = (result.pc >> 2) & 1u;
  path_history_ = static_cast<std::uint16_t>((static_cast<std::uint32_t>(path_history_) << 1) | address_bit);
  global_history_ = (global_history_ << 1) | std::uint64_t(result.taken);

  // throw std::runtime_error("PredictorOne::Update: TODO(PA3-PREDICTOR-ONE)");
}

std::uint64_t PredictorOne::StorageBits() const {
  // Logical hardware storage.
  const std::uint64_t base_bits = std::uint64_t{kBaseTableEntries} * 2;
  const std::uint64_t tagged_bits = std::uint64_t{kNumTaggedTables}
      * kTableEntries * (1 + kTagBits + 3 + 2); // valid, tag, prediction, useful
  const std::uint64_t btb_bits = std::uint64_t{kBranchTableEntries}
      * (1 + 32 + 32 + 2); // valid, full PC, target, four-valued BranchType

  // The aging counter ranges from zero through period - 1.
  std::uint64_t aging_counter_bits = 0;
  for (auto maximum = kUsefulnessAgingPeriod - 1; maximum != 0; maximum >>= 1) {
    ++aging_counter_bits;
  }
  const std::uint64_t history_bits = 64 + 16; // global and path history
  const std::uint64_t control_bits = 32 + aging_counter_bits + 1; // RNG, count, phase

  // SC
  const std::uint64_t sc_bits = std::uint64_t{kSCNumTables} * kSCNumEntries * kCounterBits + 3 * 5;
  // valid + 10-bit PC tag + two 10-bit iteration counts + 4-bit
  // confidence + 4-bit replacement age + one loop direction per entry.
  const std::uint64_t loop_bits = kLoopEntries * (1 + 10 + 10 + 10 + 4 + 4 + 1);
  return base_bits + tagged_bits + btb_bits + history_bits + control_bits + sc_bits + loop_bits;

  // throw std::runtime_error("PredictorOne::StorageBits: TODO(PA3-PREDICTOR-ONE)");
}

const PredictorMetrics& PredictorOne::Metrics() const {
  return metrics_;
}

std::size_t PredictorOne::LoopIndex(std::uint32_t pc) {
  return ((pc >> 2) ^ (pc >> 8)) & (kLoopEntries - 1);
}

std::uint16_t PredictorOne::LoopTag(std::uint32_t pc) {
  return static_cast<std::uint16_t>((pc >> 2) & 1023u);
}

void PredictorOne::UpdateLoop(std::uint32_t pc, bool taken) {
  LoopEntry& entry = loop_[LoopIndex(pc)];
  ++loop_metrics_.table_reads;
  const auto write = [&]() { ++loop_metrics_.table_writes; };
  if (!entry.valid || entry.tag != LoopTag(pc)) {
    if (entry.valid && entry.age != 0) {
      --entry.age;
      write();
      return;
    }
    entry = LoopEntry{};
    entry.valid = true;
    entry.tag = LoopTag(pc);
    entry.direction = taken;
    entry.age = 1;
    entry.current = 1;  // The first observation is a continuation candidate.
    write();
    return;
  }

  if (taken == entry.direction) {
    if (entry.current == kLoopMaxTrip) {
      // A saturated counter cannot identify an exact trip length.
      entry.trip = 0;
      entry.confidence = 0;
    } else {
      ++entry.current;
    }
  } else {
    if (entry.current != 0 && entry.current == entry.trip) {
      if (entry.confidence < 15) ++entry.confidence;
      if (entry.age < 15) ++entry.age;
    } else {
      entry.trip = entry.current;
      entry.confidence = 0;
      if (entry.age > 0) --entry.age;
    }
    entry.current = 0;
  }
  write();
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

void PredictorOne::AgeUsefulness() {
  if (usefulness_aging_count_ < kUsefulnessAgingPeriod - 1) {
    ++usefulness_aging_count_;
    return;
  }

  usefulness_aging_count_ = 0;
  // First clear bit 1 (keep bit 0), then clear bit 0 (keep bit 1).
  const std::uint8_t keep_mask = clear_usefulness_high_bit_ ? 1u : 2u;
  for (auto& table : taggedTables_) {
    for (auto& entry : table) {
      entry.useful &= keep_mask;
    }
  }
  clear_usefulness_high_bit_ = !clear_usefulness_high_bit_;
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
  return IndexFromFoldedHistory(pc, FoldHistory(global_history_, length, kIndexBits));
}

std::uint16_t PredictorOne::TaggedTag(std::size_t table,
                                     std::uint32_t pc) const {
  const std::size_t length = tagged_tables_hist_len.at(table);
  return TagFromFoldedHistory(pc, FoldHistory(global_history_, length, kIndexBits));
}

std::size_t PredictorOne::IndexFromFoldedHistory(
    std::uint32_t pc, std::uint32_t folded_history) {
  const std::uint32_t word_pc = pc >> 2;
  return (word_pc ^ (word_pc >> kIndexBits) ^ folded_history)
      & (kTableEntries - 1);
}

std::uint16_t PredictorOne::TagFromFoldedHistory(
    std::uint32_t pc, std::uint32_t folded_history) {
  const std::uint32_t word_pc = pc >> 2;
  // Using a different mixing function from the index to avoid equal indices resulting in equal tags. Partial-tag collisions can still occur.
  const std::uint32_t hash = word_pc ^ (word_pc >> kTagBits)
      ^ (folded_history << 1);
  return static_cast<std::uint16_t>(hash & kTagMask);
}

std::uint32_t PredictorOne::NextAllocationRandom() {
  // Nonzero-seeded xorshift32; reset makes experiments reproducible.
  allocation_random_state_ ^= allocation_random_state_ << 13;
  allocation_random_state_ ^= allocation_random_state_ >> 17;
  allocation_random_state_ ^= allocation_random_state_ << 5;
  return allocation_random_state_;
}

void PredictorOne::AllocateTagged(const BranchResult& result,
                                  std::uint64_t information, int provider) {
  if (((information >> kAllocationContextValidBit) & 1u) == 0) return;

  std::array<TaggedLookup, kNumTaggedTables> candidates{};
  std::array<std::uint32_t, kNumTaggedTables> weights{};
  std::uint32_t total_weight = 0;
  for (int table = provider + 1; table < static_cast<int>(kNumTaggedTables); ++table) {
    const auto folded = static_cast<std::uint32_t>((information >> (kAllocationHistoryShift + table * kIndexBits)) & (kTableEntries - 1));
    auto& candidate = candidates[table];
    candidate.index = IndexFromFoldedHistory(result.pc, folded);
    candidate.tag = TagFromFoldedHistory(result.pc, folded);
    const auto& entry = taggedTables_[table][candidate.index];
    if (!entry.valid || entry.useful == 0) {
      // Each successive shorter history has twice the allocation weight.
      weights[table] = std::uint32_t{1} << (kNumTaggedTables - 1 - table);
      total_weight += weights[table];
    }
  }

  if (total_weight == 0) {
    // All candidates are useful. Reduce their protection, but do not
    // allocate this time even if a counter reaches zero.
    for (int table = provider + 1; table < static_cast<int>(kNumTaggedTables); ++table) {
      auto& entry = taggedTables_[table][candidates[table].index];
      if (entry.useful > 0) --entry.useful;
    }
    return;
  }

  std::uint32_t choice = NextAllocationRandom() % total_weight;
  for (int table = provider + 1; table < static_cast<int>(kNumTaggedTables); ++table) {
    if (choice >= weights[table]) {
      choice -= weights[table];
      continue;
    }
    const auto& candidate = candidates[table];
    auto& entry = taggedTables_[table][candidate.index];
    entry.valid = true;
    entry.tag = candidate.tag;
    entry.pred_counter = result.taken ? 0 : -1;
    entry.useful = 0;
    return; // At most one allocation per misprediction.
  }
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

//* SC related implementations

std::size_t PredictorOne::GetSCIndex(std::uint32_t pc, bool isTAGETaken, std::uint64_t information, std::size_t table) {
  std::uint32_t wpc = pc >> 2;
  std::uint32_t val;

  if (table < 2) {
    val = wpc ^ static_cast<std::uint32_t>(table);
  } else {
    std::uint32_t fold = 0;
    if (table == 2) fold = (information >> 26) & 0x0Fu;
    else if (table == 3) fold = (information >> 26) & 0xFFu;
    else if (table == 4) fold = (information >> 34) & 0xFFu;
    else fold = (information >> 42) & 0xFFu;
    val = wpc ^ fold;
  }

  std::uint32_t hash = val & 0x3F;
  return (hash << 1) | static_cast<std::uint32_t>(isTAGETaken);
}

int PredictorOne::GetSCSum(std::uint32_t pc, bool isTAGETaken, std::uint64_t information) {
  int sum = 0;
  for (std::size_t table = 0; table < 6; table++) {
    std::size_t index = GetSCIndex(pc, isTAGETaken, information, table);
    sum += SCTables_[table][index];
  }
  sum += isTAGETaken ? 2 * static_cast<int>(kSCNumTables) : -2 * static_cast<int>(kSCNumTables);
  return sum;
}

}
