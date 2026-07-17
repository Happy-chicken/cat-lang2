#pragma once

#include "expr.h"
#include "stmt.h"
#include "item.h"
#include <variant>

namespace cat::opt::ast {

template <typename Derived>
class ASTPass {
public:
  constexpr const char *name() const { return Derived::name; }

  void run(Program &program) {
    auto &self = static_cast<Derived &>(*this);
    self.before_run(program);
    for (auto &item : program.items) {
      self.on_item(item);
    }
    self.after_run(program);
  }

  // ---- Item traversal ----
  void on_item(ItemNode &node) {
    std::visit(
        [this](auto &item) {
          static_cast<Derived *>(this)->visit_item_impl(item);
        },
        node.item);
  }

  void visit_item_impl(FunctionDef &def) {
    auto &self = static_cast<Derived &>(*this);
    self.before_function_def(def);
    self.visit_block(def.body);
    self.after_function_def(def);
  }
  void visit_item_impl(FunctionDecl &) {}
  void visit_item_impl(Trait &) {}

  void visit_item_impl(Class &cls) {
    auto &self = static_cast<Derived &>(*this);
    self.before_class(cls);
    for (auto &field : cls.fields) {
      if (field.init) self.on_expr(*field.init);
    }
    self.after_class(cls);
  }

  void visit_item_impl(Impl &imp) {
    auto &self = static_cast<Derived &>(*this);
    self.before_impl(imp);
    for (auto &method : imp.methods) {
      self.visit_item_impl(method);
    }
    self.after_impl(imp);
  }

  void visit_item_impl(GlobalVar &gv) {
    auto &self = static_cast<Derived &>(*this);
    self.before_global_var(gv);
    if (gv.init) self.on_expr(*gv.init);
    self.after_global_var(gv);
  }

  // ---- Block traversal ----
  void visit_block(Block &block) {
    auto &self = static_cast<Derived &>(*this);
    self.before_block(block);
    for (auto &stmt : block.stmts) {
      self.on_stmt(stmt);
    }
    self.after_block(block);
  }

  // ---- Statement traversal ----
  void on_stmt(StmtNode &node) {
    std::visit(
        [this](auto &stmt) {
          static_cast<Derived *>(this)->visit_stmt_impl(stmt);
        },
        node.stmt);
  }

  void visit_stmt_impl(VarDefStmt &def) {
    auto &self = static_cast<Derived &>(*this);
    self.before_var_def(def);
    if (def.init) self.on_expr(*def.init);
    self.after_var_def(def);
  }

  void visit_stmt_impl(IfStmt &ifs) {
    auto &self = static_cast<Derived &>(*this);
    self.before_if_stmt(ifs);
    self.on_expr(ifs.condition);
    self.visit_block(*ifs.then_branch);
    for (auto &[cond, block] : ifs.elif_branch) {
      self.on_expr(cond);
      self.visit_block(*block);
    }
    if (ifs.else_branch) self.visit_block(*ifs.else_branch);
    self.after_if_stmt(ifs);
  }

  void visit_stmt_impl(LoopStmt &loop) {
    auto &self = static_cast<Derived &>(*this);
    self.before_loop(loop);
    self.on_expr(loop.condition);
    self.visit_block(*loop.body);
    self.after_loop(loop);
  }

  void visit_stmt_impl(ReturnStmt &ret) {
    auto &self = static_cast<Derived &>(*this);
    self.before_return(ret);
    if (ret.expr) self.on_expr(*ret.expr);
    self.after_return(ret);
  }

  void visit_stmt_impl(ExprStmt &es) {
    auto &self = static_cast<Derived &>(*this);
    self.before_expr_stmt(es);
    self.on_expr(es.expr);
    self.after_expr_stmt(es);
  }

  void visit_stmt_impl(BreakStmt &) {}
  void visit_stmt_impl(ContinueStmt &) {}
  void visit_stmt_impl(BlockStmt &bs) {
    auto &self = static_cast<Derived &>(*this);
    self.visit_block(*bs.block);
  }

  // ---- Expression traversal ----
  void on_expr(ExprNode &node) {
    std::visit(
        [this](auto &expr) {
          static_cast<Derived *>(this)->visit_expr_impl(expr);
        },
        node.expr);
  }

  void visit_expr_impl(LiteralExpr &) {}
  void visit_expr_impl(Variable &) {}

  void visit_expr_impl(AssignExpr &assign) {
    auto &self = static_cast<Derived &>(*this);
    self.on_expr(*assign.target);
    self.on_expr(*assign.value);
  }

  void visit_expr_impl(BinaryExpr &bin) {
    auto &self = static_cast<Derived &>(*this);
    self.before_binary(bin);
    self.on_expr(*bin.lhs);
    self.on_expr(*bin.rhs);
    self.after_binary(bin);
  }

  void visit_expr_impl(UnaryExpr &unary) {
    auto &self = static_cast<Derived &>(*this);
    self.before_unary(unary);
    self.on_expr(*unary.expr);
    self.after_unary(unary);
  }

  void visit_expr_impl(CallExpr &call) {
    auto &self = static_cast<Derived &>(*this);
    self.before_call(call);
    self.on_expr(*call.callee);
    for (auto &arg : call.args) self.on_expr(*arg);
    self.after_call(call);
  }

  void visit_expr_impl(MemberExpr &m) {
    auto &self = static_cast<Derived &>(*this);
    self.on_expr(*m.object);
  }

  void visit_expr_impl(IndexExpr &idx) {
    auto &self = static_cast<Derived &>(*this);
    self.on_expr(*idx.object);
    self.on_expr(*idx.index);
  }

  void visit_expr_impl(ListExpr &list) {
    auto &self = static_cast<Derived &>(*this);
    for (auto &elem : list.elements) self.on_expr(*elem);
  }

  // ---- Default hooks (override in Derived) ----
  void before_run(Program &) {}
  void after_run(Program &) {}
  void before_function_def(FunctionDef &) {}
  void after_function_def(FunctionDef &) {}
  void before_class(Class &) {}
  void after_class(Class &) {}
  void before_impl(Impl &) {}
  void after_impl(Impl &) {}
  void before_global_var(GlobalVar &) {}
  void after_global_var(GlobalVar &) {}
  void before_block(Block &) {}
  void after_block(Block &) {}
  void before_var_def(VarDefStmt &) {}
  void after_var_def(VarDefStmt &) {}
  void before_if_stmt(IfStmt &) {}
  void after_if_stmt(IfStmt &) {}
  void before_loop(LoopStmt &) {}
  void after_loop(LoopStmt &) {}
  void before_return(ReturnStmt &) {}
  void after_return(ReturnStmt &) {}
  void before_expr_stmt(ExprStmt &) {}
  void after_expr_stmt(ExprStmt &) {}
  void before_binary(BinaryExpr &) {}
  void after_binary(BinaryExpr &) {}
  void before_unary(UnaryExpr &) {}
  void after_unary(UnaryExpr &) {}
  void before_call(CallExpr &) {}
  void after_call(CallExpr &) {}
};

} // namespace cat::midend
