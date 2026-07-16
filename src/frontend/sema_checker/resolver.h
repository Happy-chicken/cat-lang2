#pragma once
#include "diag.h"
#include "item.h"
#include "pass_interface.h"
#include "sema_ctx.h"
namespace cat {
class Resolver : public semantics::Pass {
  const char *name() const noexcept { return "Resolver"; }

  bool run(Program &program, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);
};
} // namespace cat