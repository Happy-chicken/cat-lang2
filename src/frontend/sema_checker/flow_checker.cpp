#include "flow_checker.h"
#include "stmt.h"
#include "symbol_table.h"
#include <variant>

namespace cat {
static bool is_terminal(Terminator term) {
  return term == Terminator::Returns || term == Terminator::Diverges;
}

void FlowChecker::check_function_returns(const FunctionDef &func,
                                         const ast::Type &return_type,
                                         error::DiagCtxt &diag) {
  moved.clear();
  auto term = analyze_block(func.body, diag);

  if (std::holds_alternative<ast::Type::Void>(return_type.data))
    return;
  if (term != Terminator::Returns) {
    auto span =
        func.body.stmts.empty() ? Span{0, 0} : func.body.stmts.back().span;
    diag.error(span, "Function with non-void return type must return a value "
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
          [&](const ReturnStmt &s) {
            if (s.expr.has_value())
              check_expr(*s.expr, diag);
            return Terminator::Returns;
          },
          [&](const LoopStmt &loop_stmt) {
            auto saved = moved;
            check_expr(loop_stmt.condition, diag);
            auto body_start = moved;
            auto term = analyze_block(*loop_stmt.body, diag);
            for (auto &v : moved)
              body_start.insert(v);
            for (auto &v : body_start)
              saved.insert(v);
            moved = std::move(saved);
            if (term == Terminator::Returns)
              return Terminator::Returns;
            return Terminator::FallsThrough;
          },
          [&](const IfStmt &if_stmt) { return analyze_if_stmt(if_stmt, diag); },
          [&](const ContinueStmt &) { return Terminator::Diverges; },
          [&](const BreakStmt &) { return Terminator::Diverges; },
          [&](const BlockStmt &block_stmt) {
            return analyze_block(*block_stmt.block, diag);
          },
          [&](const ExprStmt &es) {
            check_expr(es.expr, diag);
            return Terminator::FallsThrough;
          },
          [&](const VarDefStmt &vds) {
            if (vds.init.has_value())
              check_expr(*vds.init, diag);
            if (vds.ty && std::get_if<ast::Type::Own>(&vds.ty->data))
              if (auto *var = std::get_if<Variable>(&vds.init->expr))
                moved.insert(var->name);
            return Terminator::FallsThrough;
          },
          [&](const auto &) { return Terminator::FallsThrough; },
      },
      stmt.stmt);
}

Terminator FlowChecker::analyze_if_stmt(const IfStmt &if_stmt,
                                        error::DiagCtxt &diag) {
  check_expr(if_stmt.condition, diag);

  auto saved = moved;

  moved = saved;
  auto then_term = analyze_block(*if_stmt.then_branch, diag);
  auto then_moved = std::move(moved);

  std::vector<Terminator> elif_terms;
  std::vector<decltype(moved)> elif_moved;
  for (const auto &[cond, block] : if_stmt.elif_branch) {
    moved = saved;
    check_expr(cond, diag);
    elif_terms.push_back(analyze_block(*block, diag));
    elif_moved.push_back(std::move(moved));
  }

  moved = saved;
  auto else_term = Terminator::FallsThrough;
  if (if_stmt.else_branch)
    else_term = analyze_block(*if_stmt.else_branch, diag);
  auto else_moved = std::move(moved);

  moved = std::move(then_moved);
  for (auto &em : elif_moved)
    for (auto &v : em)
      moved.insert(v);
  for (auto &v : else_moved)
    moved.insert(v);

  bool has_else = if_stmt.else_branch != nullptr;
  bool all_return =
      (then_term == Terminator::Returns) &&
      std::all_of(elif_terms.begin(), elif_terms.end(),
                  [](Terminator t) { return t == Terminator::Returns; }) &&
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

void FlowChecker::check_expr(const ExprNode &expr, error::DiagCtxt &diag) {
  std::visit(
      overloaded{
          [&](const Variable &v) {
            if (moved.count(v.name))
              diag.error(expr.span, "Variable '" + v.name +
                                        "' was moved and cannot be used here")
                  .emit_to(diag);
          },
          [&](const CallExpr &call) {
            check_expr(*call.callee, diag);
            for (auto &arg : call.args)
              check_expr(*arg, diag);

            // Extract the mangled function name (e.g. "ClassName_method" for
            // methods)
            auto fn_name = std::visit(
                overloaded{
                    [](const Variable &v) -> std::string { return v.name; },
                    [](const MemberExpr &m) -> std::string {
                      if (auto *obj = std::get_if<Variable>(&m.object->expr))
                        return obj->name + "_" + m.field;
                      return "";
                    },
                    [](const auto &) -> std::string { return ""; },
                },
                call.callee->expr);

            if (fn_name.empty())
              return;

            auto *fn_sym = sym_table->resolve(fn_name);
            if (!fn_sym)
              return;

            // Mark ownership args as moved (FunctionData path)
            if (auto *fn_data =
                    std::get_if<FunctionData>(&fn_sym->get_kind())) {
              for (size_t i = 0;
                   i < fn_data->params.size() && i < call.args.size(); ++i) {
                if (auto *var = std::get_if<Variable>(&call.args[i]->expr))
                  if (std::get_if<ast::Type::Own>(&fn_data->params[i].data))
                    moved.insert(var->name);
              }
              return;
            }

            // Mark ownership args as moved (function-typed variable path)
            const auto &sym_ty = fn_sym->get_type();
            if (sym_ty.has_value()) {
              if (auto *func_ty = std::get_if<ast::Type::Func>(&sym_ty->data)) {
                for (size_t i = 0;
                     i < func_ty->params.size() && i < call.args.size(); ++i) {
                  if (!func_ty->params[i])
                    continue;
                  if (auto *var = std::get_if<Variable>(&call.args[i]->expr))
                    if (std::get_if<ast::Type::Own>(&func_ty->params[i]->data))
                      moved.insert(var->name);
                }
              }
            }
          },
          [&](const AssignExpr &a) {
            check_expr(*a.target, diag);
            check_expr(*a.value, diag);
          },
          [&](const BinaryExpr &b) {
            check_expr(*b.lhs, diag);
            check_expr(*b.rhs, diag);
          },
          [&](const UnaryExpr &u) { check_expr(*u.expr, diag); },
          [&](const MemberExpr &m) { check_expr(*m.object, diag); },
          [&](const IndexExpr &i) {
            check_expr(*i.object, diag);
            check_expr(*i.index, diag);
          },
          [&](const LambdaExpr &f) {
            if (f.body)
              analyze_block(*f.body, diag);
          },
          [&](const auto &) {},
      },
      expr.expr);
}

} // namespace cat
