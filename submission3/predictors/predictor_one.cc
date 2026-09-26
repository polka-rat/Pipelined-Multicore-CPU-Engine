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
