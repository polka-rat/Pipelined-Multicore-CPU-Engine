#include "execution_unit/internal.h"
#include "execution_unit/helpers.h"
#include <stdexcept>
namespace pipesim {
ExecutionPipeline::Impl::Impl(ExecutionUnitKind unit_kind,const ExecutionUnitConfig& unit_config):kind(unit_kind),config(unit_config){if(static_cast<std::size_t>(kind)>static_cast<std::size_t>(ExecutionUnitKind::kAgu))throw std::runtime_error("invalid execution-unit kind");if(config.initiation_interval==0)throw std::runtime_error("zero execution initiation interval");throw std::runtime_error("ExecutionPipeline::Impl: TODO(A2-EXEC-01)");}
ExecutionResult ExecutionPipeline::Impl::ResultFor(const ExecutionOperation& operation)const{ExecutionResult result;result.sequence=operation.sequence;result.instruction=operation.instruction;result.nominal_result_boundary=execution_internal::CheckedResultBoundary(operation.dispatch_boundary,config.latency);result.fetch_cycle=operation.fetch_cycle;result.predictions=operation.predictions;switch(kind){case ExecutionUnitKind::kAdd:result.value=operation.lhs+operation.rhs;return result;case ExecutionUnitKind::kSub:case ExecutionUnitKind::kAnd:case ExecutionUnitKind::kOr:case ExecutionUnitKind::kXor:case ExecutionUnitKind::kControl:case ExecutionUnitKind::kAgu:break;}throw std::runtime_error("ExecutionPipeline::ResultFor: TODO(A2-EXEC-02)");}
}
