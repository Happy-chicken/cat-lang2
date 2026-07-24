#include "sema_checker.h"
#include "expr.h"
#include "scope.h"
#include "stmt.h"
#include "symbol.h"
#include <variant>

namespace cat {

static bool is_nested_illegal_kind(const ast::Type &ty) {
  return std::get_if<ast::Type::Ref>(&ty.data) ||
         std::get_if<ast::Type::Own>(&ty.data);
}

static bool validate_ref_own_nesting(const ast::Type &ty, Span span,
                                     error::DiagCtxt &diag) {
  return std::visit(
      [&](const auto &v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ast::Type::Ref> ||
                      std::is_same_v<T, ast::Type::Own>) {
          if (v.inner) {
            if (is_nested_illegal_kind(*v.inner)) {
              const char *kind =
                  std::is_same_v<T, ast::Type::Ref> ? "reference" : "ownership";
              diag.error(span,
                         std::string(kind) +
                             " cannot wrap reference or ownership type (" +
                             v.inner->to_string() + ")")
                  .emit_to(diag);
              return false;
            }
            return validate_ref_own_nesting(*v.inner, span, diag);
          }
        } else if constexpr (std::is_same_v<T, ast::Type::Ptr> ||
                             std::is_same_v<T, ast::Type::List>) {
          if (v.inner)
            return validate_ref_own_nesting(*v.inner, span, diag);
        }
        return true;
      },
      ty.data);
}

void SemaChecker::check_function(const FunctionDef &func, Span span,
                                 semantics::SemaCtxt &ctx,
                                 error::DiagCtxt &diag) {
  ctx.get_symbol_table().enter_scope(ScopeKind::Function);
  for (const auto &param : func.function_header.params) {
    validate_ref_own_nesting(param.ty, span, diag);
    bool is_ref = std::get_if<ast::Type::Ref>(&param.ty.data) != nullptr;
    bool is_own = std::get_if<ast::Type::Own>(&param.ty.data) != nullptr;
    Symbol param_sym = Symbol::new_parameter(param.name, param.ty.clone(),
                                             is_ref, is_own, span);
    auto existing = ctx.get_symbol_table().declare(std::move(param_sym));
    if (existing) {
      diag.error(span, "Parameter name '" + param.name +
                           "' is already declared in this scope")
          .note("Previous declaration was here: " +
                existing->get_span().to_string())
          .emit_to(diag);
    }
  }

  if (func.function_header.return_type.has_value())
    validate_ref_own_nesting(*func.function_header.return_type, span, diag);

  for (const auto &stmt : func.body.stmts) {
    check_stmt(stmt, stmt.span, ctx, diag);
  }
  ctx.get_symbol_table().exit_scope();
}

// Forward declaration used by check_impl
static void validate_trait_impl(const string &trait_name,
                                const string &class_name,
                                const vector<FunctionDef> &methods, Span span,
                                semantics::SemaCtxt &ctx,
                                error::DiagCtxt &diag);

void SemaChecker::check_impl(const Impl &imp, Span span,
                             semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
  if (ctx.get_symbol_table().resolve_global(imp.class_name) == nullptr) {
    diag.error(span, "Class '" + imp.class_name + "' is not declared")
        .emit_to(diag);
  }

  // Validate that all trait-required methods are implemented
  if (imp.trait_name.has_value())
    validate_trait_impl(*imp.trait_name, imp.class_name, imp.methods, span, ctx,
                        diag);

  for (const auto &method : imp.methods) {
    check_function(method, span, ctx, diag);
  }
}

void SemaChecker::check_block(const Block &block, Span span,
                              semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
  ctx.get_symbol_table().enter_scope(ScopeKind::Block);
  for (const auto &stmt : block.stmts) {
    check_stmt(stmt, stmt.span, ctx, diag);
  }
  ctx.get_symbol_table().exit_scope();
}

// Check that an impl block provides all methods required by its trait.
static void validate_trait_impl(const string &trait_name,
                                const string &class_name,
                                const vector<FunctionDef> &methods, Span span,
                                semantics::SemaCtxt &ctx,
                                error::DiagCtxt &diag) {
  auto trait_sym = ctx.get_symbol_table().resolve_global(trait_name);
  if (!trait_sym) {
    diag.error(span, "Trait '" + trait_name + "' is not declared")
        .emit_to(diag);
    return;
  }
  auto *trait = std::get_if<TraitData>(&trait_sym->get_kind());
  if (!trait)
    return;

  for (const auto &required : trait->methods) {
    auto found = std::find_if(methods.begin(), methods.end(),
                              [&](const FunctionDef &method) {
                                return method.function_header.name == required;
                              });
    if (found == methods.end()) {
      diag.error(span, "Method '" + required + "' is required by trait '" +
                           trait_name + "' but not implemented in class '" +
                           class_name + "'")
          .emit_to(diag);
    }
  }
}

struct InitTypeInfo {
  optional<ast::Type> var_ty;
  optional<size_t> list_len;
};

static InitTypeInfo infer_type_from_init(const ExprNode &init,
                                         semantics::SemaCtxt &ctx) {
  InitTypeInfo result;
  if (auto *list = std::get_if<ListExpr>(&init.expr)) {
    result.list_len = list->elements.size();
    result.var_ty = ast::type_list(ast::Type{});
  } else if (auto *call = std::get_if<CallExpr>(&init.expr)) {
    if (auto *cv = std::get_if<Variable>(&call->callee->expr)) {
      auto class_sym = ctx.get_symbol_table().resolve(cv->name);
      if (class_sym &&
          std::holds_alternative<ClassData>(class_sym->get_kind())) {
        result.var_ty = ast::Type(ast::Type::Class{cv->name});
      } else if (class_sym &&
                 std::holds_alternative<FunctionData>(class_sym->get_kind())) {
        auto &func_data = std::get<FunctionData>(class_sym->get_kind());
        result.var_ty = func_data.return_type.clone();
      }
    }
  } else if (auto *func = std::get_if<LambdaExpr>(&init.expr)) {
    vector<uptr<ast::Type>> param_types;
    for (auto &pt : func->params) {
      param_types.push_back(std::make_unique<ast::Type>(pt.ty.clone()));
    }
    auto ret = func->return_type
                   ? std::make_unique<ast::Type>(func->return_type->clone())
                   : std::make_unique<ast::Type>(ast::Type::Void{});
    result.var_ty =
        ast::Type(ast::Type::Func{std::move(param_types), std::move(ret)});
  }
  return result;
}

void SemaChecker::check_stmt(const StmtNode &stmt, Span span,
                             semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
  std::visit(
      overloaded{
          [&](const VarDefStmt &var_def) {
            bool var_is_ref = false;
            bool var_is_own = false;
            if (var_def.ty.has_value()) {
              var_is_ref =
                  std::get_if<ast::Type::Ref>(&var_def.ty->data) != nullptr;
              var_is_own =
                  std::get_if<ast::Type::Own>(&var_def.ty->data) != nullptr;
              if ((var_is_ref || var_is_own) && !var_def.init.has_value()) {
                diag.error(span, "Variable '" + var_def.name +
                                     "' of reference or ownership type must be "
                                     "initialized")
                    .emit_to(diag);
              }
            }
            if (var_def.init.has_value()) {
              check_expr(*var_def.init, var_def.init->span, ctx, diag);
            }
            optional<size_t> list_len;
            optional<ast::Type> var_ty;
            if (var_def.ty.has_value()) {
              validate_ref_own_nesting(*var_def.ty, span, diag);
              var_ty = var_def.ty->clone();
            }
            if (var_def.init.has_value() && !var_ty.has_value()) {
              auto info = infer_type_from_init(*var_def.init, ctx);
              list_len = info.list_len;
              if (info.var_ty.has_value())
                var_ty = std::move(info.var_ty);
            }
            auto sym = Symbol::new_variable(
                var_def.name, var_ty ? std::move(*var_ty) : ast::Type{}, false,
                span, var_is_ref, var_is_own, list_len);
            auto existing = ctx.get_symbol_table().declare(std::move(sym));
            if (existing) {
              diag.error(span, "Variable '" + var_def.name +
                                   "' is already declared in this scope")
                  .note("Previous declaration was here: " +
                        existing->get_span().to_string())
                  .emit_to(diag);
            }
          },
          [&](const IfStmt &if_stmt) {
            const auto &condition = if_stmt.condition;
            const auto &then_branch = if_stmt.then_branch;
            const auto &elif_branches = if_stmt.elif_branch;
            const auto &else_branch = if_stmt.else_branch;
            check_expr(condition, condition.span, ctx, diag);
            check_block(*then_branch.get(), condition.span, ctx, diag);
            for (const auto &[elif_cond, elif_block] : elif_branches) {
              check_expr(elif_cond, elif_cond.span, ctx, diag);
              check_block(*elif_block, elif_cond.span, ctx, diag);
            }
            if (else_branch && !else_branch->stmts.empty()) {
              check_block(*else_branch, else_branch->stmts[0].span, ctx, diag);
            }
          },
          [&](const LoopStmt &while_stmt) {
            ctx.get_symbol_table().enter_scope(ScopeKind::Loop);
            const auto &condition = while_stmt.condition;
            const auto &body = while_stmt.body;
            check_expr(condition, condition.span, ctx, diag);
            check_block(*body.get(), condition.span, ctx, diag);
            ctx.get_symbol_table().exit_scope();
          },
          [&](const ExprStmt &expr_stmt) {
            check_expr(expr_stmt.expr, expr_stmt.expr.span, ctx, diag);
          },
          [&](const ReturnStmt &return_stmt) {
            if (!ctx.get_symbol_table().nearest_of_kind(ScopeKind::Function)) {
              diag.error(span, "Return statement is not inside a function")
                  .emit_to(diag);
            }
            auto &expr = return_stmt.expr;
            if (expr.has_value()) {
              check_expr(*expr, expr->span, ctx, diag);
            }
          },
          [&](const BreakStmt &) {
            if (!ctx.get_symbol_table().nearest_of_kind(ScopeKind::Loop)) {
              diag.error(span, "Break statement is not inside a loop")
                  .emit_to(diag);
            }
          },
          [&](const ContinueStmt &) {
            if (!ctx.get_symbol_table().nearest_of_kind(ScopeKind::Loop)) {
              diag.error(span, "Continue statement is not inside a loop")
                  .emit_to(diag);
            }
          },
          [&](const BlockStmt &block_stmt) {
            if (!block_stmt.block->stmts.empty()) {
              check_block(*block_stmt.block.get(),
                          block_stmt.block->stmts[0].span, ctx, diag);
            }
          },
          [&](const auto &) {}},
      stmt.stmt);
}

// ── helpers for check_call_expr ──────────────────────────────────────

// Extract the user-visible function name from a callee expression.
// Returns "" for anonymous callables (e.g. lambda-typed variables).
static string extract_call_name(const ExprNode &callee) {
  return std::visit(
      overloaded{
          [](const Variable &var) -> string { return var.name; },
          [](const MemberExpr &member) -> string { return member.field; },
          [](const auto &) -> string { return ""; }},
      callee.expr);
}

// Describes the resolved target of a call expression.
struct CallTargetInfo {
  string resolved_name; // symbol-table name (mangled for class methods)
  string display_name;  // user-written name (for diagnostics)
  bool is_builtin;
};

// For obj.method(...), resolve whether the call goes to a list builtin
// or a class method.  Returns nullopt when obj is not a simple variable
// (the type checker will resolve it later).
static optional<CallTargetInfo> resolve_method_target(const string &func_name,
                                                      const CallExpr &call,
                                                      semantics::SemaCtxt &ctx,
                                                      error::DiagCtxt &diag,
                                                      Span span) {
  auto &member = std::get<MemberExpr>(call.callee->expr);
  auto *obj_var = std::get_if<Variable>(&member.object->expr);
  if (!obj_var)
    return std::nullopt; // receiver is an expression — defer to type checker

  auto obj_sym = ctx.get_symbol_table().resolve(obj_var->name);
  if (!obj_sym || !obj_sym->get_type().has_value())
    return std::nullopt;

  auto &ty = *obj_sym->get_type();

  // list builtin
  if (std::get_if<ast::Type::List>(&ty.data)) {
    if (ctx.get_builtins().is_method_declared("list", func_name))
      return CallTargetInfo{func_name, func_name, true};
    diag.error(span, "Unknown list method '" + func_name + "'").emit_to(diag);
    return std::nullopt;
  }

  // class method → mangle ClassName_methodName
  if (auto *cls = std::get_if<ast::Type::Class>(&ty.data))
    return CallTargetInfo{cls->name + "_" + func_name, func_name, false};

  return std::nullopt;
}

// Check arity of a builtin list method against the registry.
static void check_builtin_arity(const string &func_name, size_t arg_count,
                                Span span, semantics::SemaCtxt &ctx,
                                error::DiagCtxt &diag) {
  auto desc = ctx.get_builtins().lookup("list", func_name);
  if (desc && arg_count != desc->get().arity) {
    diag.error(span, "'" + func_name + "' expects " +
                         std::to_string(desc->get().arity) +
                         " arguments, got " + std::to_string(arg_count))
        .emit_to(diag);
  }
}

// list.push / list.pop invalidate compile-time list length tracking.
static void clear_list_len_for_mutating_builtin(const string &func_name,
                                                const CallExpr &call,
                                                semantics::SemaCtxt &ctx) {
  if (func_name != "push" && func_name != "pop")
    return;
  auto &member = std::get<MemberExpr>(call.callee->expr);
  auto *var = std::get_if<Variable>(&member.object->expr);
  if (!var)
    return;
  auto *sym = ctx.get_symbol_table().resolve(var->name);
  if (sym)
    sym->clear_known_list_len();
}

// Check arity for a user-defined callable (function, class constructor,
// or variable holding a function value).
static void check_symbol_arity(const Symbol &sym, const string &name,
                               bool is_method, size_t arg_count, Span span,
                               error::DiagCtxt &diag) {
  std::visit(
      overloaded{
          [&](const FunctionData &f) {
            int expected = static_cast<int>(f.params.size());
            if (is_method)
              expected = std::max(expected - 1, 0);
            if (expected != static_cast<int>(arg_count)) {
              diag.error(span, "Function '" + name + "' expects " +
                                   std::to_string(expected) +
                                   " arguments, but " +
                                   std::to_string(arg_count) + " were provided")
                  .emit_to(diag);
            }
          },
          [&](const ClassData &c) {
            size_t required = 0;
            for (bool has_def : c.has_default)
              if (!has_def)
                ++required;
            if (arg_count < required || arg_count > c.fields.size()) {
              diag.error(span, "Class '" + name + "' expects " +
                                   std::to_string(required) + " to " +
                                   std::to_string(c.fields.size()) +
                                   " arguments, but " +
                                   std::to_string(arg_count) + " were provided")
                  .emit_to(diag);
            }
          },
          [&](const auto &) {
            // function-typed variable — check arity from stored type
            const auto &ty = sym.get_type();
            if (!ty.has_value())
              return;
            auto *ft = std::get_if<ast::Type::Func>(&ty->data);
            if (!ft)
              return;
            int expected = static_cast<int>(ft->params.size());
            if (expected != static_cast<int>(arg_count)) {
              diag.error(span, "Function '" + name + "' expects " +
                                   std::to_string(expected) +
                                   " arguments, but " +
                                   std::to_string(arg_count) + " were provided")
                  .emit_to(diag);
            }
          },
      },
      sym.get_kind());
}

template <typename CheckExprFn>
static void check_call_expr(const CallExpr &call, Span span,
                            semantics::SemaCtxt &ctx, error::DiagCtxt &diag,
                            CheckExprFn &&check_expr_fn) {
  // (1) recursively check the callee
  check_expr_fn(*call.callee, call.callee->span);

  // (2) extract the human-readable name ("" for anonymous callables)
  auto func_name = extract_call_name(*call.callee);

  // (3) recursively check all arguments
  for (const auto &arg : call.args)
    check_expr_fn(*arg, arg->span);

  // (4) anonymous callables — nothing more to check at sema level
  if (func_name.empty())
    return;

  bool is_method = std::holds_alternative<MemberExpr>(call.callee->expr);

  // (5) resolve call target (builtin / mangled class method / plain name)
  optional<CallTargetInfo> target;
  if (is_method) {
    target = resolve_method_target(func_name, call, ctx, diag, span);
    if (!target)
      return; // resolution failed — error already reported
  } else {
    target = CallTargetInfo{func_name, func_name, false};
  }

  // (6) builtin path — arity from registry; push/pop side effects
  if (target->is_builtin) {
    check_builtin_arity(func_name, call.args.size(), span, ctx, diag);
    clear_list_len_for_mutating_builtin(func_name, call, ctx);
    return;
  }

  // (7) user-defined path — resolve the symbol
  auto sym = ctx.get_symbol_table().resolve(target->resolved_name);
  if (!sym) {
    diag.error(span, "Function '" + func_name + "' is not declared")
        .emit_to(diag);
    return;
  }

  // (8) guard: reject non-callable symbols (untyped vars deferred to
  //     type checker)
  if (!sym->is_callable()) {
    bool is_untyped_var =
        std::holds_alternative<VariableData>(sym->get_kind()) &&
        !sym->get_type().has_value();
    if (!is_untyped_var)
      diag.error(span, "'" + func_name + "' is not callable").emit_to(diag);
    return;
  }

  // (9) validate arity
  check_symbol_arity(*sym, func_name, is_method, call.args.size(), span, diag);
}

// ── helpers for expression checking ──────────────────────────────

// Try to evaluate a constant integer from an expression (literal or -literal).
static optional<int64_t> eval_const_index(const Expr &e) {
  if (auto *lit = std::get_if<LiteralExpr>(&e))
    if (auto *v = std::get_if<int64_t>(&lit->lit))
      return *v;
  if (auto *unary = std::get_if<UnaryExpr>(&e))
    if (unary->op == UnaryOp::Neg)
      if (auto *lit = std::get_if<LiteralExpr>(&unary->expr->expr))
        if (auto *v = std::get_if<int64_t>(&lit->lit))
          return -*v;
  return std::nullopt;
}

// Compile-time bounds check: report if idx_val >= known_len for a list.
static void check_list_bounds(optional<int64_t> idx_val,
                              optional<size_t> known_len, Span idx_span,
                              error::DiagCtxt &diag) {
  if (!idx_val.has_value() || !known_len.has_value())
    return;
  auto idx = *idx_val;
  if (idx < 0) {
    diag.error(idx_span,
               "Index out of bounds: negative index " + std::to_string(idx))
        .emit_to(diag);
    return;
  }
  if (static_cast<size_t>(idx) >= *known_len) {
    diag.error(idx_span, "Index out of bounds: index " + std::to_string(idx) +
                             " exceeds list length " +
                             std::to_string(*known_len))
        .emit_to(diag);
  }
}

// Determine whether an expression is a valid target for the & operator.
static bool is_valid_addrof_target(const Expr &e) {
  return std::holds_alternative<Variable>(e) ||
         std::holds_alternative<MemberExpr>(e) ||
         std::holds_alternative<IndexExpr>(e) ||
         (std::holds_alternative<UnaryExpr>(e) &&
          (std::get<UnaryExpr>(e).op == UnaryOp::Deref ||
           std::get<UnaryExpr>(e).op == UnaryOp::AddrOf));
}

void SemaChecker::check_expr(const ExprNode &expr, Span span,
                             semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
  std::visit(
      overloaded{

          [&](const LiteralExpr &lit) {},
          [&](const Variable &var) {
            auto sym = ctx.get_symbol_table().resolve(var.name);
            if (!sym) {
              diag.error(span, "Variable '" + var.name + "' is not declared")
                  .emit_to(diag);
            }
          },
          [&](const AssignExpr &assign) {
            check_expr(*assign.target, assign.target->span, ctx, diag);
            check_expr(*assign.value, assign.value->span, ctx, diag);
          },
          [&](const BinaryExpr &binary) {
            check_expr(*binary.lhs, binary.lhs->span, ctx, diag);
            check_expr(*binary.rhs, binary.rhs->span, ctx, diag);
          },
          [&](const UnaryExpr &unary) {
            check_expr(*unary.expr, unary.expr->span, ctx, diag);
            if (unary.op == UnaryOp::AddrOf &&
                !is_valid_addrof_target(unary.expr->expr)) {
              diag.error(span,
                         "Address-of operator can only be applied to "
                         "variables, member expressions, index expressions, "
                         "dereferences, or address-of expressions")
                  .emit_to(diag);
            }
          },
          [&](const CallExpr &call) {
            check_call_expr(call, span, ctx, diag,
                            [&](const ExprNode &e, Span s) {
                              check_expr(e, s, ctx, diag);
                            });
          },
          [&](const MemberExpr &member) {
            check_expr(*member.object, member.object->span, ctx, diag);
          },
          [&](const IndexExpr &index) {
            check_expr(*index.object, index.object->span, ctx, diag);
            check_expr(*index.index, index.index->span, ctx, diag);

            // Resolve a constant index value for compile-time bounds checking
            auto idx_val = eval_const_index(index.index->expr);

            // Determine the known list length from the object expression
            optional<size_t> known_len;
            if (auto *list = std::get_if<ListExpr>(&index.object->expr)) {
              known_len = list->elements.size();
            } else if (auto *var = std::get_if<Variable>(&index.object->expr)) {
              if (auto *sym = ctx.get_symbol_table().resolve(var->name))
                if (auto *vd = std::get_if<VariableData>(&sym->get_kind()))
                  known_len = vd->known_list_len;
            }

            check_list_bounds(idx_val, known_len, index.index->span, diag);
          },
          [&](const ListExpr &list) {
            for (const auto &element : list.elements) {
              check_expr(*element, element->span, ctx, diag);
            }
          },
          [&](const LambdaExpr &lambda) {
            ctx.get_symbol_table().enter_scope(ScopeKind::Function);
            for (size_t i = 0; i < lambda.params.size(); ++i) {
              auto &pt = lambda.params[i];
              bool is_ref = std::get_if<ast::Type::Ref>(&pt.ty.data) != nullptr;
              bool is_own = std::get_if<ast::Type::Own>(&pt.ty.data) != nullptr;
              Symbol param_sym = Symbol::new_parameter(
                  lambda.params[i].name, pt.ty.clone(), is_ref, is_own, span);
              ctx.get_symbol_table().declare(std::move(param_sym));
            }
            for (auto &stmt : lambda.body->stmts) {
              check_stmt(stmt, stmt.span, ctx, diag);
            }
            ctx.get_symbol_table().exit_scope();
          },
          [&](const auto &) {}},
      expr.expr);
}

void SemaChecker::check_global_var(const GlobalVar &gv, Span span,
                                   semantics::SemaCtxt &ctx,
                                   error::DiagCtxt &diag) {
  if (gv.ty.has_value())
    validate_ref_own_nesting(*gv.ty, span, diag);
  if (gv.init.has_value()) {
    check_expr(*gv.init, gv.init->span, ctx, diag);
  }
}

void SemaChecker::check_class_defaults(const Class &cls,
                                       semantics::SemaCtxt &ctx,
                                       error::DiagCtxt &diag) {
  for (const auto &field : cls.fields) {
    validate_ref_own_nesting(field.ty, Span{}, diag);
    if (field.init.has_value()) {
      check_expr(*field.init, field.init->span, ctx, diag);
    }
  }
}

} // namespace cat
