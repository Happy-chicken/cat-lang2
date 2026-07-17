#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class ConstantFolder : public ASTPass<ConstantFolder> {
public:
  static constexpr const char *name = "ConstantFolder";

  void before_binary(BinaryExpr &bin) { fold(bin); }

private:
  void fold(BinaryExpr &bin) {
    int64_t li, ri;
    float lf, rf;

    if (is_literal(*bin.lhs, li) && is_literal(*bin.rhs, ri)) {
      auto result = fold_int(bin.op, li, ri);
      set_expr(bin.lhs, std::move(result));
      set_expr(bin.rhs, cat::make_int_literal(ri));
    } else if (is_literal(*bin.lhs, lf) && is_literal(*bin.rhs, rf)) {
      auto result = fold_float(bin.op, lf, rf);
      set_expr(bin.lhs, std::move(result));
      set_expr(bin.rhs, cat::make_float_literal(rf));
    }
  }

  Expr fold_int(BinaryOp op, int64_t a, int64_t b) {
    switch (op) {
    case BinaryOp::Add: return cat::make_int_literal(a + b);
    case BinaryOp::Sub: return cat::make_int_literal(a - b);
    case BinaryOp::Mul: return cat::make_int_literal(a * b);
    case BinaryOp::Div:
      if (b == 0) break;
      return cat::make_int_literal(a / b);
    case BinaryOp::And: return cat::make_bool_literal(a && b);
    case BinaryOp::Or:  return cat::make_bool_literal(a || b);
    case BinaryOp::Eq:  return cat::make_bool_literal(a == b);
    case BinaryOp::NotEq: return cat::make_bool_literal(a != b);
    case BinaryOp::Lt:  return cat::make_bool_literal(a < b);
    case BinaryOp::Gt:  return cat::make_bool_literal(a > b);
    case BinaryOp::Le:  return cat::make_bool_literal(a <= b);
    case BinaryOp::Ge:  return cat::make_bool_literal(a >= b);
    default: break;
    }
    return cat::make_int_literal(a);
  }

  Expr fold_float(BinaryOp op, float a, float b) {
    switch (op) {
    case BinaryOp::Add: return cat::make_float_literal(a + b);
    case BinaryOp::Sub: return cat::make_float_literal(a - b);
    case BinaryOp::Mul: return cat::make_float_literal(a * b);
    case BinaryOp::Div:
      if (b == 0) break;
      return cat::make_float_literal(a / b);
    case BinaryOp::Eq:  return cat::make_bool_literal(a == b);
    case BinaryOp::NotEq: return cat::make_bool_literal(a != b);
    case BinaryOp::Lt:  return cat::make_bool_literal(a < b);
    case BinaryOp::Gt:  return cat::make_bool_literal(a > b);
    case BinaryOp::Le:  return cat::make_bool_literal(a <= b);
    case BinaryOp::Ge:  return cat::make_bool_literal(a >= b);
    default: break;
    }
    return cat::make_float_literal(a);
  }
};

} // namespace cat::midend
