#include "design/control.h"
#include <stdexcept>
namespace pipesim::design_internal {
DecodeResult MainControl::Decode(const Instruction& instruction) { Touch(); return {UnitFor(instruction.opcode), WritesRegister(instruction)}; }
HazardResult HazardControl::Check(Machine* machine,std::uint64_t sequence,const Instruction& instruction,bool writes_register){ Touch(); (void)writes_register; if(ReadsRs1(instruction.opcode)&&machine->HasOlderProducer(sequence,instruction.rs1)) return HazardResult::kRaw; throw std::runtime_error("HazardControl::Check: TODO(A2-DESIGN-03)"); }
}
