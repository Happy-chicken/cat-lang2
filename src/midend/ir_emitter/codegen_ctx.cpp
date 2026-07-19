#include "codegen_ctx.h"

namespace cat::ir {

  CodeGenCtxt::CodeGenCtxt(const string &module_name)
      : llvm_ctx(std::make_unique<llvm::LLVMContext>()),
        builder(std::make_unique<llvm::IRBuilder<>>(*llvm_ctx)),
        module(std::make_unique<llvm::Module>(module_name, *llvm_ctx)),
        str_counter(0) {}

}// namespace cat::ir
