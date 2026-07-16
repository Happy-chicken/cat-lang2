#pragma once
#include "pass_interface.h"
#include "resolver.h"

namespace cat {
  class SemaChecker : public semantics::Pass {
public:
    const char *name() const noexcept override { return "SemaChecker"; }

    bool run(Program &program, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) override {
      Resolver resolver;
      return resolver.run(program, ctx, diag);
    }
  };
}// namespace cat
