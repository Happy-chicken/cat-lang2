#pragma once

#include "../../src/common/common.h"
#include <variant>
namespace cat {

struct ExprNode;

enum class BinaryOp { Add, Sub, Mul, Div, Eq, NotEq, Lt, Gt, Le, Ge, And, Or };

enum class UnaryOp { Neg, Not, Inc, Dec, Deref, AddrOf };

using Literal = std::variant<int64_t,    // Int
                             bool,       // Bool
                             float,      // Float
                             char,       // Char
                             std::string // StringLiteral
                             >;

struct LiteralExpr {
  Literal lit;
};

struct Variable {
  std::string name;
};

struct AssignExpr {
  uptr<ExprNode> target;
  uptr<ExprNode> value;
};

struct BinaryExpr {
  BinaryOp op;
  uptr<ExprNode> lhs;
  uptr<ExprNode> rhs;
};

struct UnaryExpr {
  UnaryOp op;
  uptr<ExprNode> expr;
};

struct CallExpr {
  uptr<ExprNode> callee;
  vector<uptr<ExprNode>> args;
};

struct MemberExpr {
  uptr<ExprNode> object;
  std::string field;
};

struct IndexExpr {
  uptr<ExprNode> object;
  uptr<ExprNode> index;
};

struct ListExpr {
  vector<uptr<ExprNode>> elements;
};

using Expr = std::variant<LiteralExpr, Variable, AssignExpr, BinaryExpr,
                          UnaryExpr, CallExpr, MemberExpr, IndexExpr, ListExpr>;

struct ExprNode {
  Span span;
  Expr expr;
};

inline auto make_int_literal(int64_t v) { return Expr{LiteralExpr{v}}; }
inline auto make_bool_literal(bool v) { return Expr{LiteralExpr{v}}; }
inline auto make_float_literal(float v) { return Expr{LiteralExpr{v}}; }
inline auto make_char_literal(char v) { return Expr{LiteralExpr{v}}; }
inline auto make_string_literal(std::string v) {
  return Expr{LiteralExpr{std::move(v)}};
}

inline auto make_variable(std::string name) {
  return Expr{Variable{std::move(name)}};
}

inline auto make_assign(uptr<ExprNode> target, uptr<ExprNode> value) {
  return Expr{AssignExpr{std::move(target), std::move(value)}};
}

inline auto make_binary(BinaryOp op, uptr<ExprNode> lhs, uptr<ExprNode> rhs) {
  return Expr{BinaryExpr{op, std::move(lhs), std::move(rhs)}};
}

inline auto make_unary(UnaryOp op, uptr<ExprNode> expr) {
  return Expr{UnaryExpr{op, std::move(expr)}};
}

inline auto make_call(uptr<ExprNode> callee, vector<uptr<ExprNode>> args) {
  return Expr{CallExpr{std::move(callee), std::move(args)}};
}

inline auto make_member(uptr<ExprNode> object, std::string field) {
  return Expr{MemberExpr{std::move(object), std::move(field)}};
}

inline auto make_index(uptr<ExprNode> object, uptr<ExprNode> index) {
  return Expr{IndexExpr{std::move(object), std::move(index)}};
}

inline auto make_list(vector<uptr<ExprNode>> elements) {
  return Expr{ListExpr{std::move(elements)}};
}

} // namespace cat
