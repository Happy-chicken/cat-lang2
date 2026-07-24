#include "type_checker.h"
#include "../ast/type.h"
#include "type.h"
#include "unifier.h"

namespace cat::semantics {

  static optional<ast::Type> semantic_type_to_ast_type(const Type &ty) {
    return std::visit(
        overloaded{
            [](const Type::Prim &prim) -> optional<ast::Type> {
              switch (prim.kind) {
                case PrimType::Int:
                  return ast::type_int();
                case PrimType::Float:
                  return ast::type_float();
                case PrimType::Bool:
                  return ast::type_bool();
                case PrimType::Char:
                  return ast::type_char();
                case PrimType::Str:
                  return ast::type_str();
                case PrimType::Void:
                  return ast::type_void();
              }
              return std::nullopt;
            },
            [&](const Type::Ptr &ptr) -> optional<ast::Type> {
              auto inner = semantic_type_to_ast_type(*ptr.inner);
              if (inner.has_value())
                return ast::type_ptr(inner->clone());
              return std::nullopt;
            },
            [&](const Type::Ref &ref) -> optional<ast::Type> {
              auto inner = semantic_type_to_ast_type(*ref.inner);
              if (inner.has_value()) {
                return ast::type_ref(inner->clone());
              }
              return std::nullopt;
            },
            [&](const Type::Own &own) -> optional<ast::Type> {
              auto inner = semantic_type_to_ast_type(*own.inner);
              if (inner.has_value())
                return ast::type_own(inner->clone());
              return std::nullopt;
            },
            [&](const Type::List &list) -> optional<ast::Type> {
              auto inner = semantic_type_to_ast_type(*list.inner);
              if (inner.has_value())
                return ast::type_list(inner->clone());
              return std::nullopt;
            },
            [&](const Type::Class &cls) -> optional<ast::Type> {
              return ast::type_class(cls.name);
            },
            [&](const Type::Func &func) -> optional<ast::Type> {
              vector<uptr<ast::Type>> param_types;
              param_types.reserve(func.params.size());
              for (auto &p : func.params) {
                if (p) {
                  auto inner = semantic_type_to_ast_type(*p);
                  param_types.push_back(std::make_unique<ast::Type>(std::move(*inner)));
                }
              }
              std::optional<ast::Type> ret;
              if (func.ret) {
                ret = semantic_type_to_ast_type(*func.ret);
              } else {
                ret = ast::type_void();
              }
              return ast::Type(ast::Type::Func{std::move(param_types), std::make_unique<ast::Type>(std::move(*ret))});
            },
            [&](const auto &) -> optional<ast::Type> { return std::nullopt; },
        },
        ty.get_data()
    );
  }

  void TypeChecker::check_function(const FunctionDef &func, Span span, SemaCtxt &ctx, error::DiagCtxt &diag) {
    ctx.get_symbol_table().enter_scope(ScopeKind::Function);

    for (const auto &param: func.function_header.params) {
      bool is_ref = std::get_if<ast::Type::Ref>(&param.ty.data) != nullptr;
      bool is_own = std::get_if<ast::Type::Own>(&param.ty.data) != nullptr;
      Symbol param_sym =
          Symbol::new_parameter(param.name, param.ty.clone(), is_ref, is_own, span);
      ctx.get_symbol_table().declare(std::move(param_sym));
    }

    if (func.function_header.return_type.has_value()) {
      current_return_type =
          Inferer::ast_type_to_semantic_type(*func.function_header.return_type);
    } else {
      current_return_type = Type::prim(PrimType::Void);
    }

    for (const auto &stmt_node: func.body.stmts) {
      check_stmt(stmt_node, stmt_node.span, ctx, diag);
    }

    current_return_type.reset();
    ctx.get_symbol_table().exit_scope();
  }

  void TypeChecker::check_stmt(const StmtNode &stmt_node, Span span, SemaCtxt &ctx, error::DiagCtxt &diag) {
    std::visit(
        overloaded{
            [&](const VarDefStmt &var_def) {
              optional<Type> declared_type;
              if (var_def.ty.has_value()) {
                declared_type =
                    Inferer::ast_type_to_semantic_type(*var_def.ty);
              }
              Type inferred = inferer.infer_let_binding(declared_type, var_def.init, span, ctx, diag);

              if (inferred.is_error()) return;

              optional<ast::Type> stored_type;
              if (var_def.ty.has_value()) {
                stored_type = var_def.ty->clone();
              } else {
                stored_type = semantic_type_to_ast_type(
                    ctx.get_type_ctxt().resolve_type(inferred)
                );
              }
              bool var_is_ref = std::get_if<ast::Type::Ref>(&stored_type->data) != nullptr;
              bool var_is_own = std::get_if<ast::Type::Own>(&stored_type->data) != nullptr;
              Symbol var_sym = Symbol::new_variable(
                  var_def.name, std::move(stored_type), false, span, var_is_ref, var_is_own
              );
              ctx.get_symbol_table().declare(std::move(var_sym));
            },
            [&](const IfStmt &if_stmt) {
              Type cond_ty = inferer.infer_expr(if_stmt.condition.expr, if_stmt.condition.span, ctx, diag);
              check_condition_is_bool(cond_ty, if_stmt.condition.span, ctx, diag);
              check_block(*if_stmt.then_branch, span, ctx, diag);

              for (const auto &[elif_cond, elif_block]:
                   if_stmt.elif_branch) {
                Type elif_ty =
                    inferer.infer_expr(elif_cond.expr, elif_cond.span, ctx, diag);
                check_condition_is_bool(elif_ty, elif_cond.span, ctx, diag);
                check_block(*elif_block, span, ctx, diag);
              }

              if (if_stmt.else_branch) {
                check_block(*if_stmt.else_branch, span, ctx, diag);
              }
            },
            [&](const LoopStmt &loop) {
              Type cond_ty = inferer.infer_expr(loop.condition.expr, loop.condition.span, ctx, diag);
              check_condition_is_bool(cond_ty, loop.condition.span, ctx, diag);
              check_block(*loop.body, span, ctx, diag);
            },
            [&](const ExprStmt &expr_stmt) {
              inferer.infer_expr(expr_stmt.expr.expr, expr_stmt.expr.span, ctx, diag);
            },
            [&](const ReturnStmt &ret) {
              if (ret.expr.has_value()) {
                Type ret_ty = inferer.infer_expr(ret.expr->expr, ret.expr->span, ctx, diag);
                if (current_return_type.has_value()) {
                  Unifier unifier(ctx.get_type_ctxt());
                  auto result =
                      unifier.unify(ret_ty, *current_return_type);
                  if (std::holds_alternative<error::UnifyError>(result)) {
                    diag.error(span, "Return type mismatch")
                        .note("Expected: " + current_return_type->to_string())
                        .note("Found: " + ret_ty.to_string())
                        .emit_to(diag);
                  }
                }
              } else {
                if (current_return_type.has_value() &&
                    !current_return_type->is_void()) {
                  diag.error(span, "Non-void function must return a value")
                      .emit_to(diag);
                }
              }
            },
            [&](const BreakStmt &) {},
            [&](const ContinueStmt &) {},
            [&](const BlockStmt &block_stmt) {
              check_block(*block_stmt.block, span, ctx, diag);
            },
        },
        stmt_node.stmt
    );
  }

  void TypeChecker::check_block(const Block &block, Span span, SemaCtxt &ctx, error::DiagCtxt &diag) {
    ctx.get_symbol_table().enter_scope(ScopeKind::Block);
    for (const auto &stmt_node: block.stmts) {
      check_stmt(stmt_node, stmt_node.span, ctx, diag);
    }
    ctx.get_symbol_table().exit_scope();
  }

  void TypeChecker::check_condition_is_bool(const Type &ty, Span span, SemaCtxt &ctx, error::DiagCtxt &diag) {
    Type resolved = ctx.get_type_ctxt().resolve_type(ty);
    if (!resolved.is_bool() && !resolved.is_error()) {
      diag.error(span, "Condition must be a boolean expression")
          .note("Got: " + resolved.to_string())
          .emit_to(diag);
    }
  }

  void TypeChecker::check_global_var(const GlobalVar &gv, Span span, SemaCtxt &ctx, error::DiagCtxt &diag) {
    optional<Type> declared_type;
    if (gv.ty.has_value()) {
      declared_type = Inferer::ast_type_to_semantic_type(*gv.ty);
    }
    inferer.infer_let_binding(declared_type, gv.init, span, ctx, diag);
  }

  void TypeChecker::check_class_defaults(const Class &cls, SemaCtxt &ctx, error::DiagCtxt &diag) {
    for (const auto &field: cls.fields) {
      if (field.init.has_value()) {
        Type decl_ty = Inferer::ast_type_to_semantic_type(field.ty);
        Type init_ty = inferer.infer_expr(field.init->expr, field.init->span, ctx, diag);

        Unifier unifier(ctx.get_type_ctxt());
        auto result = unifier.unify(init_ty, decl_ty);
        if (std::holds_alternative<error::UnifyError>(result)) {
          diag.error(field.init->span, "Field default value type mismatch for '" + field.name + "'")
              .note("Expected: " + decl_ty.to_string())
              .note("Found: " + init_ty.to_string())
              .emit_to(diag);
        }
      }
    }
  }

}// namespace cat::semantics
