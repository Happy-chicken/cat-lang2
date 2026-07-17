#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class BooleanSimplifier : public ASTPass<BooleanSimplifier> {
public:
  static constexpr const char *name = "BooleanSimplifier";

  void before_unary(UnaryExpr &unary) { simplify(unary); }

private:
  void simplify(UnaryExpr &unary) {
    if (unary.op != UnaryOp::Not) return;
    // !!x -> x
    if (auto *inner = std::get_if<UnaryExpr>(&unary.expr->expr)) {
      if (inner->op == UnaryOp::Not) {
        set_expr(unary.expr, std::move(inner->expr->expr));
        return;
      }
    }
    // !true -> false, !false -> true
    bool val;
    if (is_literal(*unary.expr, val)) {
      set_expr(unary.expr, cat::make_bool_literal(!val));
    }
  }
};

} // namespace cat::midend
