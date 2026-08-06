#include "pipesim.h"

#include <cstdint>
#include <memory>
#include <string>

namespace pipesim {
namespace {

enum class AluOp {
  kAdd,
  kSub,
  kAnd,
  kOr,
  kXor,
};

struct Control {
  bool reg_write = false;
  bool mem_read = false;
  bool mem_write = false;
  bool use_immediate = false;
  bool halt = false;
  AluOp alu_op = AluOp::kAdd;
};

struct Latch {
  bool valid = false;
  Instruction instruction;
  std::uint32_t first = 0;
  std::uint32_t second = 0;
  std::uint32_t result = 0;
  std::uint32_t store_value = 0;
  Control control;
};

bool IsControl(Opcode opcode) {
  (void)opcode;
  return false;  // TODO
}

bool ReadsRs1(Opcode opcode) {
  (void)opcode;
  return false;  // TODO
}

bool ReadsRs2(Opcode opcode) {
  (void)opcode;
  return false;  // TODO
}

class Alu : public TrackedUnit {
 public:
  std::uint32_t Run(AluOp operation, std::uint32_t left, std::uint32_t right) {
    Touch();

    switch (operation) {
      case AluOp::kAdd:
        return left + right;
      case AluOp::kSub:
        return 0;  // TODO
      case AluOp::kAnd:
        return 0;  // TODO
      case AluOp::kOr:
        return 0;  // TODO
      case AluOp::kXor:
        return 0;  // TODO
    }
    return 0;
  }
};

class MainControl : public TrackedUnit {
 public:
  Control Decode(Opcode opcode) {
    Touch();

    Control control;
    switch (opcode) {
      case Opcode::kAddi:
        control.reg_write = true;
        control.use_immediate = true;
        control.alu_op = AluOp::kAdd;
        break;

      case Opcode::kAdd:
      case Opcode::kSub:
      case Opcode::kAnd:
      case Opcode::kOr:
      case Opcode::kXor:
        break;  // TODO

      case Opcode::kLw:
        break;  // TODO

      case Opcode::kSw:
        break;  // TODO

      case Opcode::kBeq:
      case Opcode::kBne:
        break;  // TODO

      case Opcode::kHalt:
        break;  // TODO

      case Opcode::kJ:
      case Opcode::kNop:
        break;
    }

    return control;
  }
};

class HazardControl : public TrackedUnit {
 public:
  bool MustStall(const Latch& consumer, const Latch& id_ex,
                 const Latch& ex_mem) {
    Touch();
    (void)consumer;
    (void)id_ex;
    (void)ex_mem;
    return false;  // TODO
  }

 private:
  bool HasDependency(int source, bool source_is_used,
                     const Latch& producer) const {
    (void)source;
    (void)source_is_used;
    (void)producer;
    return false;  // TODO
  }
};

enum class FrontAction {
  kNormal,
  kHold,
  kStop,
  kRedirect,
};

enum class RegisterKind {
  kPc,
  kIfId,
  kIdEx,
  kExMem,
  kMemWb,
};

class Core {
 public:
  Core(Machine* machine, Alu* alu, MainControl* main_control,
       HazardControl* hazard_control)
      : machine_(machine),
        alu_(alu),
        main_control_(main_control),
        hazard_control_(hazard_control) {}

  void ResetRegister(RegisterKind kind) {
    switch (kind) {
      case RegisterKind::kPc:
        pc_ = machine_->EntryPoint();
        next_pc_ = pc_;
        halt_pending_ = false;
        next_halt_pending_ = false;
        break;
      case RegisterKind::kIfId:
        if_id_ = Latch();
        next_if_id_ = Latch();
        break;
      case RegisterKind::kIdEx:
        id_ex_ = Latch();
        next_id_ex_ = Latch();
        break;
      case RegisterKind::kExMem:
        ex_mem_ = Latch();
        next_ex_mem_ = Latch();
        break;
      case RegisterKind::kMemWb:
        mem_wb_ = Latch();
        next_mem_wb_ = Latch();
        break;
    }
  }

  void CommitRegister(RegisterKind kind) {
    switch (kind) {
      case RegisterKind::kPc:
        pc_ = next_pc_;
        halt_pending_ = next_halt_pending_;
        break;
      case RegisterKind::kIfId:
        if_id_ = next_if_id_;
        break;
      case RegisterKind::kIdEx:
        id_ex_ = next_id_ex_;
        break;
      case RegisterKind::kExMem:
        ex_mem_ = next_ex_mem_;
        break;
      case RegisterKind::kMemWb:
        mem_wb_ = next_mem_wb_;
        break;
    }
  }

  std::string RegisterSnapshot(RegisterKind kind) const {
    if (kind == RegisterKind::kPc) {
      return std::to_string(pc_);
    }
    const Latch* latch = nullptr;
    switch (kind) {
      case RegisterKind::kIfId: latch = &if_id_; break;
      case RegisterKind::kIdEx: latch = &id_ex_; break;
      case RegisterKind::kExMem: latch = &ex_mem_; break;
      case RegisterKind::kMemWb: latch = &mem_wb_; break;
      case RegisterKind::kPc: break;
    }
    if (latch == nullptr || !latch->valid) return "EMPTY";
    return OpcodeName(latch->instruction.opcode);
  }

  void ResetStage(StageRole role) { snapshots_[StageIndex(role)] = StageSnapshot(); }
  StageSnapshot Snapshot(StageRole role) const { return snapshots_[StageIndex(role)]; }

  void Evaluate(StageRole role) {
    switch (role) {
      case StageRole::kWriteback: Writeback(); break;
      case StageRole::kMemory:    Memory();    break;
      case StageRole::kExecute:   Execute();   break;
      case StageRole::kDecode:    Decode();    break;
      case StageRole::kFetch:     Fetch();     break;
    }
  }

 private:
  int StageIndex(StageRole role) const {
    switch (role) {
      case StageRole::kFetch: return 0;
      case StageRole::kDecode: return 1;
      case StageRole::kExecute: return 2;
      case StageRole::kMemory: return 3;
      case StageRole::kWriteback: return 4;
    }
    return 0;
  }

  void SetSnapshot(StageRole role, const Latch& latch, const std::string& action) {
    StageSnapshot& snapshot = snapshots_[StageIndex(role)];
    snapshot = StageSnapshot();
    snapshot.valid = latch.valid;
    snapshot.action = action;
    if (latch.valid) {
      snapshot.pc = latch.instruction.pc;
      snapshot.opcode = OpcodeName(latch.instruction.opcode);
    }
  }

  void Writeback() {
    SetSnapshot(StageRole::kWriteback, mem_wb_, mem_wb_.valid ? "RETIRE" : "EMPTY");
    if (!mem_wb_.valid) return;

    if (mem_wb_.control.reg_write) {
      machine_->WriteRegister(mem_wb_.instruction.rd, mem_wb_.result);
    }
    machine_->RetireInstruction();
    if (mem_wb_.control.halt) {
      machine_->SetHalted();
    }
  }

  void Memory() {
    SetSnapshot(StageRole::kMemory, ex_mem_, ex_mem_.valid ? "RUN" : "EMPTY");
    next_mem_wb_ = Latch();
    if (!ex_mem_.valid) return;

    next_mem_wb_ = ex_mem_;
    // TODO
  }

  void Execute() {
    front_action_ = FrontAction::kNormal;
    redirect_pc_ = 0;

    SetSnapshot(StageRole::kExecute, id_ex_, id_ex_.valid ? "RUN" : "EMPTY");
    next_ex_mem_ = Latch();
    if (!id_ex_.valid) return;

    next_ex_mem_ = id_ex_;
    next_ex_mem_.store_value = id_ex_.second;

    const Instruction& instruction = id_ex_.instruction;

    // TODO

    if (instruction.opcode == Opcode::kBeq) {
      Redirect(id_ex_.first == id_ex_.second
                   ? static_cast<std::uint32_t>(instruction.immediate)
                   : instruction.pc + 4);
    }
    // TODO
  }

  void Redirect(std::uint32_t new_pc) {
    front_action_ = FrontAction::kRedirect;
    redirect_pc_ = new_pc;
    snapshots_[StageIndex(StageRole::kExecute)].action = "RESOLVE";
  }

  void Decode() {
    SetSnapshot(StageRole::kDecode, if_id_, if_id_.valid ? "RUN" : "EMPTY");
    next_id_ex_ = Latch();

    if (front_action_ == FrontAction::kRedirect || !if_id_.valid) return;

    // TODO
  }

  void Fetch() {
    StageSnapshot& snapshot = snapshots_[StageIndex(StageRole::kFetch)];
    snapshot = StageSnapshot();
    snapshot.pc = pc_;

    if (front_action_ == FrontAction::kRedirect) {
      next_pc_ = redirect_pc_;
      next_if_id_ = Latch();
      snapshot.action = "REDIRECT";
      return;
    }

    if (front_action_ == FrontAction::kHold) {
      next_pc_ = pc_;
      next_if_id_ = if_id_;
      snapshot.action = "HOLD";
      return;
    }

    if (front_action_ == FrontAction::kStop || halt_pending_ || next_halt_pending_) {
      next_pc_ = pc_;
      next_if_id_ = Latch();
      snapshot.action = "BUBBLE";
      return;
    }

    Instruction instruction;
    if (!machine_->Fetch(pc_, &instruction)) {
      next_pc_ = pc_;
      next_if_id_ = Latch();
      snapshot.action = "EMPTY";
      return;
    }

    next_if_id_ = Latch();
    next_if_id_.valid = true;
    next_if_id_.instruction = instruction;
    next_pc_ = pc_ + 4;

    snapshot.valid = true;
    snapshot.pc = instruction.pc;
    snapshot.opcode = OpcodeName(instruction.opcode);
    snapshot.action = "FETCH";
  }

  Machine* machine_;
  Alu* alu_;
  MainControl* main_control_;
  HazardControl* hazard_control_;

  std::uint32_t pc_ = 0;
  std::uint32_t next_pc_ = 0;

  Latch if_id_, next_if_id_;
  Latch id_ex_, next_id_ex_;
  Latch ex_mem_, next_ex_mem_;
  Latch mem_wb_, next_mem_wb_;

  bool halt_pending_ = false;
  bool next_halt_pending_ = false;

  FrontAction front_action_ = FrontAction::kNormal;
  std::uint32_t redirect_pc_ = 0;
  StageSnapshot snapshots_[5];
};

class Stage : public StageComponent {
 public:
  Stage(StageRole role, Core* core, bool owns_core = false)
      : role_(role), core_(core) {
    if (owns_core) owned_core_.reset(core);
  }
  void Reset() override { core_->ResetStage(role_); }
  void Evaluate() override { core_->Evaluate(role_); }
  StageSnapshot Snapshot() const override { return core_->Snapshot(role_); }

 private:
  StageRole role_;
  Core* core_;
  std::unique_ptr<Core> owned_core_;
};

class Register : public ClockedState {
 public:
  Register(RegisterKind kind, Core* core) : kind_(kind), core_(core) {}
  void Reset() override { core_->ResetRegister(kind_); }
  void Commit() override { core_->CommitRegister(kind_); }
  std::string Snapshot() const override { return core_->RegisterSnapshot(kind_); }

 private:
  RegisterKind kind_;
  Core* core_;
};

}  // namespace

std::unique_ptr<Processor> BuildDesign(Machine* machine) {
  ProcessorBuilder builder;

  Alu* alu = new Alu;
  MainControl* main_control = new MainControl;
  HazardControl* hazard_control = new HazardControl;
  Core* core = new Core(machine, alu, main_control, hazard_control);

  Register* pc = new Register(RegisterKind::kPc, core);
  Register* if_id = new Register(RegisterKind::kIfId, core);
  Register* id_ex = new Register(RegisterKind::kIdEx, core);
  Register* ex_mem = new Register(RegisterKind::kExMem, core);
  Register* mem_wb = new Register(RegisterKind::kMemWb, core);

  Stage* fetch = new Stage(StageRole::kFetch, core, /*owns_core=*/true);
  Stage* decode = new Stage(StageRole::kDecode, core);
  Stage* execute = new Stage(StageRole::kExecute, core);
  Stage* memory = new Stage(StageRole::kMemory, core);
  Stage* writeback = new Stage(StageRole::kWriteback, core);

  // Worked example: register the PC and the fetch stage, then connect
  // them. Every other register/stage/unit follows the same AddRegister /
  // AddStage / AddUnit shape -- each returns a NodeId, keep it, pass it
  // to Connect(). Fill in the rest against SPEC.md's connection table.
  NodeId pc_node = builder.AddRegister(RegisterRole::kProgramCounter, "pc", pc);
  NodeId fetch_node = builder.AddStage(StageRole::kFetch, "fetch", fetch);
  builder.Connect(pc_node, fetch_node, ConnectionType::kData);

  // TODO: register if_id, id_ex, ex_mem, mem_wb (AddRegister)
  // TODO: register decode, execute, memory, writeback (AddStage)
  // TODO: register alu, main_control, hazard_control (AddUnit)
  // TODO: connect every edge in SPEC.md's table. In the worked example
  //       above, a register feeding a stage is kData -- a stage writing
  //       into a register is kClockedData instead. kControl, kStall,
  //       kBubble, kRedirect are each used for exactly the one
  //       connection SPEC.md names them for.

  return builder.Build(machine);
}

}  // namespace pipesim
