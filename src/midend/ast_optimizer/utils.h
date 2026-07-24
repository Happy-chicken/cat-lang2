#pragma once

#include "common/common.h"
#include "expr.h"
#include <utility>

namespace cat::opt::ast {

template <typename ValueType>
bool is_literal(const ExprNode &node, ValueType &out) {
  if (auto *lit = std::get_if<LiteralExpr>(&node.expr)) {
    if (auto *v = std::get_if<ValueType>(&lit->lit)) {
      out = *v;
      return true;
    }
  }
  return false;
}

inline bool is_truthy(const ExprNode &node) {
  bool val = false;
  return is_literal(node, val) && val;
}

inline bool is_falsy(const ExprNode &node) {
  bool val = true;
  return is_literal(node, val) && !val;
}

inline bool is_int_zero(const ExprNode &node) {
  int64_t val = 1;
  return is_literal(node, val) && val == 0;
}

// move a new Expr value into an ExprNode's expr field
inline void set_expr(ExprNode &node, Expr &&e) { node.expr = std::move(e); }

inline void set_expr(uptr<ExprNode> &node, Expr &&e) {
  node->expr = std::move(e);
}

} // namespace cat::opt::ast
