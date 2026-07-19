#pragma once
#include "common.h"
#include "diag.h"
#include <llvm-20/llvm/ExecutionEngine/Orc/LLJIT.h>

namespace cat::ir {
  class IrEmitter;
}// namespace cat::ir

namespace cat::jit {

  class JIT {
  public:
    explicit JIT(error::DiagCtxt &diag);

    void add_module(ir::IrEmitter &emitter);
    void add_symbol(const string &name, void *fn_ptr);
    int run();

  private:
    void *lookup(const string &name);

    error::DiagCtxt &diag;
    uptr<llvm::orc::LLJIT> engine;
  };

}// namespace cat::jit
