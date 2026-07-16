#pragma once
#include "diag.h"
#include "item.h"
#include "pass_interface.h"
#include "sema_ctx.h"
#include <unordered_set>
namespace cat {
  class Resolver : public semantics::Pass {
public:
    const char *name() const noexcept { return "Resolver"; }

    bool run(Program &program, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void declare_top_level(Symbol symbol, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void declare_type_item(const ItemNode &item_node, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void declare_trait_item(const ItemNode &item_node, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void declare_value_item(const ItemNode &item_node, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void declare_impl_methods(const Impl &imp, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void declare_global_var(GlobalVar &gv, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void validate_impl_target(Impl &impl, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void collect_class_deps(const ast::Type &ty, vector<string> &deps);

    void detect_cycle(
        const string &current,
        const std::unordered_map<string, vector<string>> &deps,
        const std::unordered_map<string, Span> &spans,
        std::unordered_set<string> &visited,
        vector<string> &stack,
        std::unordered_set<string> &in_stack,
        error::DiagCtxt &diag
    );

    void check_struct_recursion(const Program &program, error::DiagCtxt &diag);
  };
}// namespace cat
