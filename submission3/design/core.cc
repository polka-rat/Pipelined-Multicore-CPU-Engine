#include "design/core.h"
#include "design/instruction_helpers.h"
#include "branch_prediction/internal.h"
#include "speculation.h"
#include <limits>
#include <stdexcept>
namespace pipesim::design_internal {
Core::Core(Machine* machine, 
           const std::array<ExecutionPipeline*,7>& pipelines, 
           MainControl* main_control, 
           HazardControl* hazard_control, 
           BranchPredictionSystem* branch_prediction, 
           SpeculationController* speculation_control
        ): machine_(machine), pipelines_(pipelines), main_control_(main_control), 
        hazard_control_(hazard_control), branch_prediction_(branch_prediction), 
        speculation_control_(speculation_control) 
{
    if (machine_== nullptr || main_control_ == nullptr || hazard_control_ == nullptr || 
        branch_prediction_ == nullptr || speculation_control_ == nullptr) 
            throw std::runtime_error("CPU core received a null component");
    for (std::size_t i=0; i<pipelines_.size(); ++i) 
        if(pipelines_[i] == nullptr || pipelines_[i]->kind()!=static_cast<ExecutionUnitKind>(i)) 
            throw std::runtime_error("CPU core execution-unit registration mismatch");
}
void Core::ResetRegister(RegisterKind kind){ if(kind!=RegisterKind::kPc)return; pc_=machine_->EntryPoint(); next_pc_=pc_; fetch_stopped_=false; next_fetch_stopped_=false; if_id_=FrontendLatch(); next_if_id_=FrontendLatch(); current_boundary_=0; cycle_begun_=false; for(auto& s:snapshots_)s=StageSnapshot(); }
void Core::CommitRegister(RegisterKind kind){ if(kind!=RegisterKind::kPc)return; if(!cycle_begun_||!pipelines_operated_) throw std::runtime_error("CPU core commit without a complete operate pass"); pc_=next_pc_; fetch_stopped_=next_fetch_stopped_; if_id_=next_if_id_; if(current_boundary_==std::numeric_limits<std::uint64_t>::max()) throw std::runtime_error("CPU boundary counter overflow"); ++current_boundary_; cycle_begun_=false; }
std::string Core::RegisterSnapshot(RegisterKind kind) const { std::ostringstream output; switch(kind){ case RegisterKind::kPc: output<<"pc=0x"<<std::hex<<pc_<<std::dec<<" stopped="<<(fetch_stopped_?1:0); return output.str(); case RegisterKind::kIfId: if(!if_id_.valid)return "EMPTY"; output<<OpcodeName(if_id_.fetched.instruction.opcode)<<"@0x"<<std::hex<<if_id_.fetched.instruction.pc<<std::dec<<"#"<<if_id_.fetched.sequence; return output.str(); case RegisterKind::kIdEx:return "EXECUTION_PIPELINES"; case RegisterKind::kExMem:return "AGU_HANDOFF"; case RegisterKind::kMemWb:return "COMPLETION_QUEUE";} return "EMPTY"; }
void Core::ResetStage(StageRole role){ snapshots_[static_cast<std::size_t>(role)]=StageSnapshot(); }
StageSnapshot Core::Snapshot(StageRole role) const{return snapshots_[static_cast<std::size_t>(role)];}
// void Core::Writeback(){ BeginCycle(); throw std::runtime_error("Core::Writeback: TODO(A2-DESIGN-06)"); }
    void Core::Writeback() { 
        BeginCycle(); 
        StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kWriteback)];
        snapshot = StageSnapshot();
        snapshot.action = "EMPTY";
        
        Retirement retirement;
        if (!machine_->PeekRetirement(&retirement)) return;

        snapshot.valid = true;
        snapshot.pc = retirement.instruction.pc;
        snapshot.opcode = OpcodeName(retirement.instruction.opcode);

        machine_->ApplyRetirement(retirement);

        // throw std::runtime_error("Core::Writeback: TODO(A2--DESIGN-06)"); 
    }
// void Core::Memory(){ StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kMemory)]; snapshot=StageSnapshot(); snapshot.action="EMPTY"; throw std::runtime_error("Core::Memory: TODO(A2-DESIGN-05)"); }
void Core::Memory(){ 
        StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kMemory)]; 
        snapshot=StageSnapshot(); 
        snapshot.action="EMPTY"; 

        auto result = pipelines_[static_cast<std::size_t>(ExecutionUnitKind::kAgu)]->OutputIntent();
        if (!result.has_value()) {
            return;
        }

        ExecutionResult& executionResult = result.value();

        snapshot.valid = true;
        snapshot.pc = executionResult.instruction.pc;
        snapshot.opcode = OpcodeName(executionResult.instruction.opcode);

        if (machine_->TryIssueData(executionResult.sequence,
                                   executionResult.address,
                                   executionResult.memory_write,
                                   executionResult.store_value)) {
                                    consume_output_[static_cast<std::size_t>(ExecutionUnitKind::kAgu)] = true;
        } 
        // else if (!machine_->PendingDataHasCapacity()) {
        //     machine_->RecordCpuStall(CpuStallReason::kPendingDataFull);
        // } else {
        //     machine_->RecordCpuStall(CpuStallReason::kDcacheInput);
        // } 

        // throw std::runtime_error("Core::Memory: TODO(A2--DESIGN-05)"); 
    }
// void Core::Execute(){ StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kExecute)]; snapshot=StageSnapshot(); snapshot.action="EMPTY"; throw std::runtime_error("Core::Execute: TODO(A2-DESIGN-05)"); }
void Core::Execute(){ 
        StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kExecute)];
        snapshot=StageSnapshot(); 
        snapshot.action="EMPTY"; 

        for (int i = 0; i < 6; i++) {
            auto result = pipelines_[i]->OutputIntent();
            if (!result.has_value()) continue;
            ExecutionResult& executionResult = result.value();

            snapshot.valid = true;
            snapshot.pc = executionResult.instruction.pc;
            snapshot.opcode = OpcodeName(executionResult.instruction.opcode);

            if (static_cast<ExecutionUnitKind>(i) == ExecutionUnitKind::kControl) {
                if (executionResult.redirect) {
                    next_pc_ = executionResult.next_pc;
                    machine_->InvalidateFetchesAfter(executionResult.sequence);
                    next_fetch_stopped_ = false;
                    control_recognized_this_cycle_ = true;
                } else if (executionResult.halt) {
                    next_fetch_stopped_ = true;
                } else {
                    next_fetch_stopped_ = false;
                }
            }

            machine_->CompleteNonMemory(executionResult.sequence, executionResult.value, executionResult.halt);
            consume_output_[i] = true;
        }

        // throw std::runtime_error("Core::Execute: TODO(A2--DESIGN-05)"); 
    }
// void Core::Decode(){ StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kDecode)]; snapshot=StageSnapshot(); snapshot.action="EMPTY"; throw std::runtime_error("Core::Decode: TODO(A2-DESIGN-03)"); }
void Core::Decode(){ 
        StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kDecode)]; 
        snapshot=StageSnapshot(); 
        snapshot.action="EMPTY"; 

        if (!if_id_.valid) {
            for (int i = 0; i < 7; i++) {
                pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
            }
            pipelines_operated_ = true;
            return;
        }

        FetchedInstruction& fetchedInstruction = if_id_.fetched;
        Instruction& instruction = fetchedInstruction.instruction;

        snapshot.valid = true;
        snapshot.pc = instruction.pc;
        snapshot.opcode = OpcodeName(instruction.opcode);

        if (IsControl(instruction.opcode) && !if_id_.control_recognized) {
            next_fetch_stopped_ = true;
            next_if_id_.control_recognized = true;
            control_recognized_this_cycle_ = true;
        }

        DecodeResult decodeResult;

        if (!if_id_.decoded) {
            decodeResult = main_control_->Decode(instruction);
        } else {
            decodeResult = if_id_.decode;
        }

        ExecutionUnitKind unit = decodeResult.unit;
        int unitIndex = static_cast<int> (unit);

        // Checks in order given in the pdf
        // Completion capacity
        if (!machine_->CompletionQueueHasCapacity()) {
            machine_->RecordCpuStall(CpuStallReason::kCompletionQueueFull);
            for (int i = 0; i < 7; i++) {
                pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
            }
            pipelines_operated_ = true;
            return;

        }
        // RAW and WAW
        HazardResult hazardResult = hazard_control_->Check(machine_, fetchedInstruction.sequence, instruction, decodeResult.writes_register);
        if (hazardResult == HazardResult::kRaw) {
            machine_->RecordCpuStall(CpuStallReason::kOutstandingRaw);
            for (int i = 0; i < 7; i++) {
                pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
            }
            pipelines_operated_ = true;
            return;
        } 
        if (hazardResult == HazardResult::kWaw) {
            machine_->RecordCpuStall(CpuStallReason::kOutstandingWaw);
            for (int i = 0; i < 7; i++) {
                pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
            }
            pipelines_operated_ = true;
            return;
        } 
        // Unit availability
        if (!pipelines_[unitIndex]->CanAccept(current_boundary_+1, consume_output_[unitIndex])) {
            machine_->RecordCpuStall(CpuStallReason::kExecutionUnitBusy);
            for (int i = 0; i < 7; i++) {
                pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
            }
            pipelines_operated_ = true;
            return;
        }
        // Completion reservation
        std::optional<int> resultBoundary;
        if (unit != ExecutionUnitKind::kAgu) {
            resultBoundary = ResultBoundary(current_boundary_+1, pipelines_[unitIndex]->config().latency);
            if (!machine_->NormalCompletionSlotAvailable(*resultBoundary)) {
                machine_->RecordCpuStall(CpuStallReason::kExecutionCompletionSlot);
                for (int i = 0; i < 7; i++) {
                    pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
                }
                pipelines_operated_ = true;
                return;
            }
        }

        ExecutionOperation executionOperation;
        executionOperation.sequence = fetchedInstruction.sequence;
        executionOperation.instruction = instruction;
        executionOperation.dispatch_boundary = current_boundary_+1;

        executionOperation.lhs = machine_->ReadRegister(instruction.rs1);
        if (ReadsRs2(instruction.opcode)) {
            executionOperation.rhs = machine_->ReadRegister(instruction.rs2);
        } else {
            executionOperation.rhs = static_cast<std::uint32_t>(instruction.immediate);
        }

        if (instruction.opcode == Opcode::kSw) {
            executionOperation.store_value =
            machine_->ReadRegister(instruction.rs2);
        }
        
        if (!machine_->TryAllocateCompletion(fetchedInstruction.sequence, instruction, decodeResult.writes_register, resultBoundary)) {
            machine_->RecordCpuStall(CpuStallReason::kExecutionCompletionSlot);
            for (int i = 0; i < 7; i++) {
                pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
            }
            pipelines_operated_ = true;
            return;
        }

        next_if_id_ = FrontendLatch();
        if_id_consumed_ = true;

        for (int i = 0; i < 7; i++) {
            if (i == unitIndex) {
                pipelines_[i]->Operate(current_boundary_, executionOperation, consume_output_[i]);
            } else {
                pipelines_[i]->Operate(current_boundary_, std::nullopt, consume_output_[i]);
            }
        }

        pipelines_operated_ = true;
        // throw std::runtime_error("Core::Decode: TODO(A2--DESIGN-03)"); 
    }
// void Core::Fetch(){ StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kFetch)]; snapshot=StageSnapshot(); snapshot.pc=pc_; throw std::runtime_error("Core::Fetch: TODO(A2-DESIGN-02)"); }
void Core::Fetch(){ 
        StageSnapshot& snapshot=snapshots_[static_cast<std::size_t>(StageRole::kFetch)]; 
        snapshot=StageSnapshot(); 
        snapshot.pc=pc_; 

        // Check if fetch is stopped due to pending CONTROL resolution
        if (fetch_stopped_ || control_recognized_this_cycle_) {
            machine_->RecordCpuStall(CpuStallReason::kControlDisabled);
            next_pc_ = pc_;
            next_if_id_ = (if_id_consumed_) ? FrontendLatch():if_id_;
            return;
        } 
        // If Fetch is running, run TryIssueFetch()
        else {
            machine_->TryIssueFetch(pc_);
        }

        bool checkIfId = (!if_id_.valid) || (if_id_consumed_);
        if (checkIfId) {
            // if IF-ID is free then check and update from L1I
            FetchedInstruction fetchedInstruction;
            bool peekFetchedResult = machine_->PeekFetched(&fetchedInstruction);
            if (peekFetchedResult) {
                // Received an instruction from L1I
                machine_->ConsumeFetched();
                next_if_id_ = FrontendLatch();
                next_if_id_.valid = true;
                next_if_id_.fetched = fetchedInstruction;
                next_pc_ = pc_ + 4;

                snapshot.valid = true;
                snapshot.pc = fetchedInstruction.instruction.pc;
                snapshot.opcode = OpcodeName(fetchedInstruction.instruction.opcode);

            } else {
                // Still waiting for an instruction from L1I
                next_pc_ = pc_;
                next_if_id_ = FrontendLatch();
            }
        } else {
            // Stalled
            next_pc_ = pc_;
            next_if_id_ = if_id_;
        }

        // throw std::runtime_error("Core::Fetch: TODO(A2--DESIGN-02)"); 
    }
void Core::BeginCycle(){ if(cycle_begun_) throw std::runtime_error("CPU logic cycle initialized twice"); next_pc_=pc_; next_fetch_stopped_=fetch_stopped_; next_if_id_=if_id_; consume_output_.fill(false); if_id_consumed_=false; control_recognized_this_cycle_=false; pipelines_operated_=false; cycle_begun_=true; }
StageSnapshot Core::SnapshotFor(const Instruction& instruction){ StageSnapshot snapshot; snapshot.valid=true; snapshot.pc=instruction.pc; snapshot.opcode=OpcodeName(instruction.opcode); return snapshot; }
}
