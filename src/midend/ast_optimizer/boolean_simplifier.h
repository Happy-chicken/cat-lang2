#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class BooleanSimplifier : public ASTPass<BooleanSimplifier> {
public:
  void on_function(FunctionDef &fn) { walk_block(fn.body); }

  void on_unary(Expr &parent, UnaryExpr &unary) {
    if (unary.op != UnaryOp::Not) return;

    if (auto *inner = std::get_if<UnaryExpr>(&unary.expr->expr)) {
      if (inner->op == UnaryOp::Not) {
        parent = std::move(inner->expr->expr);
        return;
      }
    }

    bool val;
    if (is_literal(*unary.expr, val)) {
      parent = make_bool_literal(!val);
    }
  }
};

}// namespace cat::opt::ast
