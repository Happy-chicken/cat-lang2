#include "flow_checker.h"
#include "stmt.h"
#include "symbol_table.h"
#include <variant>

namespace cat {
static bool is_terminal(Terminator term) {
  return term == Terminator::Returns || term == Terminator::Diverges;
}

void FlowChecker::check_function_returns(const Block &block,
                                         const ast::Type &return_type,
                                         error::DiagCtxt &diag) {
  std::set<std::string> moved;
  auto term = analyze_block(block, diag, moved);
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
                                      error::DiagCtxt &diag,
                                      std::set<std::string> &moved) {
  Terminator term = Terminator::FallsThrough;
  for (size_t i = 0; i < block.stmts.size(); ++i) {
    term = analyze_stmt(block.stmts[i], diag, moved);
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
                                     error::DiagCtxt &diag,
                                     std::set<std::string> &moved) {
  return std::visit(
      overloaded{
          [&](const ReturnStmt &s) {
            if (s.expr.has_value()) {
              check_expr_for_moved(*s.expr, moved, diag);
              mark_own_args(*s.expr, moved, diag);
            }
            return Terminator::Returns;
          },
          [&](const LoopStmt &loop_stmt) {
            std::set<std::string> body_moved = moved;
            check_expr_for_moved(loop_stmt.condition, body_moved, diag);
            mark_own_args(loop_stmt.condition, body_moved, diag);
            auto term = analyze_block(*loop_stmt.body, diag, body_moved);
            for (auto &v: body_moved) moved.insert(v);
            if (term == Terminator::Returns)
              return Terminator::Returns;
            return Terminator::FallsThrough;
          },
          [&](const IfStmt &if_stmt) {
            return analyze_if_stmt(if_stmt, diag, moved);
          },
          [&](const ContinueStmt &) { return Terminator::Diverges; },
          [&](const BreakStmt &) { return Terminator::Diverges; },
          [&](const BlockStmt &block_stmt) {
            return analyze_block(*block_stmt.block, diag, moved);
          },
          [&](const ExprStmt &es) {
            check_expr_for_moved(es.expr, moved, diag);
            mark_own_args(es.expr, moved, diag);
            return Terminator::FallsThrough;
          },
          [&](const VarDefStmt &vds) {
            if (vds.init.has_value()) {
              check_expr_for_moved(*vds.init, moved, diag);
              mark_own_args(*vds.init, moved, diag);
            }
            return Terminator::FallsThrough;
          },
          [&](const auto &) { return Terminator::FallsThrough; },
      },
      stmt.stmt);
}

Terminator FlowChecker::analyze_if_stmt(const IfStmt &if_stmt,
                                        error::DiagCtxt &diag,
                                        std::set<std::string> &moved) {
  check_expr_for_moved(if_stmt.condition, moved, diag);
  mark_own_args(if_stmt.condition, moved, diag);

  std::set<std::string> then_moved = moved;
  auto then_term = analyze_block(*if_stmt.then_branch, diag, then_moved);

  std::vector<Terminator> elif_terms;
  std::vector<std::set<std::string>> elif_moved_sets;
  for (const auto &[cond, block] : if_stmt.elif_branch) {
    std::set<std::string> el_moved = moved;
    check_expr_for_moved(cond, el_moved, diag);
    mark_own_args(cond, el_moved, diag);
    elif_terms.push_back(analyze_block(*block, diag, el_moved));
    elif_moved_sets.push_back(std::move(el_moved));
  }

  bool has_else = if_stmt.else_branch != nullptr;
  std::set<std::string> else_moved = moved;
  auto else_term = Terminator::FallsThrough;
  if (has_else) {
    else_term = analyze_block(*if_stmt.else_branch, diag, else_moved);
  }

  moved = std::move(then_moved);
  for (auto &em: elif_moved_sets)
    for (auto &v: em) moved.insert(v);
  for (auto &v: else_moved) moved.insert(v);

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

void FlowChecker::collect_variable_names(const ExprNode &expr, std::vector<std::string> &names) {
  std::visit(
      overloaded{
          [&](const Variable &v) { names.push_back(v.name); },
          [&](const BinaryExpr &b) {
            collect_variable_names(*b.lhs, names);
            collect_variable_names(*b.rhs, names);
          },
          [&](const UnaryExpr &u) { collect_variable_names(*u.expr, names); },
          [&](const AssignExpr &a) {
            collect_variable_names(*a.target, names);
            collect_variable_names(*a.value, names);
          },
          [&](const CallExpr &c) {
            collect_variable_names(*c.callee, names);
            for (auto &arg: c.args)
              collect_variable_names(*arg, names);
          },
          [&](const MemberExpr &m) { collect_variable_names(*m.object, names); },
          [&](const IndexExpr &i) {
            collect_variable_names(*i.object, names);
            collect_variable_names(*i.index, names);
          },
          [&](const auto &) {},
      },
      expr.expr);
}

void FlowChecker::check_expr_for_moved(const ExprNode &expr,
                                       const std::set<std::string> &moved,
                                       error::DiagCtxt &diag) {
  if (moved.empty()) return;
  std::vector<std::string> names;
  collect_variable_names(expr, names);
  for (auto &name: names) {
    if (moved.count(name)) {
      diag.error(expr.span, "Variable '" + name + "' was moved and cannot be used here")
          .emit_to(diag);
    }
  }
}

static bool param_type_is_own(const ast::Type &ty) {
  return std::get_if<ast::Type::Own>(&ty.data) != nullptr;
}

void FlowChecker::mark_own_args(const ExprNode &expr,
                                std::set<std::string> &moved,
                                error::DiagCtxt &diag) {
  if (!sema_ctx) return;

  std::visit(
      overloaded{
          [&](const CallExpr &call) {
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

            if (fn_name.empty()) return;

            auto *sym = sema_ctx->get_symbol_table().resolve(fn_name);
            if (!sym) return;

            auto *fn_data = std::get_if<FunctionData>(&sym->get_kind());
            if (!fn_data) return;

            for (size_t i = 0; i < fn_data->params.size() && i < call.args.size(); ++i) {
              if (!param_type_is_own(fn_data->params[i])) continue;
              if (auto *var = std::get_if<Variable>(&call.args[i]->expr)) {
                moved.insert(var->name);
              }
            }

            for (auto &arg: call.args)
              mark_own_args(*arg, moved, diag);
          },
          [&](const BinaryExpr &b) {
            mark_own_args(*b.lhs, moved, diag);
            mark_own_args(*b.rhs, moved, diag);
          },
          [&](const UnaryExpr &u) { mark_own_args(*u.expr, moved, diag); },
          [&](const AssignExpr &a) {
            mark_own_args(*a.target, moved, diag);
            mark_own_args(*a.value, moved, diag);
          },
          [&](const MemberExpr &m) { mark_own_args(*m.object, moved, diag); },
          [&](const IndexExpr &i) {
            mark_own_args(*i.object, moved, diag);
            mark_own_args(*i.index, moved, diag);
          },
          [&](const auto &) {},
      },
      expr.expr);
}

} // namespace cat
