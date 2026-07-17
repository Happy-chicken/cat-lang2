#pragma once
#include "common/common.h"
#include "diag.h"
#include "item.h"
#include "pass_interface.h"
#include "sema_ctx.h"

namespace cat::semantics {

  class PassManager {
public:
    PassManager() : ctx{} {}

    void add_pass(uptr<semantics::Pass> pass) {
      passes.push_back(std::move(pass));
    }

    bool run(Program &program, error::DiagCtxt &diag) {
      bool ok = true;
      for (auto &pass: passes) {
        if (!pass->run(program, ctx, diag)) {
          ok = false;
        }
      }
      return ok;
    }

private:
    vector<uptr<semantics::Pass>> passes;
    semantics::SemaCtxt ctx;
  };

}// namespace cat
