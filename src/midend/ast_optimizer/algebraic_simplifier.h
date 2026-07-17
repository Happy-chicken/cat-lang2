#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class AlgebraicSimplifier : public ASTPass<AlgebraicSimplifier> {
public:
  static constexpr const char *name = "AlgebraicSimplifier";

  void before_binary(BinaryExpr &bin) { simplify(bin); }

private:
  void simplify(BinaryExpr &bin) {
    // x + 0 = x, x - 0 = x
    if (bin.op == BinaryOp::Add || bin.op == BinaryOp::Sub) {
      if (is_int_zero(*bin.rhs)) return;
    }
    // 0 + x = x
    if (bin.op == BinaryOp::Add) {
      if (is_int_zero(*bin.lhs)) {
        bin.lhs.swap(bin.rhs);
        return;
      }
    }
    // x * 1 = x, 1 * x = x
    if (bin.op == BinaryOp::Mul) {
      int64_t val;
      if (is_literal(*bin.rhs, val) && val == 1) return;
      if (is_literal(*bin.lhs, val) && val == 1) {
        bin.lhs.swap(bin.rhs);
        return;
      }
      // x * 0 = 0, 0 * x = 0
      if (is_int_zero(*bin.rhs) || is_int_zero(*bin.lhs)) {
        set_expr(bin.lhs, cat::make_int_literal(0));
        set_expr(bin.rhs, cat::make_int_literal(0));
      }
    }
  }
};

} // namespace cat::midend
