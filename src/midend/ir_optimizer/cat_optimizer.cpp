#include "cat_coptimizer.h"

#include <llvm/Analysis/CGSCCPassManager.h>
#include <llvm/Analysis/LoopAnalysisManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Support/raw_ostream.h>

#include "domtree.h"
#include "leak_checker.h"
#include "mem2reg.h"

namespace cat::opt {

llvm::PreservedAnalyses CatPromotePass::run(llvm::Function &F,
                                            llvm::FunctionAnalysisManager &) {
  ana::DomTree DT(F);
  ana::DomFrontier DF(DT);
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

  llvm::FunctionPassManager FPM;
  FPM.addPass(CatPromotePass());

  llvm::ModulePassManager MPM;
  MPM.addPass(llvm::createModuleToFunctionPassAdaptor(std::move(FPM)));
  MPM.run(module, MAM);

  auto summaries = compute_func_summaries(module);

  for (auto &F : module) {
    if (F.isDeclaration()) continue;
    if (F.getName().starts_with("llvm.")) continue;

    llvm::DominatorTree LLDT(const_cast<llvm::Function &>(F));
    LeakChecker LC(const_cast<llvm::Function &>(F), LLDT,
                   /*strict_mode=*/false, &summaries);
    LC.run();
    LC.dump_reports(llvm::errs());
  }
}

} // namespace cat::opt
