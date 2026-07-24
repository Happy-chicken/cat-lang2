#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class ConstantFolder : public ASTPass<ConstantFolder> {
public:
  void on_function(FunctionDef &fn) { walk_block(fn.body); }

  void on_binary(Expr &parent, BinaryExpr &bin) {
    int64_t li, ri;
    float lf, rf;

    if (is_literal(*bin.lhs, li) && is_literal(*bin.rhs, ri)) {
      parent = fold_int(bin.op, li, ri);
    } else if (is_literal(*bin.lhs, lf) && is_literal(*bin.rhs, rf)) {
      parent = fold_float(bin.op, lf, rf);
    }
  }

private:
  Expr fold_int(BinaryOp op, int64_t a, int64_t b) {
    switch (op) {
    case BinaryOp::Add:
      return make_int_literal(a + b);
    case BinaryOp::Sub:
      return make_int_literal(a - b);
    case BinaryOp::Mul:
      return make_int_literal(a * b);
    case BinaryOp::Div:
      if (b == 0)
        break;
      return make_int_literal(a / b);
    case BinaryOp::And:
      return make_bool_literal(a && b);
    case BinaryOp::Or:
      return make_bool_literal(a || b);
    case BinaryOp::Eq:
      return make_bool_literal(a == b);
    case BinaryOp::NotEq:
      return make_bool_literal(a != b);
    case BinaryOp::Lt:
      return make_bool_literal(a < b);
    case BinaryOp::Gt:
      return make_bool_literal(a > b);
    case BinaryOp::Le:
      return make_bool_literal(a <= b);
    case BinaryOp::Ge:
      return make_bool_literal(a >= b);
    default:
      break;
    }
    return make_int_literal(a);
  }

  Expr fold_float(BinaryOp op, float a, float b) {
    switch (op) {
    case BinaryOp::Add:
      return make_float_literal(a + b);
    case BinaryOp::Sub:
      return make_float_literal(a - b);
    case BinaryOp::Mul:
      return make_float_literal(a * b);
    case BinaryOp::Div:
      if (b == 0)
        break;
      return make_float_literal(a / b);
    case BinaryOp::Eq:
      return make_bool_literal(a == b);
    case BinaryOp::NotEq:
      return make_bool_literal(a != b);
    case BinaryOp::Lt:
      return make_bool_literal(a < b);
    case BinaryOp::Gt:
      return make_bool_literal(a > b);
    case BinaryOp::Le:
      return make_bool_literal(a <= b);
    case BinaryOp::Ge:
      return make_bool_literal(a >= b);
    default:
      break;
    }
    return make_float_literal(a);
  }
};

} // namespace cat::opt::ast
