#pragma once
#include "inferer.h"
#include "item.h"
#include "pass_interface.h"
#include <variant>

namespace cat::semantics {

class TypeChecker : public semantics::Pass {
public:
  const char *name() const noexcept override { return "TypeChecker"; }

  bool run(cat::Program &program, SemaCtxt &ctx,
           ::cat::error::DiagCtxt &diag) override {
    for (auto &item_node : program.items) {
      std::visit(
          overloaded{
              [&](FunctionDef &func) {
                check_function(func, item_node.span, ctx, diag);
              },
              [&](Impl &imp) {
                for (auto &method : imp.methods) {
                  check_function(method, item_node.span, ctx, diag);
                }
              },
              [&](GlobalVar &gv) {
                check_global_var(gv, item_node.span, ctx, diag);
              },
              [&](Struct &strukt) { check_struct_defaults(strukt, ctx, diag); },
              [&](const auto &) {},
          },
          item_node.item);
    }
    return !diag.has_errors();
  }

  TypeChecker() : inferer{}, next_expr_id(0) {}

private:
  Inferer inferer;
  uint32_t next_expr_id;
  optional<Type> current_return_type;

  uint32_t fresh_expr_id() { return next_expr_id++; }

  void check_function(const FunctionDef &func, Span span, SemaCtxt &ctx,
                      error::DiagCtxt &diag);
  void check_stmt(const StmtNode &stmt, Span span, SemaCtxt &ctx,
                  error::DiagCtxt &diag);
  void check_block(const Block &block, Span span, SemaCtxt &ctx,
                   error::DiagCtxt &diag);
  void check_condition_is_bool(const Type &ty, Span span, SemaCtxt &ctx,
                               error::DiagCtxt &diag);
  void check_global_var(const GlobalVar &gv, Span span, SemaCtxt &ctx,
                        error::DiagCtxt &diag);
  void check_struct_defaults(const Struct &strukt, SemaCtxt &ctx,
                             error::DiagCtxt &diag);
};

} // namespace cat::semantics
