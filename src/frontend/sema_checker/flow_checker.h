#pragma once
#include "pass_interface.h"
#include "sema_checker.h"
#include "stmt.h"
#include "type.h"
namespace cat {
  enum class Terminator {
    Returns,
    Diverges,
    FallsThrough
  };

  class FlowChecker : public semantics::Pass {

    const char *name() const noexcept override {
      return "FlowChecker";
    }

    bool run(Program &program, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) override {
      sym_table = &ctx.get_symbol_table();
      for (const auto& item_node: program.items) {
        std::visit(overloaded{
            [&](const FunctionDef& func) {
              ast::Type return_type = func.function_header.return_type ? func.function_header.return_type->clone() : ast::type_void();
              check_function_returns(func, return_type, diag);
            },
            [&](const Impl& imp) {
              for (const auto& method: imp.methods) {
                ast::Type return_type = method.function_header.return_type ? method.function_header.return_type->clone() : ast::type_void();
                check_function_returns(method, return_type, diag);
              }
            },
            [&](const auto&) {}}, item_node.item);
      }
      return !diag.has_errors();
    }

  void check_function_returns(const FunctionDef &func, const ast::Type& return_type, error::DiagCtxt &diag);

private:
  Terminator analyze_block(const Block& block, error::DiagCtxt &diag);
  Terminator analyze_stmt(const StmtNode& stmt, error::DiagCtxt &diag);
  Terminator analyze_if_stmt(const IfStmt& if_stmt, error::DiagCtxt &diag);
  void check_expr(const ExprNode &expr, error::DiagCtxt &diag);

  SymbolTable *sym_table = nullptr;
  unordered_set<string> moved;
  };
}
