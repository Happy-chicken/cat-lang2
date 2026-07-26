#include "cat_coptimizer.h"

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/Transforms/InstCombine/InstCombine.h>
#include <llvm/Transforms/Scalar/DCE.h>
#include <llvm/Transforms/Scalar/SimplifyCFG.h>

#include "leak_checker.h"
#include "mem2reg.h"

namespace cat::opt {

llvm::PreservedAnalyses CatPromotePass::run(llvm::Function &F,
                                            llvm::FunctionAnalysisManager &AM) {
  auto &DT = AM.getResult<llvm::DominatorTreeAnalysis>(F);
  auto &DF = AM.getResult<llvm::DominanceFrontierAnalysis>(F);

  PromoteMem2Reg pass(F, DT, DF);
  pass.run();

  return llvm::PreservedAnalyses::none();
}

void CatOptimizer::optimize(llvm::Module &module) {
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

  // Step 1: mem2reg via the function pass manager.
  llvm::FunctionPassManager FPM;
  FPM.addPass(CatPromotePass());

  llvm::ModulePassManager MPM;
  MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
  MPM.run(module, MAM);

  // Step 2: compute cross-function summaries for the leak analysis.
  auto summaries = compute_func_summaries(module);

  // Step 3: run leak analysis per function using summaries.
  for (auto &F : module) {
    if (F.isDeclaration())
      continue;
    if (F.getName().starts_with("llvm."))
      continue;

    llvm::DominatorTree DT(F);
    LeakChecker LC(F, DT, /*strict_mode=*/false, &summaries);
    LC.run();
    LC.dump_reports(llvm::errs());
  }
}

} // namespace cat::opt
