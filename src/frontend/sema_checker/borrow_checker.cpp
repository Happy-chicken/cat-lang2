#include "borrow_checker.h"
#include "diag.h"
#include "sema_ctx.h"
#include "stmt.h"
#include "symbol.h"
#include "symbol_table.h"
#include <variant>

namespace cat {

bool BorrowChecker::run(Program &program, semantics::SemaCtxt &ctx,
                        error::DiagCtxt &diag) {
  sym_table = &ctx.get_symbol_table();
  builtins = &ctx.get_builtins();
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
  borrow_map.clear();
  scope_vars.clear();

  for (const auto &p : func.function_header.params) {
    bool is_struct = p.ty.is_struct_type() && !p.ty.is_move_type();
    bool is_cref = std::get_if<ast::Type::CRef>(&p.ty.data) != nullptr;
    string struct_name;
    auto try_set_struct = [&](const uptr<ast::Type> &inner) {
      if (inner)
        if (auto *strukt = std::get_if<ast::Type::Struct>(&inner->data))
          struct_name = strukt->name;
    };
    std::visit(
        overloaded{
            [&](const ast::Type::Struct &strukt) { struct_name = strukt.name; },
            [&](const ast::Type::Ref &ref) { try_set_struct(ref.inner); },
            [&](const ast::Type::CRef &cref) { try_set_struct(cref.inner); },
            [&](const ast::Type::Own &own) { try_set_struct(own.inner); },
            [](auto &&) {},
        },
        p.ty.data);
    states[p.name] = {VarState::Free, struct_name, is_struct, is_cref};
  }

  analyze_block(func.body, diag);
}

void BorrowChecker::check_impl_methods(const Impl &imp, error::DiagCtxt &diag) {
  for (const auto &method : imp.methods)
    check_function(method, diag);
}

// ── cache helpers ──

bool BorrowChecker::is_struct_var(const string &name) const {
  auto it = states.find(name);
  return it != states.end() && it->second.is_struct;
}

bool BorrowChecker::is_cref_var(const string &name) const {
  auto it = states.find(name);
  return it != states.end() && it->second.is_cref;
}

string BorrowChecker::get_struct_name(const string &name) const {
  auto it = states.find(name);
  if (it == states.end())
    return "";
  return it->second.struct_name;
}

// ── scope helpers ──

void BorrowChecker::release_borrow(const string &borrower) {
  auto it = borrow_map.find(borrower);
  if (it == borrow_map.end())
    return;

  const string &source = it->second;
  auto st = lookup_state(source);

  if (st == VarState::MutBorrowed) {
    mark_free(source);
  } else if (st == VarState::ImmutBorrowed) {
    auto &cnt = states[source].immut_count;
    if (cnt > 0) {
      cnt--;
      if (cnt == 0)
        mark_free(source);
    }
  }
  borrow_map.erase(it);
}

// ── type classification helpers ──

BorrowChecker::ParamKind
BorrowChecker::classify_param(const ast::Type &ty) const {
  return std::visit(overloaded{[](const ast::Type::Ref &) -> ParamKind {
                                 return ParamKind::BorrowMut;
                               },
                               [](const ast::Type::CRef &) -> ParamKind {
                                 return ParamKind::BorrowImmut;
                               },
                               [](const ast::Type::Own &) -> ParamKind {
                                 return ParamKind::Move;
                               },
                               [&](const auto &) -> ParamKind {
                                 if (ty.is_struct_type())
                                   return ParamKind::Move;
                                 return ParamKind::Copy;
                               }},
                    ty.data);
}

// ── state helpers ──

void BorrowChecker::mark_moved(const string &name) {
  states[name].state = VarState::Moved;
  states[name].immut_count = 0;
}

void BorrowChecker::mark_mut_borrowed(const string &name,
                                      const string &borrower) {
  states[name].state = VarState::MutBorrowed;
  borrow_map[borrower] = name;
  states[name].immut_count = 0;
}

void BorrowChecker::mark_immut_borrowed(const string &name,
                                        const string &borrower) {
  states[name].state = VarState::ImmutBorrowed;
  states[name].immut_count++;
  borrow_map[borrower] = name;
}

void BorrowChecker::mark_free(const string &name) {
  states[name].state = VarState::Free;
  states[name].immut_count = 0;
}

BorrowChecker::VarState BorrowChecker::lookup_state(const string &name) const {
  auto it = states.find(name);
  if (it != states.end())
    return it->second.state;
  return VarState::Free;
}

// ── statement analysis ──

void BorrowChecker::analyze_block(const Block &block, error::DiagCtxt &diag) {
  sym_table->enter_scope(ScopeKind::Block);
  size_t depth = sym_table->depth();
  if (scope_vars.size() <= depth)
    scope_vars.resize(depth + 1);
  for (const auto &stmt : block.stmts)
    analyze_stmt(stmt, diag);
  for (const auto &var : scope_vars[depth])
    release_borrow(var);
  scope_vars[depth].clear();
  sym_table->exit_scope();
}

void BorrowChecker::analyze_stmt(const StmtNode &stmt, error::DiagCtxt &diag) {
  std::visit(
      overloaded{
          [&](const VarDefStmt &vds) {
            if (vds.init.has_value())
              check_expr(*vds.init, diag);

            scope_vars[sym_table->depth()].push_back(vds.name);

            if (!vds.init.has_value())
              return;

            auto *src_var = std::get_if<Variable>(&vds.init->expr);

            if (!vds.ty.has_value()) {
              bool is_struct = false;
              string struct_name;
              bool is_cref = false;
              if (src_var) {
                is_struct = is_struct_var(src_var->name);
                is_cref = is_cref_var(src_var->name);
                struct_name = get_struct_name(src_var->name);
              } else {
                is_struct = std::holds_alternative<ListExpr>(vds.init->expr) ||
                            std::holds_alternative<CallExpr>(vds.init->expr);
                if (is_struct &&
                    std::holds_alternative<CallExpr>(vds.init->expr)) {
                  auto &call_expr = std::get<CallExpr>(vds.init->expr);
                  if (auto *cv = std::get_if<Variable>(&call_expr.callee->expr))
                    struct_name = cv->name;
                }
              }
              states[vds.name] = {VarState::Free, struct_name, is_struct,
                                  is_cref};
              if (src_var && is_struct)
                mark_moved(src_var->name);
              return;
            }

            bool is_struct =
                vds.ty->is_struct_type() && !vds.ty->is_move_type();
            bool is_cref =
                std::get_if<ast::Type::CRef>(&vds.ty->data) != nullptr;
            string struct_name;
            auto try_set_struct = [&](const uptr<ast::Type> &inner) {
              if (inner)
                if (auto *strukt = std::get_if<ast::Type::Struct>(&inner->data))
                  struct_name = strukt->name;
            };
            if (auto *strukt = std::get_if<ast::Type::Struct>(&vds.ty->data))
              struct_name = strukt->name;
            if (auto *ref = std::get_if<ast::Type::Ref>(&vds.ty->data))
              try_set_struct(ref->inner);
            if (auto *cref = std::get_if<ast::Type::CRef>(&vds.ty->data))
              try_set_struct(cref->inner);
            if (auto *own = std::get_if<ast::Type::Own>(&vds.ty->data))
              try_set_struct(own->inner);

            states[vds.name] = {VarState::Free, struct_name, is_struct, is_cref};

            if (std::get_if<ast::Type::Ref>(&vds.ty->data)) {
              if (!src_var)
                return;
              auto st = lookup_state(src_var->name);
              if (st != VarState::Free) {
                diag.error(stmt.span, "Cannot mutably borrow '" +
                                          src_var->name +
                                          "' because it is not free")
                    .emit_to(diag);
                return;
              }
              mark_mut_borrowed(src_var->name, vds.name);
            } else if (std::get_if<ast::Type::CRef>(&vds.ty->data)) {
              if (!src_var)
                return;
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
              if (!src_var)
                return;
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
            auto saved_borrow = borrow_map;

            analyze_block(*ls.body, diag);

            for (const auto &[name, info] : states) {
              if (info.state == VarState::Moved)
                saved_states[name].state = VarState::Moved;
            }
            states = std::move(saved_states);
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
  auto saved_borrow = borrow_map;

  states = saved_states;
  borrow_map = saved_borrow;
  analyze_block(*if_stmt.then_branch, diag);
  auto then_states = std::move(states);

  vector<decltype(states)> elif_states_list;
  for (const auto &[cond, block] : if_stmt.elif_branch) {
    states = saved_states;
    borrow_map = saved_borrow;
    check_expr(cond, diag);
    analyze_block(*block, diag);
    elif_states_list.push_back(std::move(states));
  }

  states = saved_states;
  borrow_map = saved_borrow;
  if (if_stmt.else_branch)
    analyze_block(*if_stmt.else_branch, diag);
  auto else_states = std::move(states);

  states = saved_states;
  borrow_map = saved_borrow;

  auto collect_moved = [&](const unordered_map<string, VarInfo> &branch) {
    unordered_set<string> m;
    for (const auto &[name, info] : branch)
      if (info.state == VarState::Moved)
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
    states[name].state = VarState::Moved;
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
              if (is_cref_var(v.name)) {
                diag.error(expr.span,
                           "Cannot write to cref reference '" + v.name + "'")
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
                if (is_struct_var(rhs_var->name))
                  mark_moved(rhs_var->name);
              }
            }
          },
          [&](const BinaryExpr &b) {
            check_expr(*b.lhs, diag);
            check_expr(*b.rhs, diag);
          },
          [&](const UnaryExpr &u) { check_expr(*u.expr, diag); },
          [&](const MemberExpr &m) {
            check_expr(*m.object, diag);
            if (is_write_target)
              if (auto *v = std::get_if<Variable>(&m.object->expr))
                if (is_cref_var(v->name))
                  diag.error(expr.span,
                             "Cannot write through cref reference '" + v->name +
                                 "'")
                      .emit_to(diag);
          },
          [&](const IndexExpr &i) {
            check_expr(*i.object, diag);
            check_expr(*i.index, diag);
            if (is_write_target)
              if (auto *v = std::get_if<Variable>(&i.object->expr))
                if (is_cref_var(v->name))
                  diag.error(expr.span,
                             "Cannot write through cref reference '" + v->name +
                                 "'")
                      .emit_to(diag);
          },
          [&](const ListExpr &l) {
            for (const auto &elem : l.elements)
              check_expr(*elem, diag);
          },
          [&](const LambdaExpr &f) {
            if (f.body) {
              auto saved_states = states;
              auto saved_borrow = borrow_map;
              auto saved_scope_vars = scope_vars;

              states.clear();
              borrow_map.clear();
              scope_vars.clear();

              analyze_block(*f.body, diag);

              states = std::move(saved_states);
              borrow_map = std::move(saved_borrow);
              scope_vars = std::move(saved_scope_vars);
            }
          },
          [&](const auto &) {},
      },
      expr.expr);
}

void BorrowChecker::resolve_call_args(const CallExpr &call,
                                      error::DiagCtxt &diag) {
  auto *member_expr = std::get_if<MemberExpr>(&call.callee->expr);
  auto *obj_var = member_expr
                      ? std::get_if<Variable>(&member_expr->object->expr)
                      : nullptr;

  // skip pure-read universal builtins — no borrow effects
  if (member_expr) {
    auto univ = builtins->lookup_universal(member_expr->field);
    if (univ && univ->get().meta.effect == runtime::MethodEffect::PureRead)
      return;
  }

  auto fn_name =
      std::visit(overloaded{
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

  static size_t call_id = 0;
  vector<string> call_borrowers;

  auto mark_borrow_mut = [&](const string &var_name, const string &arg_label) {
    string borrower =
        var_name + "_$call" + std::to_string(call_id) + "_" + arg_label;
    call_borrowers.push_back(borrower);
    mark_mut_borrowed(var_name, borrower);
  };

  auto mark_borrow_immut = [&](const string &var_name,
                               const string &arg_label) {
    string borrower =
        var_name + "_$call" + std::to_string(call_id) + "_" + arg_label;
    call_borrowers.push_back(borrower);
    mark_immut_borrowed(var_name, borrower);
  };

  auto *fn_sym = sym_table->resolve(fn_name);

  // fallback: resolve obj.method via struct-mangled name (StructName_method)
  if (!fn_sym && obj_var) {
    string cls = get_struct_name(obj_var->name);
    if (!cls.empty())
      fn_sym = sym_table->resolve(cls + "_" + member_expr->field);
  }

  // fallback: resolve obj.method as a list/str builtin with a mutating borrow
  if (!fn_sym && obj_var && is_struct_var(obj_var->name)) {
    for (auto tag : {runtime::LIST_TAG, runtime::STR_TAG}) {
      if (builtins->is_method_declared(tag, member_expr->field)) {
        auto desc = builtins->lookup(tag, member_expr->field);
        if (desc &&
            desc->get().meta.effect == runtime::MethodEffect::Mutating) {
          if (lookup_state(obj_var->name) != VarState::Free) {
            diag.error(call.callee->span,
                       "Cannot call mutating method: '" + obj_var->name +
                           "' is not free")
                .emit_to(diag);
          } else {
            string borrower = obj_var->name + "_$call" +
                              std::to_string(call_id) + "_builtin";
            call_borrowers.push_back(borrower);
            mark_mut_borrowed(obj_var->name, borrower);
          }
        }
        break;
      }
    }
  }

  // process user-defined function / function-typed variable parameters
  if (fn_sym) {
    ++call_id;

    if (auto *fn_data = std::get_if<FunctionData>(&fn_sym->get_kind())) {
      // classify the self parameter (methods only)
      if (member_expr && obj_var && !fn_data->params.empty()) {
        auto pc = classify_param(fn_data->params[0]);
        switch (pc) {
        case ParamKind::Move:
          if (lookup_state(obj_var->name) == VarState::Free)
            mark_moved(obj_var->name);
          else
            diag.error(call.callee->span, "Self '" + obj_var->name +
                                              "' has been moved or borrowed "
                                              "and cannot be moved here")
                .emit_to(diag);
          break;
        case ParamKind::BorrowMut:
          if (lookup_state(obj_var->name) != VarState::Free) {
            diag.error(call.callee->span,
                       "Self '" + obj_var->name +
                           "' is not free to be mutably borrowed")
                .emit_to(diag);
          } else {
            mark_borrow_mut(obj_var->name, "self");
          }
          break;
        case ParamKind::BorrowImmut: {
          auto st = lookup_state(obj_var->name);
          if (st == VarState::Moved || st == VarState::MutBorrowed) {
            diag.error(call.callee->span,
                       "Self '" + obj_var->name +
                           "' is not free to be immutably borrowed")
                .emit_to(diag);
          } else {
            mark_borrow_immut(obj_var->name, "self");
          }
          break;
        }
        case ParamKind::Copy:
          break;
        }
      }

      // classify each argument against the corresponding parameter
      size_t poff = member_expr ? 1 : 0;
      for (size_t i = 0;
           i + poff < fn_data->params.size() && i < call.args.size(); ++i) {
        auto *var = std::get_if<Variable>(&call.args[i]->expr);
        if (!var)
          continue;

        auto pc = classify_param(fn_data->params[i + poff]);
        switch (pc) {
        case ParamKind::Move:
          if (lookup_state(var->name) == VarState::Free)
            mark_moved(var->name);
          else
            diag.error(
                    call.args[i]->span,
                    "Argument '" + var->name +
                        "' has been moved or borrowed and cannot be moved here")
                .emit_to(diag);
          break;
        case ParamKind::BorrowMut:
          if (lookup_state(var->name) != VarState::Free) {
            diag.error(call.args[i]->span,
                       "Argument '" + var->name +
                           "' is not free to be mutably borrowed")
                .emit_to(diag);
          } else {
            mark_borrow_mut(var->name, std::to_string(i));
          }
          break;
        case ParamKind::BorrowImmut: {
          auto st = lookup_state(var->name);
          if (st == VarState::Moved || st == VarState::MutBorrowed) {
            diag.error(call.args[i]->span,
                       "Argument '" + var->name +
                           "' is not free to be immutably borrowed")
                .emit_to(diag);
          } else {
            mark_borrow_immut(var->name, std::to_string(i));
          }
          break;
        }
        case ParamKind::Copy:
          break;
        }
      }
    } else {
      // fn_sym is a function-typed variable — classify via its stored Func type
      const auto &sym_ty = fn_sym->get_type();
      if (sym_ty.has_value()) {
        if (auto *func_ty = std::get_if<ast::Type::Func>(&sym_ty->data)) {
          for (size_t i = 0; i < func_ty->params.size() && i < call.args.size();
               ++i) {
            if (!func_ty->params[i])
              continue;
            auto *var = std::get_if<Variable>(&call.args[i]->expr);
            if (!var)
              continue;

            auto pc = classify_param(*func_ty->params[i]);
            switch (pc) {
            case ParamKind::Move:
              if (lookup_state(var->name) == VarState::Free)
                mark_moved(var->name);
              else
                diag.error(call.args[i]->span,
                           "Argument '" + var->name +
                               "' has been moved or borrowed and cannot be "
                               "moved here")
                    .emit_to(diag);
              break;
            case ParamKind::BorrowMut:
              if (lookup_state(var->name) != VarState::Free) {
                diag.error(call.args[i]->span,
                           "Argument '" + var->name +
                               "' is not free to be mutably borrowed")
                    .emit_to(diag);
              } else {
                mark_borrow_mut(var->name, std::to_string(i));
              }
              break;
            case ParamKind::BorrowImmut: {
              auto st = lookup_state(var->name);
              if (st == VarState::Moved || st == VarState::MutBorrowed) {
                diag.error(call.args[i]->span,
                           "Argument '" + var->name +
                               "' is not free to be immutably borrowed")
                    .emit_to(diag);
              } else {
                mark_borrow_immut(var->name, std::to_string(i));
              }
              break;
            }
            case ParamKind::Copy:
              break;
            }
          }
        }
      }
    }

  }

  // release temporary call-site borrows immediately after the call
  for (const auto &b : call_borrowers)
    release_borrow(b);
}

} // namespace cat
