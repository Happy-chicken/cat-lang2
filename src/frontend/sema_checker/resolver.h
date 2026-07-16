#pragma once
#include "diag.h"
#include "item.h"
#include "pass_interface.h"
#include "sema_ctx.h"
namespace cat {
class Resolver : public semantics::Pass {
  const char *name() const noexcept { return "Resolver"; }

  bool run(Program &program, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

  void declare_type_item(const ItemNode &item_node, semantics::SemaCtxt &ctx,
                         error::DiagCtxt &diag);

  void declare_trait_item(const ItemNode &item_node, semantics::SemaCtxt &ctx,
                          error::DiagCtxt &diag);

  void declare_value_item(const ItemNode &item_node, semantics::SemaCtxt &ctx,
                          error::DiagCtxt &diag);

  void declare_impl_methods(const ItemNode &item_node, semantics::SemaCtxt &ctx,
                            error::DiagCtxt &diag);

  void declare_global_var(const ItemNode &item_node, semantics::SemaCtxt &ctx,
                          error::DiagCtxt &diag);

  void validate_impl_target(Impl &impl, Span span, semantics::SemaCtxt &ctx,
                            error::DiagCtxt &diag);
};
} // namespace cat