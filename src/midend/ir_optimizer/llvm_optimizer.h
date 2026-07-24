#pragma once

#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/Passes/PassBuilder.h>

namespace cat::opt {

struct LLVMOptOptions {
  bool mem2reg     = true;
  bool simplify_cfg = true;
  bool instcombine  = true;
  bool dce          = true;
  unsigned instcombine_iterations = 2;
};

class LLVMOptimizer {
public:
  explicit LLVMOptimizer(LLVMOptOptions opts = {}) : options(std::move(opts)) {}

  void optimize(llvm::Module &module);

private:
  LLVMOptOptions options;
};

} // namespace cat::opt
