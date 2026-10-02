#include "execution_unit/internal.h"
#include "execution_unit/helpers.h"
#include <stdexcept>
namespace pipesim {
const char* ExecutionUnitKindName(ExecutionUnitKind kind){switch(kind){case ExecutionUnitKind::kAdd:return "ADD";case ExecutionUnitKind::kSub:return "SUB";case ExecutionUnitKind::kAnd:return "AND";case ExecutionUnitKind::kOr:return "OR";case ExecutionUnitKind::kXor:return "XOR";case ExecutionUnitKind::kControl:return "CONTROL";case ExecutionUnitKind::kAgu:return "AGU";}return "UNKNOWN";}
ExecutionPipeline::ExecutionPipeline(ExecutionUnitKind kind,const ExecutionUnitConfig& config):impl_(new Impl(kind,config)){}
ExecutionPipeline::~ExecutionPipeline()=default;

void ExecutionPipeline::Reset(){
    //Added
    impl_->begun=false;
    impl_->operated=false;
    impl_->next_legal_current=0;
    impl_->next_legal_next=0;
    impl_->current.assign(impl_->current.size(), std::nullopt);
    impl_->next.assign(impl_->next.size(), std::nullopt);
    // throw std::runtime_error("ExecutionPipeline::Reset: TODO(A2--EXEC-01)");
}

void ExecutionPipeline::BeginCycle(){
    //Added
    if(impl_->begun) throw std::runtime_error("Called BeginCycle on ExecutionPipeline twice in a row");
    
    impl_->begun=true;
    impl_->next = impl_->current;
    impl_->next_legal_next=impl_->next_legal_current;
    impl_->operated=false;
    // throw std::runtime_error("ExecutionPipeline::BeginCycle: TODO(A2--EXEC-01)");
}

ExecutionUnitKind ExecutionPipeline::kind()const{return impl_->kind;}
const ExecutionUnitConfig& ExecutionPipeline::config()const{return impl_->config;}

std::optional<ExecutionResult> ExecutionPipeline::OutputIntent()const{
    ExecutionResult result;
    if(!impl_->current.empty() && impl_->current.back().has_value()){
        result = impl_->ResultFor(impl_->current.back().value());
        return result;
    }
    return std::nullopt;
    // throw std::runtime_error("ExecutionPipeline::OutputIntent: TODO(A2--EXEC-02)");
}

//Added
bool ExecutionPipeline::CanAccept(std::uint64_t dispatch_boundary,bool output_will_be_consumed)const{
    // (void)dispatch_boundary;(void)output_will_be_consumed;

    // if(!impl_->begun) throw std::runtime_error("execution pipeline hasnt even begun yet");
    
    if (dispatch_boundary<impl_->next_legal_current) return false;

    auto temp_current = impl_->current;

    if (output_will_be_consumed) temp_current.back().reset();
    

    for (std::size_t i = temp_current.size(); i-- > 1;) {
        if (temp_current[i - 1].has_value() && !temp_current[i].has_value()) {
            temp_current[i] = temp_current[i - 1];
            temp_current[i - 1].reset();
        }
    }

    return !temp_current.front().has_value();

    // throw std::runtime_error("ExecutionPipeline::CanAccept: TODO(A2--EXEC-03)");
    
}

//Added
void ExecutionPipeline::Operate(std::uint64_t current_boundary,const std::optional<ExecutionOperation>& dispatch,bool consume_output){
    if(!impl_->begun||impl_->operated) throw std::runtime_error("execution pipeline Operate lifecycle violation");
    // (void)current_boundary;(void)dispatch;(void)consume_output;
    if(dispatch.has_value()){
        if (!execution_internal::OpcodeBelongsTo(dispatch->instruction.opcode, impl_->kind)){
            throw std::runtime_error("execution operation does not match unit kind");
        }
        if(current_boundary+1<current_boundary) throw std::runtime_error("Current Boundary Overflow");
        if(dispatch->dispatch_boundary != current_boundary+1 || dispatch->dispatch_boundary < impl_->next_legal_current) throw std::runtime_error("Dispatch Boundary Violations");
        

    }

    if(consume_output){
        if( !impl_->current.back().has_value()) throw std::runtime_error("execution pipeline Operate called with consume_output true but no output to consume");
        impl_->next.back().reset();
    } 
    for (std::size_t i = impl_->next.size(); i-- > 1;) {
        if (impl_->next[i - 1].has_value() && !impl_->next[i].has_value()) {
            impl_->next[i] = impl_->next[i - 1];
            impl_->next[i - 1].reset();
        }
    }

    if(dispatch.has_value()){
        if(!impl_->next.front().has_value()) impl_->next.front() = dispatch;
        else throw std::runtime_error("execution pipeline cannot accept dispatch");

        if(dispatch->dispatch_boundary+impl_->config.initiation_interval>dispatch->dispatch_boundary){
            impl_->next_legal_next=dispatch->dispatch_boundary+impl_->config.initiation_interval;
        }
        else throw std::runtime_error("execution pipeline next legal dispatch boundary overflow");
    }

    impl_->operated=true;

    // throw std::runtime_error("ExecutionPipeline::Operate: TODO(A2--EXEC-04)");
}

//Added 
void ExecutionPipeline::Commit(){
    if(!impl_->begun || !impl_->operated) throw std::runtime_error("execution pipeline Commit lifecycle violation");
    impl_->begun=false;
    impl_->operated=false;
    impl_->current=impl_->next;
    impl_->next_legal_current=impl_->next_legal_next;

    // throw std::runtime_error("ExecutionPipeline::Commit: TODO(A2--EXEC-05)");
}

//Added
ExecutionSnapshot ExecutionPipeline::Snapshot()const{
    ExecutionSnapshot snapshot;
    snapshot.kind=impl_->kind;
    snapshot.next_legal_dispatch_boundary=impl_->next_legal_current;
    for(std::size_t i=0;i<impl_->current.size();++i){
        ExecutionStageSnapshot stage_snapshot;
        stage_snapshot.stage=i;
        if(impl_->current[i].has_value()){
            const ExecutionOperation operation=impl_->current[i].value();
            stage_snapshot.valid=true;
            stage_snapshot.sequence=operation.sequence;
            stage_snapshot.pc=operation.instruction.pc;
            stage_snapshot.opcode=OpcodeName(operation.instruction.opcode);
            stage_snapshot.unit=ExecutionUnitKindName(impl_->kind);
            stage_snapshot.dispatch_boundary=operation.dispatch_boundary;
            stage_snapshot.nominal_result_boundary=execution_internal::CheckedResultBoundary(operation.dispatch_boundary,impl_->config.latency);
            stage_snapshot.lhs=operation.lhs;
            stage_snapshot.rhs=operation.rhs;
            stage_snapshot.store_value=operation.store_value;
            stage_snapshot.destination=operation.instruction.rd;
            stage_snapshot.writes_register=execution_internal::WritesRegister(operation.instruction.opcode,operation.instruction.rd);
        }
        snapshot.stages.push_back(stage_snapshot);
    }

    return snapshot;
    // throw std::runtime_error("ExecutionPipeline::Snapshot: TODO(A2--EXEC-05)");
}

//Added
bool ExecutionPipeline::HasWork()const{
    for(const auto& stage:impl_->current){
        if(stage.has_value()) return true;
    }
    return false;
    // throw std::runtime_error("ExecutionPipeline::HasWork: TODO(A2--EXEC-05)");
}

std::uint64_t ExecutionPipeline::SquashAfter(std::uint64_t sequence){
    std::uint64_t removed=0;
    for(auto& operation:impl_->next){
        if(operation&&operation->sequence>sequence){
            operation.reset();++removed;
        }
    }
    return removed;
}
}
