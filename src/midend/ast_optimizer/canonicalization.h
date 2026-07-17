#pragma once

#include "ast_pass.h"

namespace cat::opt::ast {

class Canonicalization : public ASTPass<Canonicalization> {
public:
  static constexpr const char *name = "Canonicalization";

  // ensure that in a binary expression, the literal is always on the RHS
  void before_binary(BinaryExpr &bin) {
    if (bin.op == BinaryOp::Add || bin.op == BinaryOp::Mul ||
        bin.op == BinaryOp::Eq || bin.op == BinaryOp::NotEq) {
      if (std::holds_alternative<LiteralExpr>(bin.lhs->expr) &&
          !std::holds_alternative<LiteralExpr>(bin.rhs->expr)) {
        std::swap(bin.lhs, bin.rhs);
      }
    }
  }

  // normalize if/elif/else to have else_branch always present (empty block)
  void after_if_stmt(IfStmt &ifs) {
    if (!ifs.else_branch && ifs.elif_branch.empty()) {
      ifs.else_branch = std::make_unique<Block>();
    }
  }
};

} // namespace cat::midend
