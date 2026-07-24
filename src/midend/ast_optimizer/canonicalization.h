#pragma once

#include "ast_pass.h"

namespace cat::opt::ast {

class Canonicalization : public ASTPass<Canonicalization> {
public:
  void on_function(FunctionDef &fn) { walk_block(fn.body); }

  void on_binary(Expr &, BinaryExpr &bin) {
    if (bin.op == BinaryOp::Add || bin.op == BinaryOp::Mul ||
        bin.op == BinaryOp::Eq || bin.op == BinaryOp::NotEq) {
      if (std::holds_alternative<LiteralExpr>(bin.lhs->expr) &&
          !std::holds_alternative<LiteralExpr>(bin.rhs->expr)) {
        std::swap(bin.lhs, bin.rhs);
      }
    }
  }

  void on_if_stmt(IfStmt &ifs) {
    if (!ifs.else_branch && ifs.elif_branch.empty()) {
      ifs.else_branch = std::make_unique<Block>();
    }
  }
};

} // namespace cat::opt::ast
