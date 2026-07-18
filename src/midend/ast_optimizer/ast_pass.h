#pragma once

#include "expr.h"
#include "item.h"
#include "stmt.h"
#include <variant>

namespace cat::opt::ast {

template <typename Derived>
class ASTPass {
public:
  void run(Program &program) {
    auto &self = static_cast<Derived &>(*this);
    for (auto &item : program.items) {
      visit_item(item);
    }
  }

  void on_function(FunctionDef &) {}

protected:
  void walk_block(Block &block);

  void walk_stmt(StmtNode &node);

  void walk_expr(Expr &expr);

  void on_block(Block &) {}
  void on_if_stmt(IfStmt &) {}
  void on_binary(Expr &, BinaryExpr &) {}
  void on_unary(Expr &, UnaryExpr &) {}

private:
  void visit_item(ItemNode &node) {
    auto &self = static_cast<Derived &>(*this);
    std::visit(
        [&](auto &item) {
          using T = std::decay_t<decltype(item)>;
          if constexpr (std::is_same_v<T, FunctionDef>) {
            self.on_function(item);
            walk_block(item.body);
          } else if constexpr (std::is_same_v<T, Impl>) {
            for (auto &m : item.methods) {
              self.on_function(m);
              walk_block(m.body);
            }
          }
        },
        node.item
    );
  }
};

// ---------- out-of-line definitions ----------

template <typename Derived>
void ASTPass<Derived>::walk_block(Block &block) {
  auto &self = static_cast<Derived &>(*this);
  for (auto &stmt : block.stmts) {
    walk_stmt(stmt);
  }
  self.on_block(block);
}

template <typename Derived>
void ASTPass<Derived>::walk_stmt(StmtNode &node) {
  auto &self = static_cast<Derived &>(*this);
  std::visit(
      [&](auto &stmt) {
        using T = std::decay_t<decltype(stmt)>;
        if constexpr (std::is_same_v<T, VarDefStmt>) {
          if (stmt.init) walk_expr(stmt.init->expr);
        } else if constexpr (std::is_same_v<T, IfStmt>) {
          self.on_if_stmt(stmt);
          walk_expr(stmt.condition.expr);
          walk_block(*stmt.then_branch);
          for (auto &[cond, block] : stmt.elif_branch) {
            walk_expr(cond.expr);
            walk_block(*block);
          }
          if (stmt.else_branch) walk_block(*stmt.else_branch);
        } else if constexpr (std::is_same_v<T, LoopStmt>) {
          walk_expr(stmt.condition.expr);
          walk_block(*stmt.body);
        } else if constexpr (std::is_same_v<T, ReturnStmt>) {
          if (stmt.expr) walk_expr(stmt.expr->expr);
        } else if constexpr (std::is_same_v<T, ExprStmt>) {
          walk_expr(stmt.expr.expr);
        } else if constexpr (std::is_same_v<T, BlockStmt>) {
          walk_block(*stmt.block);
        }
      },
      node.stmt
  );
}

template <typename Derived>
void ASTPass<Derived>::walk_expr(Expr &expr) {
  auto &self = static_cast<Derived &>(*this);
  std::visit(
      [&](auto &e) {
        using T = std::decay_t<decltype(e)>;
        if constexpr (std::is_same_v<T, BinaryExpr>) {
          walk_expr(e.lhs->expr);
          walk_expr(e.rhs->expr);
          self.on_binary(expr, e);
        } else if constexpr (std::is_same_v<T, UnaryExpr>) {
          walk_expr(e.expr->expr);
          self.on_unary(expr, e);
        } else if constexpr (std::is_same_v<T, AssignExpr>) {
          walk_expr(e.target->expr);
          walk_expr(e.value->expr);
        } else if constexpr (std::is_same_v<T, CallExpr>) {
          walk_expr(e.callee->expr);
          for (auto &arg : e.args) walk_expr(arg->expr);
        } else if constexpr (std::is_same_v<T, MemberExpr>) {
          walk_expr(e.object->expr);
        } else if constexpr (std::is_same_v<T, IndexExpr>) {
          walk_expr(e.object->expr);
          walk_expr(e.index->expr);
        } else if constexpr (std::is_same_v<T, ListExpr>) {
          for (auto &elem : e.elements) walk_expr(elem->expr);
        }
      },
      expr
  );
}

}// namespace cat::opt::ast
