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
  throw std::runtime_error("SpeculationController::Reset: TODO(PA3-FRAMEWORK-01)");
}

bool SpeculationController::CanFetch() const {
  throw std::runtime_error("SpeculationController::CanFetch: TODO(PA3-FRAMEWORK-01)");
}

bool SpeculationController::CanDispatch(
    std::uint64_t sequence,
    const Instruction& instruction) const {
  (void)sequence;
  (void)instruction;
  throw std::runtime_error("SpeculationController::CanDispatch: TODO(PA3-FRAMEWORK-02)");
}

void SpeculationController::AddCheckpoint(
    const BranchCheckpoint& checkpoint) {
  (void)checkpoint;
  throw std::runtime_error("SpeculationController::AddCheckpoint: TODO(PA3-FRAMEWORK-03)");
}

bool SpeculationController::IsSpeculative(std::uint64_t sequence) const {
  (void)sequence;
  throw std::runtime_error("SpeculationController::IsSpeculative: TODO(PA3-FRAMEWORK-03)");
}

std::uint64_t SpeculationController::SpeculationDepth(
    std::uint64_t sequence) const {
  (void)sequence;
  throw std::runtime_error("SpeculationController::SpeculationDepth: TODO(PA3-FRAMEWORK-03)");
}

RecoveryDecision SpeculationController::Resolve(const BranchResult& result) {
  (void)result;
  throw std::runtime_error("SpeculationController::Resolve: TODO(PA3-FRAMEWORK-04)");
}

void SpeculationController::RemoveYoungerCheckpoints(std::uint64_t sequence) {
  (void)sequence;
  throw std::runtime_error("SpeculationController::RemoveYoungerCheckpoints: TODO(PA3-FRAMEWORK-05)");
}

std::size_t SpeculationController::CheckpointCount() const {
  return impl_->checkpoints.size();
}

}
