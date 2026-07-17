#include "flow_checker.h"
#include "stmt.h"
#include <variant>

namespace cat {
static bool is_terminal(Terminator term) {
  return term == Terminator::Returns || term == Terminator::Diverges;
}

void FlowChecker::check_function_returns(const Block &block,
                                         const ast::Type &return_type,
                                         error::DiagCtxt &diag) {
  auto term = analyze_block(block, diag);
  if (std::holds_alternative<ast::Type::Void>(return_type.data))
    return;
  if (term != Terminator::Returns) {
    auto span = block.stmts.empty() ? Span{0, 0} : block.stmts.back().span;
    diag.error(span,
               "Function with non-void return type must return a value "
               "on all paths")
        .emit_to(diag);
  }
}

Terminator FlowChecker::analyze_block(const Block &block,
                                      error::DiagCtxt &diag) {
  Terminator term = Terminator::FallsThrough;
  for (size_t i = 0; i < block.stmts.size(); ++i) {
    term = analyze_stmt(block.stmts[i], diag);
    if (is_terminal(term)) {
      if (i + 1 < block.stmts.size()) {
        diag.warn("Unreachable code")
            .span(block.stmts[i + 1].span)
            .emit_to(diag);
      }
      break;
    }
  }
  return term;
}

Terminator FlowChecker::analyze_stmt(const StmtNode &stmt,
                                     error::DiagCtxt &diag) {
  return std::visit(
      overloaded{
          [&](const ReturnStmt &) { return Terminator::Returns; },
          [&](const LoopStmt &loop_stmt) {
            auto term = analyze_block(*loop_stmt.body, diag);
            if (term == Terminator::Returns) {
              return Terminator::Returns;
            }
            return Terminator::FallsThrough;
          },
          [&](const IfStmt &if_stmt) {
            return analyze_if_stmt(if_stmt, diag);
          },
          [&](const ContinueStmt &) { return Terminator::Diverges; },
          [&](const BreakStmt &) { return Terminator::Diverges; },
          [&](const BlockStmt &block_stmt) {
            return analyze_block(*block_stmt.block, diag);
          },
          [&](const ExprStmt &) { return Terminator::FallsThrough; },
          [&](const VarDefStmt &) { return Terminator::FallsThrough; },
          [&](const auto &) { return Terminator::FallsThrough; },
      },
      stmt.stmt);
}

Terminator FlowChecker::analyze_if_stmt(const IfStmt &if_stmt,
                                        error::DiagCtxt &diag) {
  auto then_term = analyze_block(*if_stmt.then_branch, diag);

  std::vector<Terminator> elif_terms;
  for (const auto &[cond, block] : if_stmt.elif_branch) {
    elif_terms.push_back(analyze_block(*block, diag));
  }

  auto else_term = if_stmt.else_branch
                       ? analyze_block(*if_stmt.else_branch, diag)
                       : Terminator::FallsThrough;

  bool has_else = if_stmt.else_branch != nullptr;

  bool all_return = (then_term == Terminator::Returns) &&
                    std::all_of(elif_terms.begin(), elif_terms.end(),
                                [](Terminator t) {
                                  return t == Terminator::Returns;
                                }) &&
                    (!has_else || else_term == Terminator::Returns);

  if (all_return && has_else)
    return Terminator::Returns;

  bool all_diverge =
      (then_term == Terminator::Diverges) &&
      std::all_of(elif_terms.begin(), elif_terms.end(),
                  [](Terminator t) { return t == Terminator::Diverges; }) &&
      (!has_else || else_term == Terminator::Diverges);

  if (all_diverge && has_else)
    return Terminator::Diverges;

  return Terminator::FallsThrough;
}

} // namespace cat
