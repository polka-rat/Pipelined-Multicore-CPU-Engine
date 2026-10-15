#include "pipesim.h"
#include "cache/cache_controller.h"
#include "execution_unit.h"
#include "design/control.h"
#include "design/core.h"
#include "design/instruction_helpers.h"
#include "design/adapters.h"
#include "branch_prediction.h"
#include "speculation.h"
#include <array>
#include <memory>
#include <stdexcept>
namespace pipesim {

    using namespace design_internal;

    std::unique_ptr<Processor> BuildDesign(Machine* machine) {

        if (machine == nullptr) throw std::runtime_error("BuildDesign machine is null");

        ProcessorBuilder builder;

        const cache::SystemConfig& config = machine->Configuration();

        std::array<ExecutionPipeline*,7> pipelines= {
        };

        for(std::size_t i=0;i<pipelines.size();++i) pipelines[i]=new ExecutionPipeline(static_cast<ExecutionUnitKind>(i),config.execution_units[i]);

        MainControl* main_control=new MainControl();

        HazardControl* hazard_control=new HazardControl();

        auto predictors = BuildPredictors();

        BranchPredictionSystem* branch_prediction = new BranchPredictionSystem(       std::move(predictors[0]), std::move(predictors[1]),       std::move(predictors[2]));

        SpeculationController* speculation_control = new SpeculationController(8);

        std::shared_ptr<Core> core(new Core(machine,pipelines,main_control,hazard_control,                                       branch_prediction,speculation_control));

        const NodeId pc=builder.AddRegister(RegisterRole::kProgramCounter,"PC",new RegisterAdapter(core,RegisterKind::kPc));

        const NodeId fetch=builder.AddStage(StageRole::kFetch,"IF",new StageAdapter(core,StageRole::kFetch));

        builder.Connect(pc,fetch,ConnectionType::kData);

        const NodeId l1i=builder.AddCache(cache::CacheRole::kL1I,"L1I",new cache::CacheController(cache::CacheRole::kL1I,config.l1i,machine->TransactionIds()));

        builder.Connect(fetch,l1i,ConnectionType::kData);

        const NodeId branch_prediction_node = builder.AddUnit(       UnitRole::kBranchPrediction,"BRANCH_PREDICTION",branch_prediction);

        builder.Connect(branch_prediction_node,fetch,ConnectionType::kControl);

        const NodeId speculation_control_node = builder.AddUnit(       UnitRole::kSpeculationControl,"SPECULATION_CONTROL",speculation_control);

        builder.Connect(speculation_control_node,fetch,ConnectionType::kStall);

        (void)main_control;
        (void)hazard_control;

        throw std::runtime_error("BuildDesign: TODO(A2-DESIGN-01) incomplete");

    }

}

