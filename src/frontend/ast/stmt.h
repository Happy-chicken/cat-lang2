#pragma once
#include "expr.h"
#include "type.h"
namespace cat {

struct Block;

struct VarDefStmt {
  string name;
  optional<ast::Type> ty;
  optional<ExprNode> init;
};

struct IfStmt {
  ExprNode condition;
  uptr<Block> then_branch;
  vector<std::pair<ExprNode, uptr<Block>>> elif_branch;
  uptr<Block> else_branch;
};

struct LoopStmt {
  ExprNode condition;
  uptr<Block> body;
};

struct ExprStmt {
  ExprNode expr;
};

struct ReturnStmt {
  std::optional<ExprNode> expr;
};

struct BreakStmt {};

struct ContinueStmt {};

struct BlockStmt {
  uptr<Block> block;
};

using Stmt = std::variant<VarDefStmt, IfStmt, LoopStmt, ExprStmt, ReturnStmt,
                          BreakStmt, ContinueStmt, BlockStmt>;

struct StmtNode {
  Span span;
  Stmt stmt;
};

struct Block {
  vector<StmtNode> stmts;
};

inline Stmt make_var_def(std::string name,
                         std::optional<ast::Type> ty = std::nullopt,
                         std::optional<ExprNode> init = std::nullopt) {
  return Stmt{VarDefStmt{std::move(name), std::move(ty), std::move(init)}};
}

inline Stmt make_if(ExprNode cond, uptr<Block> then_blk,
                    vector<std::pair<ExprNode, uptr<Block>>> elifs = {},
                    uptr<Block> else_blk = nullptr) {
  return Stmt{IfStmt{std::move(cond), std::move(then_blk), std::move(elifs),
                     std::move(else_blk)}};
}

inline Stmt make_loop(ExprNode cond, uptr<Block> body) {
  return Stmt{LoopStmt{std::move(cond), std::move(body)}};
}

inline Stmt make_expr_stmt(ExprNode expr) {
  return Stmt{ExprStmt{std::move(expr)}};
}

inline Stmt make_return(std::optional<ExprNode> expr = std::nullopt) {
  return Stmt{ReturnStmt{std::move(expr)}};
}

inline Stmt make_break() { return Stmt{BreakStmt{}}; }
inline Stmt make_continue() { return Stmt{ContinueStmt{}}; }
inline Stmt make_block_stmt(uptr<Block> block) {
  return Stmt{BlockStmt{std::move(block)}};
}

inline uptr<Block> make_block(vector<StmtNode> stmts = {}) {
  return std::make_unique<Block>(Block{std::move(stmts)});
}
} // namespace cat
