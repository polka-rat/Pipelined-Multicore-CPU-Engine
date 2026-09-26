#include "pipesim.h"
#include "cache/cache_controller.h"
#include "execution_unit.h"
#include "design/control.h"
#include "design/core.h"
#include "design/instruction_helpers.h"
#include "design/adapters.h"
#include <array>
#include <memory>
#include <stdexcept>
namespace pipesim {
using namespace design_internal;
std::unique_ptr<Processor> BuildDesign(Machine* machine) {
  if (machine == nullptr) throw std::runtime_error("BuildDesign machine is null");
  ProcessorBuilder builder;
  const cache::SystemConfig& config = machine->Configuration();
  std::array<ExecutionPipeline*,7> pipelines={};
  for(std::size_t i=0;i<pipelines.size();++i) pipelines[i]=new ExecutionPipeline(static_cast<ExecutionUnitKind>(i),config.execution_units[i]);
  MainControl* main_control=new MainControl();
  HazardControl* hazard_control=new HazardControl();
  std::shared_ptr<Core> core(new Core(machine,pipelines,main_control,hazard_control));

  const NodeId pc=builder.AddRegister(RegisterRole::kProgramCounter,"PC",new RegisterAdapter(core,RegisterKind::kPc));
  const NodeId fetch=builder.AddStage(StageRole::kFetch,"IF",new StageAdapter(core,StageRole::kFetch));
  builder.Connect(pc,fetch,ConnectionType::kData);
  const NodeId l1i=builder.AddCache(cache::CacheRole::kL1I,"L1I",new cache::CacheController(cache::CacheRole::kL1I,config.l1i,machine->TransactionIds()));
  builder.Connect(fetch,l1i,ConnectionType::kData);

  //Adding remaining stages of builder [TODO(A2--DESIGN-01)]
  const NodeId l2i=builder.AddCache(cache::CacheRole::kL2I,"L2I",new cache::CacheController(cache::CacheRole::kL2I,config.l2i,machine->TransactionIds()));
  builder.Connect(l1i,l2i,ConnectionType::kData);

  //Decode Stage  
  const NodeId if_id=builder.AddRegister(RegisterRole::kIfId,"IF_ID",new RegisterAdapter(core,RegisterKind::kIfId));
  builder.Connect(fetch, if_id,ConnectionType::kClockedData);
  const NodeId decode=builder.AddStage(StageRole::kDecode,"ID",new StageAdapter(core,StageRole::kDecode));
  builder.Connect(if_id,decode,ConnectionType::kData);

  // Execution Stage
  const NodeId id_ex=builder.AddRegister(RegisterRole::kIdEx,"ID_EX",new RegisterAdapter(core,RegisterKind::kIdEx));
  builder.Connect(decode,id_ex,ConnectionType::kClockedData);
  const NodeId execute=builder.AddStage(StageRole::kExecute,"EX",new StageAdapter(core,StageRole::kExecute));
  builder.Connect(id_ex,execute,ConnectionType::kData);

  // 7 Execution units
  const NodeId add=builder.AddUnit(UnitRole::kAdd,"ADD",pipelines[static_cast<std::size_t>(ExecutionUnitKind::kAdd)]);
  const NodeId sub=builder.AddUnit(UnitRole::kSub,"SUB",pipelines[static_cast<std::size_t>(ExecutionUnitKind::kSub)]);
  const NodeId and_=builder.AddUnit(UnitRole::kAnd,"AND",pipelines[static_cast<std::size_t>(ExecutionUnitKind::kAnd)]);
  const NodeId or_=builder.AddUnit(UnitRole::kOr,"OR",pipelines[static_cast<std::size_t>(ExecutionUnitKind::kOr)]);
  const NodeId xor_=builder.AddUnit(UnitRole::kXor,"XOR",pipelines[static_cast<std::size_t>(ExecutionUnitKind::kXor)]);
  const NodeId control=builder.AddUnit(UnitRole::kControl,"CONTROL",pipelines[static_cast<std::size_t>(ExecutionUnitKind::kControl)]);
  const NodeId agu=builder.AddUnit(UnitRole::kAgu,"AGU",pipelines[static_cast<std::size_t>(ExecutionUnitKind::kAgu)]);

  // Connecting decode stage to execution units
  builder.Connect(decode,add,ConnectionType::kClockedData);
  builder.Connect(decode,sub,ConnectionType::kClockedData);
  builder.Connect(decode,and_,ConnectionType::kClockedData);
  builder.Connect(decode,or_,ConnectionType::kClockedData);
  builder.Connect(decode,xor_,ConnectionType::kClockedData);
  builder.Connect(decode,control,ConnectionType::kClockedData);
  builder.Connect(decode,agu,ConnectionType::kClockedData);

  // Connecting execution units' outputs to execute stage
  builder.Connect(add,execute,ConnectionType::kData);
  builder.Connect(sub,execute,ConnectionType::kData);
  builder.Connect(and_,execute,ConnectionType::kData);
  builder.Connect(or_,execute,ConnectionType::kData);
  builder.Connect(xor_,execute,ConnectionType::kData);
  builder.Connect(control,execute,ConnectionType::kData);
  builder.Connect(control,pc,ConnectionType::kRedirect);

  // Memory Stage (with Cache access)
  const NodeId ex_mem=builder.AddRegister(RegisterRole::kExMem,"EX_MEM",new RegisterAdapter(core,RegisterKind::kExMem));
  builder.Connect(execute,ex_mem,ConnectionType::kClockedData);
  const NodeId memory=builder.AddStage(StageRole::kMemory,"MEM",new StageAdapter(core,StageRole::kMemory));
  builder.Connect(ex_mem,memory,ConnectionType::kData);
  const NodeId l1d=builder.AddCache(cache::CacheRole::kL1D,"L1D",new cache::CacheController(cache::CacheRole::kL1D,config.l1d,machine->TransactionIds()));
  builder.Connect(memory,l1d,ConnectionType::kData);
  const NodeId l2d=builder.AddCache(cache::CacheRole::kL2D,"L2D",new cache::CacheController(cache::CacheRole::kL2D,config.l2d,machine->TransactionIds()));
  builder.Connect(l1d,l2d,ConnectionType::kData);  

  // Connecting agu to mem
  builder.Connect(agu,memory,ConnectionType::kData);
  
  //Writeback Stage
  const NodeId mem_wb=builder.AddRegister(RegisterRole::kMemWb,"MEM_WB",new RegisterAdapter(core,RegisterKind::kMemWb));
  builder.Connect(memory,mem_wb,ConnectionType::kClockedData);
  const NodeId writeback = builder.AddStage(StageRole::kWriteback,"WB",new StageAdapter(core,StageRole::kWriteback));
  builder.Connect(mem_wb,writeback,ConnectionType::kData);

  // Main and Hazard Control Units
  const NodeId m_control=builder.AddUnit(UnitRole::kMainControl,"MAIN_CONTROL",main_control);
  const NodeId h_control=builder.AddUnit(UnitRole::kHazardControl,"HAZARD_CONTROL",hazard_control);
  builder.Connect(m_control, decode, ConnectionType::kControl);
  builder.Connect(h_control, pc, ConnectionType::kStall);
  builder.Connect(h_control, if_id, ConnectionType::kStall);
  builder.Connect(h_control, id_ex, ConnectionType::kBubble);
  
  // (void)main_control; (void)hazard_control;
  // throw std::runtime_error("BuildDesign: TODO(A2--DESIGN-01) incomplete");
  return builder.Build(machine);
}
}
