#include "borrow_checker.h"
#include "diag.h"
#include "sema_ctx.h"
#include "stmt.h"
#include "symbol.h"
#include "symbol_table.h"

namespace cat {

bool BorrowChecker::run(Program &program, semantics::SemaCtxt &ctx,
                        error::DiagCtxt &diag) {
  sym_table = &ctx.get_symbol_table();
  for (const auto &item_node : program.items) {
    std::visit(
        overloaded{[&](const FunctionDef &func) { check_function(func, diag); },
                   [&](const Impl &imp) { check_impl_methods(imp, diag); },
                   [&](const auto &) {}},
        item_node.item);
  }
  return !diag.has_errors();
}

void BorrowChecker::check_function(const FunctionDef &func,
                                   error::DiagCtxt &diag) {
  states.clear();
  immut_count.clear();
  borrow_map.clear();
  is_struct_map.clear();
  scopes.clear();

  for (const auto &p : func.function_header.params) {
    bool is_struct =
        is_struct_ty(p.ty) &&
        !std::get_if<ast::Type::Ref>(&p.ty.data) &&
        !std::get_if<ast::Type::CRef>(&p.ty.data) &&
        !std::get_if<ast::Type::Own>(&p.ty.data);
    is_struct_map[p.name] = is_struct;
  }

  push_scope();
  analyze_block(func.body, diag);
  pop_scope();
}

void BorrowChecker::check_impl_methods(const Impl &imp,
                                       error::DiagCtxt &diag) {
  for (const auto &method : imp.methods)
    check_function(method, diag);
}

// ── scope helpers ──

void BorrowChecker::push_scope() { scopes.push_back({}); }

void BorrowChecker::pop_scope() {
  if (scopes.empty())
    return;
  auto &scope_vars = scopes.back();
  for (const auto &var : scope_vars)
    release_borrow(var);
  scopes.pop_back();
}

void BorrowChecker::release_borrow(const string &borrower) {
  auto it = borrow_map.find(borrower);
  if (it == borrow_map.end())
    return;

  const string &source = it->second;
  auto st = lookup_state(source);

  if (st == VarState::MutBorrowed) {
    mark_free(source);
  } else if (st == VarState::ImmutBorrowed) {
    auto ci = immut_count.find(source);
    if (ci != immut_count.end() && ci->second > 0) {
      ci->second--;
      if (ci->second == 0) {
        immut_count.erase(ci);
        mark_free(source);
      }
    }
  }
  borrow_map.erase(it);
}

// ── type classification helpers ──

bool BorrowChecker::is_struct_ty(const ast::Type &ty) const {
  return std::get_if<ast::Type::Class>(&ty.data) ||
         std::get_if<ast::Type::List>(&ty.data) ||
         std::get_if<ast::Type::Str>(&ty.data);
}

BorrowChecker::ParamClass
BorrowChecker::classify_param(const ast::Type &ty) const {
  if (std::get_if<ast::Type::Ref>(&ty.data))
    return ParamClass::BorrowMut;
  if (std::get_if<ast::Type::CRef>(&ty.data))
    return ParamClass::BorrowImmut;
  if (std::get_if<ast::Type::Own>(&ty.data))
    return ParamClass::Move;
  // Bare type: params deep-copy by default, so no move.
  // Later, if no Clone deriving, fall back to Move with a warning.
  return ParamClass::Copy;
}

// ── state helpers ──

void BorrowChecker::mark_moved(const string &name) {
  states[name] = VarState::Moved;
  immut_count.erase(name);
}

void BorrowChecker::mark_mut_borrowed(const string &name,
                                      const string &borrower) {
  states[name] = VarState::MutBorrowed;
  borrow_map[borrower] = name;
  immut_count.erase(name);
}

void BorrowChecker::mark_immut_borrowed(const string &name,
                                        const string &borrower) {
  states[name] = VarState::ImmutBorrowed;
  immut_count[name]++;
  borrow_map[borrower] = name;
}

void BorrowChecker::mark_free(const string &name) {
  states[name] = VarState::Free;
  immut_count.erase(name);
}

BorrowChecker::VarState BorrowChecker::lookup_state(const string &name) const {
  auto it = states.find(name);
  if (it != states.end())
    return it->second;
  return VarState::Free;
}

// ── statement analysis ──

void BorrowChecker::analyze_block(const Block &block, error::DiagCtxt &diag) {
  push_scope();
  for (const auto &stmt : block.stmts)
    analyze_stmt(stmt, diag);
  pop_scope();
}

void BorrowChecker::analyze_stmt(const StmtNode &stmt,
                                 error::DiagCtxt &diag) {
  std::visit(
      overloaded{
          [&](const VarDefStmt &vds) {
            if (vds.init.has_value())
              check_expr(*vds.init, diag);

            if (!scopes.empty())
              scopes.back().push_back(vds.name);

            if (!vds.init.has_value())
              return;

            auto *src_var = std::get_if<Variable>(&vds.init->expr);

            if (!vds.ty.has_value()) {
              bool is_struct = false;
              if (src_var) {
                auto it = is_struct_map.find(src_var->name);
                is_struct = (it != is_struct_map.end() && it->second);
              } else {
                is_struct = std::holds_alternative<ListExpr>(vds.init->expr) ||
                            std::holds_alternative<CallExpr>(vds.init->expr);
              }
              is_struct_map[vds.name] = is_struct;
              if (src_var && is_struct)
                mark_moved(src_var->name);
              return;
            }

            bool is_struct =
                is_struct_ty(*vds.ty) &&
                !std::get_if<ast::Type::Ref>(&vds.ty->data) &&
                !std::get_if<ast::Type::CRef>(&vds.ty->data) &&
                !std::get_if<ast::Type::Own>(&vds.ty->data);
            is_struct_map[vds.name] = is_struct;

            if (std::get_if<ast::Type::Ref>(&vds.ty->data)) {
              if (!src_var) return;
              auto st = lookup_state(src_var->name);
              if (st != VarState::Free) {
                diag.error(stmt.span,
                           "Cannot mutably borrow '" + src_var->name +
                               "' because it is not free")
                    .emit_to(diag);
                return;
              }
              mark_mut_borrowed(src_var->name, vds.name);
            } else if (std::get_if<ast::Type::CRef>(&vds.ty->data)) {
              if (!src_var) return;
              auto st = lookup_state(src_var->name);
              if (st == VarState::Moved || st == VarState::MutBorrowed) {
                diag.error(stmt.span,
                           "Cannot immutably borrow '" + src_var->name +
                               "' because it is moved or mutably borrowed")
                    .emit_to(diag);
                return;
              }
              mark_immut_borrowed(src_var->name, vds.name);
            } else if (std::get_if<ast::Type::Own>(&vds.ty->data)) {
              if (!src_var) return;
              auto st = lookup_state(src_var->name);
              if (st != VarState::Free) {
                diag.error(stmt.span, "Cannot take ownership of '" +
                                          src_var->name +
                                          "' because it is not free")
                    .emit_to(diag);
                return;
              }
              mark_moved(src_var->name);
            } else if (is_struct) {
              if (src_var)
                mark_moved(src_var->name);
            }
          },
          [&](const ExprStmt &es) { check_expr(es.expr, diag); },
          [&](const ReturnStmt &rs) {
            if (rs.expr.has_value())
              check_expr(*rs.expr, diag);
          },
          [&](const IfStmt &is) { analyze_if_stmt(is, diag); },
          [&](const LoopStmt &ls) {
            check_expr(ls.condition, diag);

            auto saved_states = states;
            auto saved_immut = immut_count;
            auto saved_borrow = borrow_map;

            analyze_block(*ls.body, diag);

            // After loop body: for each variable that was Moved inside the
            // loop body, treat as Moved outside (conservative).
            for (const auto &[name, state] : states) {
              if (state == VarState::Moved)
                saved_states[name] = VarState::Moved;
            }
            states = std::move(saved_states);
            immut_count = std::move(saved_immut);
            borrow_map = std::move(saved_borrow);
          },
          [&](const BlockStmt &bs) { analyze_block(*bs.block, diag); },
          [&](const auto &) {},
      },
      stmt.stmt);
}

void BorrowChecker::analyze_if_stmt(const IfStmt &if_stmt,
                                    error::DiagCtxt &diag) {
  check_expr(if_stmt.condition, diag);

  auto saved_states = states;
  auto saved_immut = immut_count;
  auto saved_borrow = borrow_map;

  states = saved_states;
  immut_count = saved_immut;
  borrow_map = saved_borrow;
  analyze_block(*if_stmt.then_branch, diag);
  auto then_states = std::move(states);
  auto then_immut = std::move(immut_count);

  vector<decltype(states)> elif_states_list;
  for (const auto &[cond, block] : if_stmt.elif_branch) {
    states = saved_states;
    immut_count = saved_immut;
    borrow_map = saved_borrow;
    check_expr(cond, diag);
    analyze_block(*block, diag);
    elif_states_list.push_back(std::move(states));
  }

  states = saved_states;
  immut_count = saved_immut;
  borrow_map = saved_borrow;
  if (if_stmt.else_branch)
    analyze_block(*if_stmt.else_branch, diag);
  auto else_states = std::move(states);

  // Merge: borrow state returns to pre-branch (all refs/crefs in branches go
  // out of scope at branch exit). Moved: union if moved in all branches.
  states = saved_states;
  immut_count = saved_immut;
  borrow_map = saved_borrow;

  unordered_set<string> all_branches;
  auto collect_moved = [&](const unordered_map<string, VarState> &branch) {
    unordered_set<string> m;
    for (const auto &[name, st] : branch)
      if (st == VarState::Moved)
        m.insert(name);
    return m;
  };

  auto then_moved = collect_moved(then_states);
  unordered_set<string> merged(then_moved);

  for (const auto &es : elif_states_list) {
    auto em = collect_moved(es);
    vector<string> to_erase;
    for (const auto &name : merged)
      if (!em.count(name))
        to_erase.push_back(name);
    for (const auto &n : to_erase)
      merged.erase(n);
  }

  if (if_stmt.else_branch || !if_stmt.elif_branch.empty()) {
    auto em = collect_moved(else_states);
    vector<string> to_erase;
    for (const auto &name : merged)
      if (!em.count(name))
        to_erase.push_back(name);
    for (const auto &n : to_erase)
      merged.erase(n);
  }

  for (const auto &name : merged)
    states[name] = VarState::Moved;
}

// ── expression checking ──

void BorrowChecker::check_expr(const ExprNode &expr, error::DiagCtxt &diag,
                               bool is_write_target) {
  std::visit(
      overloaded{
          [&](const Variable &v) {
            auto st = lookup_state(v.name);
            if (st == VarState::Moved) {
              diag.error(expr.span, "Variable '" + v.name +
                                        "' was moved and cannot be used here")
                  .emit_to(diag);
              return;
            }
            if (is_write_target) {
              if (st == VarState::MutBorrowed ||
                  st == VarState::ImmutBorrowed) {
                diag.error(expr.span, "Cannot write to '" + v.name +
                                          "' because it is borrowed")
                    .emit_to(diag);
                return;
              }
            } else {
              if (st == VarState::MutBorrowed) {
                diag.error(expr.span, "Cannot read '" + v.name +
                                          "' because it is mutably borrowed")
                    .emit_to(diag);
              }
            }
          },
          [&](const CallExpr &call) {
            check_expr(*call.callee, diag);
            for (auto &arg : call.args)
              check_expr(*arg, diag);
            resolve_call_args(call, diag);
          },
          [&](const AssignExpr &a) {
            check_expr(*a.value, diag);
            check_expr(*a.target, diag, /*is_write_target=*/true);

            if (auto *rhs_var = std::get_if<Variable>(&a.value->expr)) {
              if (lookup_state(rhs_var->name) == VarState::Free) {
                auto it = is_struct_map.find(rhs_var->name);
                if (it != is_struct_map.end() && it->second)
                  mark_moved(rhs_var->name);
              }
            }
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
          [&](const ListExpr &l) {
            for (const auto &elem : l.elements)
              check_expr(*elem, diag);
          },
          [&](const LambdaExpr &f) {
            if (f.body) {
              auto saved_states = states;
              auto saved_immut = immut_count;
              auto saved_borrow = borrow_map;
              auto saved_scopes = scopes;

              states.clear();
              immut_count.clear();
              borrow_map.clear();
              scopes.clear();
              push_scope();

              analyze_block(*f.body, diag);

              pop_scope();
              states = std::move(saved_states);
              immut_count = std::move(saved_immut);
              borrow_map = std::move(saved_borrow);
              scopes = std::move(saved_scopes);
            }
          },
          [&](const auto &) {},
      },
      expr.expr);
}

void BorrowChecker::resolve_call_args(const CallExpr &call,
                                      error::DiagCtxt &diag) {
  // .clone() → source is read, not moved. Skip ownership tracking.
  if (std::holds_alternative<MemberExpr>(call.callee->expr)) {
    auto &member = std::get<MemberExpr>(call.callee->expr);
    if (member.field == "clone")
      return;
  }

  auto fn_name = std::visit(
      overloaded{
          [](const Variable &v) -> string { return v.name; },
          [](const MemberExpr &m) -> string {
            if (auto *obj = std::get_if<Variable>(&m.object->expr))
              return obj->name + "_" + m.field;
            return "";
          },
          [](const auto &) -> string { return ""; },
      },
      call.callee->expr);

  if (fn_name.empty())
    return;

  auto *fn_sym = sym_table->resolve(fn_name);
  if (!fn_sym)
    return;

  if (auto *fn_data = std::get_if<FunctionData>(&fn_sym->get_kind())) {
    for (size_t i = 0;
         i < fn_data->params.size() && i < call.args.size(); ++i) {
      auto *var = std::get_if<Variable>(&call.args[i]->expr);
      if (!var)
        continue;

      auto pc = classify_param(fn_data->params[i]);
      switch (pc) {
      case ParamClass::Move: {
        if (lookup_state(var->name) == VarState::Free)
          mark_moved(var->name);
        break;
      }
      case ParamClass::BorrowMut: {
        if (lookup_state(var->name) != VarState::Free) {
          diag.error(call.args[i]->span,
                     "Argument '" + var->name +
                         "' is not free to be mutably borrowed")
              .emit_to(diag);
        }
        break;
      }
      case ParamClass::BorrowImmut: {
        auto st = lookup_state(var->name);
        if (st == VarState::Moved || st == VarState::MutBorrowed) {
          diag.error(call.args[i]->span,
                     "Argument '" + var->name +
                         "' is not free to be immutably borrowed")
              .emit_to(diag);
        }
        break;
      }
      case ParamClass::Copy:
        break;
      }
    }
    return;
  }

  // function-typed variable path
  const auto &sym_ty = fn_sym->get_type();
  if (sym_ty.has_value()) {
    if (auto *func_ty = std::get_if<ast::Type::Func>(&sym_ty->data)) {
      for (size_t i = 0;
           i < func_ty->params.size() && i < call.args.size(); ++i) {
        if (!func_ty->params[i])
          continue;
        auto *var = std::get_if<Variable>(&call.args[i]->expr);
        if (!var)
          continue;

        auto pc = classify_param(*func_ty->params[i]);
        switch (pc) {
        case ParamClass::Move: {
          if (lookup_state(var->name) == VarState::Free)
            mark_moved(var->name);
          break;
        }
        case ParamClass::BorrowMut: {
          if (lookup_state(var->name) != VarState::Free) {
            diag.error(call.args[i]->span,
                       "Argument '" + var->name +
                           "' is not free to be mutably borrowed")
                .emit_to(diag);
          }
          break;
        }
        case ParamClass::BorrowImmut: {
          auto st = lookup_state(var->name);
          if (st == VarState::Moved || st == VarState::MutBorrowed) {
            diag.error(call.args[i]->span,
                       "Argument '" + var->name +
                           "' is not free to be immutably borrowed")
                .emit_to(diag);
          }
          break;
        }
        case ParamClass::Copy:
          break;
        }
      }
    }
  }
}

} // namespace cat
