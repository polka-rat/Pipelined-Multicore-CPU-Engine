#include "execution_unit/internal.h"
#include "execution_unit/helpers.h"
#include <stdexcept>
namespace pipesim {


    ExecutionPipeline::Impl::Impl(ExecutionUnitKind unit_kind,const ExecutionUnitConfig& unit_config) : kind(unit_kind),config(unit_config){
        if(static_cast<std::size_t>(kind)>static_cast<std::size_t>(ExecutionUnitKind::kAgu)) throw std::runtime_error("invalid execution-unit kind");
        if(config.initiation_interval==0) throw std::runtime_error("zero execution initiation interval");

        // Added
        const std::size_t stage_count =static_cast<std::size_t>(config.latency) + 1;
        current.assign(stage_count, std::nullopt);
        next.assign(stage_count, std::nullopt);

        // throw std::runtime_error("ExecutionPipeline::Impl: TODO(A2--EXEC-01)");
    }
    
    ExecutionResult ExecutionPipeline::Impl::ResultFor(const ExecutionOperation& operation) const{
        ExecutionResult result;
        result.sequence=operation.sequence;
        result.instruction=operation.instruction;
        result.nominal_result_boundary=execution_internal::CheckedResultBoundary(operation.dispatch_boundary,config.latency);
        result.fetch_cycle=operation.fetch_cycle;
        result.predictions=operation.predictions;

        if (!execution_internal::OpcodeBelongsTo(operation.instruction.opcode, kind)){
            throw std::runtime_error("execution operation does not match unit kind");
        }

        // Added
        switch(kind){
            case ExecutionUnitKind::kAdd:
                result.value=operation.lhs+operation.rhs;
                return result;
            case ExecutionUnitKind::kSub:
                result.value=operation.lhs-operation.rhs;
                return result;
            case ExecutionUnitKind::kAnd:
                result.value=operation.lhs&operation.rhs;
                return result;
            case ExecutionUnitKind::kOr:
                result.value=operation.lhs|operation.rhs;
                return result;
            case ExecutionUnitKind::kXor:
                result.value=operation.lhs ^ operation.rhs;
                return result;
            case ExecutionUnitKind::kControl:
                if(operation.instruction.opcode==Opcode::kBeq){
                    result.redirect=operation.lhs==operation.rhs;
                    result.next_pc=result.redirect?operation.instruction.pc+4+static_cast<std::uint32_t>(operation.instruction.immediate):operation.instruction.pc+4;
                    return result;
                }else if(operation.instruction.opcode==Opcode::kBne){
                    result.redirect=(operation.lhs!=operation.rhs);
                    result.next_pc=result.redirect?operation.instruction.pc+4+static_cast<std::uint32_t>(operation.instruction.immediate):operation.instruction.pc+4;
                    return result;
                }else if(operation.instruction.opcode==Opcode::kJ){
                    result.redirect=true;
                    result.next_pc=operation.instruction.pc+4+static_cast<std::uint32_t>(operation.instruction.immediate);
                    return result;
                }else if(operation.instruction.opcode==Opcode::kHalt){
                    result.halt=true;
                    return result;
                }
                // throw std::runtime_error("ExecutionPipeline::ResultFor: unknown control opcode");

            case ExecutionUnitKind::kAgu:
                result.memory_write=(operation.instruction.opcode==Opcode::kSw);
                result.memory=(operation.instruction.opcode==Opcode::kLw || operation.instruction.opcode==Opcode::kSw);

                result.address=operation.lhs+operation.rhs;  //DOUBT
                result.store_value=operation.store_value;
  
                return result;

            default:
                throw std::runtime_error("ExecutionPipeline::ResultFor: unknown execution-unit kind");
        }
        // throw std::runtime_error("ExecutionPipeline::ResultFor: TODO(A2--EXEC-02)");
    }
}
