#include "predictors/predictor_one.h"

#include <stdexcept>

namespace pipesim {

std::string PredictorOne::Name() const {
  return "PAPER_ONE";
}

void PredictorOne::Reset() {
  metrics_ = PredictorMetrics();
  throw std::runtime_error("PredictorOne::Reset: TODO(PA3-PREDICTOR-ONE)");
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
  throw std::runtime_error("PredictorOne::Update: TODO(PA3-PREDICTOR-ONE)");
}

std::uint64_t PredictorOne::StorageBits() const {
  throw std::runtime_error("PredictorOne::StorageBits: TODO(PA3-PREDICTOR-ONE)");
}

const PredictorMetrics& PredictorOne::Metrics() const {
  return metrics_;
}

}
