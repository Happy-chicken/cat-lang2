#include "llvm_optimizer.h"

#include <llvm-20/llvm/Analysis/CGSCCPassManager.h>
#include <llvm-20/llvm/Analysis/LoopAnalysisManager.h>
#include <llvm-20/llvm/Passes/PassBuilder.h>
#include <llvm-20/llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm-20/llvm/Transforms/Scalar/DCE.h>
#include <llvm-20/llvm/Transforms/Scalar/SimplifyCFG.h>
#include <llvm-20/llvm/Transforms/Utils/Mem2Reg.h>

namespace cat::opt {

void LLVMOptimizer::optimize(llvm::Module &module) {
  llvm::LoopAnalysisManager LAM;
  llvm::FunctionAnalysisManager FAM;
  llvm::CGSCCAnalysisManager CGAM;
  llvm::ModuleAnalysisManager MAM;

  llvm::PassBuilder PB;
  PB.registerModuleAnalyses(MAM);
  PB.registerCGSCCAnalyses(CGAM);
  PB.registerFunctionAnalyses(FAM);
  PB.registerLoopAnalyses(LAM);
  PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);

  llvm::FunctionPassManager FPM;

  if (options.mem2reg)
    FPM.addPass(llvm::PromotePass());

  if (options.simplify_cfg)
    FPM.addPass(llvm::SimplifyCFGPass());

  if (options.instcombine) {
    for (unsigned i = 0; i < options.instcombine_iterations; ++i) {
      FPM.addPass(llvm::InstCombinePass());
      if (options.simplify_cfg)
        FPM.addPass(llvm::SimplifyCFGPass());
    }
  }

  if (options.dce)
    FPM.addPass(llvm::DCEPass());

  llvm::ModulePassManager MPM;
  MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
  MPM.run(module, MAM);
}

} // namespace cat::opt
