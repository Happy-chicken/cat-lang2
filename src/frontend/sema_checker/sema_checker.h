#pragma once
#include "pass_interface.h"
#include "resolver.h"
#include <variant>

namespace cat {
  template<class... Ts>
  struct overloaded : Ts... {
    using Ts::operator()...;
  };
  template<class... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;
  class SemaChecker : public semantics::Pass {
public:
    const char *name() const noexcept override { return "SemaChecker"; }

    bool run(Program &program, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) override {
      for (const auto &item : program.items) {
        std::visit(
            overloaded{
                [&](const FunctionDef &func) { check_function(func, item.span, ctx, diag); },
                [&](const Impl &imp) { check_impl(imp, item.span, ctx, diag); },
                [&](const GlobalVar &gv) { check_global_var(gv, item.span, ctx, diag); },
                [&](const Class &cls) { check_class_defaults(cls, ctx, diag); },
                [](const auto &) {}},
            item.item);
      }
      return !diag.has_errors();
    }

    void check_function(const FunctionDef &func, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void check_impl(const Impl &imp, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void check_block(const Block &block, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void check_stmt(const StmtNode &stmt, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void check_expr(const ExprNode &expr, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void check_global_var(const GlobalVar &gv, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);

    void check_class_defaults(const Class &cls, semantics::SemaCtxt &ctx, error::DiagCtxt &diag);
  };
}// namespace cat
