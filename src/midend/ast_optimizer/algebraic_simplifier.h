#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class AlgebraicSimplifier : public ASTPass<AlgebraicSimplifier> {
public:
  void on_function(FunctionDef &fn) { walk_block(fn.body); }

  void on_binary(Expr &parent, BinaryExpr &bin) {
    if (bin.op == BinaryOp::Add || bin.op == BinaryOp::Sub) {
      if (is_int_zero(*bin.rhs)) {
        parent = std::move(bin.lhs->expr);
        return;
      }
    }
    if (bin.op == BinaryOp::Add) {
      if (is_int_zero(*bin.lhs)) {
        parent = std::move(bin.rhs->expr);
        return;
      }
    }
    if (bin.op == BinaryOp::Mul) {
      int64_t val;
      if (is_literal(*bin.rhs, val) && val == 1) {
        parent = std::move(bin.lhs->expr);
        return;
      }
      if (is_literal(*bin.lhs, val) && val == 1) {
        parent = std::move(bin.rhs->expr);
        return;
      }
      if (is_int_zero(*bin.rhs) || is_int_zero(*bin.lhs)) {
        parent = make_int_literal(0);
      }
    }
  }
};

} // namespace cat::opt::ast
