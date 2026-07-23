#pragma once
#include "pass_interface.h"
#include "sema_checker.h"
#include "stmt.h"
#include "type.h"
#include <set>
#include <string>
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
      sema_ctx = &ctx;
      for (const auto& item_node: program.items) {
        std::visit(overloaded{
            [&](const FunctionDef& func) {
              ast::Type return_type = func.function_header.return_type ? func.function_header.return_type->clone() : ast::type_void();
              check_function_returns(func.body, return_type, diag);
            },
            [&](const Impl& imp) {
              for (const auto& method: imp.methods) {
                ast::Type return_type = method.function_header.return_type ? method.function_header.return_type->clone() : ast::type_void();
                check_function_returns(method.body, return_type, diag);
              }
            },
            [&](const auto&) {}}, item_node.item);
      }
      return !diag.has_errors();
    }

  void check_function_returns(const Block& block, const ast::Type& return_type, error::DiagCtxt &diag);

private:
  Terminator analyze_block(const Block& block, error::DiagCtxt &diag, std::set<std::string> &moved);

  Terminator analyze_stmt(const StmtNode& stmt, error::DiagCtxt &diag, std::set<std::string> &moved);

  Terminator analyze_if_stmt(const IfStmt& if_stmt, error::DiagCtxt &diag, std::set<std::string> &moved);

  void check_expr_for_moved(const ExprNode &expr, const std::set<std::string> &moved, error::DiagCtxt &diag);

  void mark_own_args(const ExprNode &expr, std::set<std::string> &moved, error::DiagCtxt &diag);

  void collect_variable_names(const ExprNode &expr, std::vector<std::string> &names);

  semantics::SemaCtxt *sema_ctx = nullptr;
  };
}
