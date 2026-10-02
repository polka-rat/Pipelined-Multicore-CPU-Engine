#include "speculation/internal.h"

#include <stdexcept>

namespace pipesim {

SpeculationController::SpeculationController(std::size_t maximum_checkpoints)
    : impl_(new Impl(maximum_checkpoints)) {
  if (maximum_checkpoints == 0) {
    throw std::runtime_error("maximum checkpoints must be positive");
  }
}

SpeculationController::~SpeculationController() = default;

void SpeculationController::Reset() {
  impl_->checkpoints.clear();
}

bool SpeculationController::CanFetch() const {
  return impl_->checkpoints.size() < impl_->maximum_checkpoints;
}

bool SpeculationController::CanDispatch(
    std::uint64_t sequence,
    const Instruction& instruction) const {
  if (!IsSpeculative(sequence)) return true;

  switch (instruction.opcode) {
    case Opcode::kAdd:
    case Opcode::kSub:
    case Opcode::kAnd:
    case Opcode::kOr:
    case Opcode::kXor:
    case Opcode::kAddi:
      return true;
    default:
      return false;
  }
}

void SpeculationController::AddCheckpoint(
    const BranchCheckpoint& checkpoint) {
  impl_->checkpoints.push_back(checkpoint);
}

bool SpeculationController::IsSpeculative(std::uint64_t sequence) const {
  for (const auto& checkpoint : impl_->checkpoints) {
    if (checkpoint.sequence < sequence) return true;
  }
  return false;
}

std::uint64_t SpeculationController::SpeculationDepth(
    std::uint64_t sequence) const {
  std::uint64_t depth = 0;
  for (const auto& checkpoint : impl_->checkpoints) {
    if (checkpoint.sequence < sequence) ++depth;
  }
  return depth;
}

RecoveryDecision SpeculationController::Resolve(const BranchResult& result) {
  for (auto it = impl_->checkpoints.begin();
       it != impl_->checkpoints.end(); ++it) {
    if (it->sequence != result.sequence) continue;

    RecoveryDecision decision;
    decision.branch_sequence = result.sequence;
    decision.correct_next_pc =
        result.taken ? result.target : result.pc + 4;
    decision.mispredicted =
        it->predicted_next_pc != decision.correct_next_pc;

    impl_->checkpoints.erase(it);
    return decision;
  }

  throw std::runtime_error("Resolve: no checkpoint for branch sequence");
}

void SpeculationController::RemoveYoungerCheckpoints(
    std::uint64_t sequence) {
  auto it = impl_->checkpoints.begin();
  while (it != impl_->checkpoints.end()) {
    if (it->sequence > sequence) {
      it = impl_->checkpoints.erase(it);
    } else {
      ++it;
    }
  }
}

std::size_t SpeculationController::CheckpointCount() const {
  return impl_->checkpoints.size();
}

}  // namespace pipesim
