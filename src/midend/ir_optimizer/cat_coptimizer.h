#pragma once

#include <llvm-20/llvm/IR/Analysis.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/PassManager.h>
#include <llvm/IR/Module.h>
#include <llvm/Passes/PassBuilder.h>

namespace cat::opt {

class CatPromotePass : public llvm::PassInfoMixin<CatPromotePass> {
public:
  llvm::PreservedAnalyses run(llvm::Function &F,
                              llvm::FunctionAnalysisManager &AM);
};

class CatOptimizer {
public:
  explicit CatOptimizer() {}

  void optimize(llvm::Module &module);

private:
};

} // namespace cat::opt
