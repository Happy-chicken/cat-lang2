#include "sema_checker.h"
#include "expr.h"
#include "scope.h"
#include "stmt.h"
#include "symbol.h"
#include <variant>

namespace cat {

  void SemaChecker::check_function(const FunctionDef &func, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    ctx.get_symbol_table().enter_scope(ScopeKind::Function);
    for (const auto &param : func.function_header.params) {
      Symbol param_sym = Symbol::new_parameter(param.name, param.ty.clone(), param.is_ref, span);
      auto existing = ctx.get_symbol_table().declare(std::move(param_sym));
      if (existing) {
        diag.error(span, "Parameter name '" + param.name + "' is already declared in this scope")
            .note("Previous declaration was here: " + existing->get_span().to_string())
            .emit_to(diag);
      }
    }

    for (const auto &stmt : func.body.stmts) {
      check_stmt(stmt, stmt.span, ctx, diag);
    }
    ctx.get_symbol_table().exit_scope();
  }

  void SemaChecker::check_impl(const Impl &imp, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    if (ctx.get_symbol_table().resolve_global(imp.class_name) == nullptr) {
      diag.error(span, "Class '" + imp.class_name + "' is not declared")
          .emit_to(diag);
    }
    if (imp.trait_name.has_value()) {
      const string &trait_name = *imp.trait_name;
      if (auto sym = ctx.get_symbol_table().resolve_global(trait_name)) {
        std::visit(
            overloaded{
                [&](const TraitData& trait) {
                  for (const auto & required: trait.methods) {
                  auto found = std::find_if(imp.methods.begin(), imp.methods.end(),
                      [&](const FunctionDef &method) {
                        return method.function_header.name == required;
                      });
                  if (found == imp.methods.end()) {
                    diag.error(span, "Method '" + required + "' is required by trait '" + trait_name + "' but not implemented in class '" + imp.class_name + "'")
                        .emit_to(diag);
                  }
                }
                },
                [&](const auto &) {
                }},
            sym->get_kind());
      }
      else {
        diag.error(span, "Trait '" + trait_name + "' is not declared")
            .emit_to(diag);
      }
    }

    for (const auto &method : imp.methods) {
      check_function(method, span, ctx, diag);
    }
  }

  void SemaChecker::check_block(const Block &block, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    ctx.get_symbol_table().enter_scope(ScopeKind::Block);
    for (const auto &stmt : block.stmts) {
      check_stmt(stmt, stmt.span, ctx, diag);
    }
    ctx.get_symbol_table().exit_scope();
  }

  void SemaChecker::check_stmt(const StmtNode &stmt, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    std::visit(
        overloaded{
            [&](const VarDefStmt &var_def) { 
              if (var_def.init.has_value()) {
                check_expr(*var_def.init, var_def.init->span, ctx, diag);
              }
              auto sym = Symbol::new_variable(var_def.name, var_def.ty ? var_def.ty->clone() : ast::Type{}, false, span);
              auto existing = ctx.get_symbol_table().declare(std::move(sym));
              if (existing) {
                diag.error(span, "Variable '" + var_def.name + "' is already declared in this scope")
                    .note("Previous declaration was here: " + existing->get_span().to_string())
                    .emit_to(diag);
              }
            },
            [&](const IfStmt &if_stmt) {
              const auto &condition = if_stmt.condition;
              const auto &then_branch = if_stmt.then_branch;
              const auto& elif_branches = if_stmt.elif_branch;
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
                check_block(*block_stmt.block.get(), block_stmt.block->stmts[0].span, ctx, diag);
              }
            },
            [&](const auto&) {}
          },
        stmt.stmt);
  }

  void SemaChecker::check_expr(const ExprNode &expr, Span span,semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    std::visit(overloaded{

      [&](const LiteralExpr& lit){},
      [&](const Variable& var){
        auto sym = ctx.get_symbol_table().resolve(var.name);
        if (!sym) {
          diag.error(span, "Variable '" + var.name + "' is not declared")
              .emit_to(diag);
        }
      },
      [&](const AssignExpr& assign){
        check_expr(*assign.target, assign.target->span, ctx, diag);
        check_expr(*assign.value, assign.value->span, ctx, diag);
      },
      [&](const BinaryExpr& binary){
        check_expr(*binary.lhs, binary.lhs->span, ctx, diag);
        check_expr(*binary.rhs, binary.rhs->span, ctx, diag);
      },
      [&](const UnaryExpr& unary){
        check_expr(*unary.expr, unary.expr->span, ctx, diag);
        if (unary.op == UnaryOp::AddrOf) {
          if (!(std::holds_alternative<Variable>(unary.expr->expr) || 
                std::holds_alternative<MemberExpr>(unary.expr->expr) || 
                std::holds_alternative<IndexExpr>(unary.expr->expr))) {
            diag.error(span, "Address-of operator can only be applied to variables, member expressions, or index expressions")
                .emit_to(diag);
          }
        }
      },
      [&](const CallExpr& call){
        check_expr(*call.callee, call.callee->span, ctx, diag);
        auto func_name = std::visit(overloaded{
            [&](const Variable& var) -> string { return var.name; },
            [&](const MemberExpr& member) -> string { return member.field; },
            [&](const auto&) -> string { return ""; }
        }, call.callee->expr);
        if (!func_name.empty()) {
          auto sym = ctx.get_symbol_table().resolve(func_name);
          if (!sym) {
            diag.error(span, "Function '" + func_name + "' is not declared")
                .emit_to(diag);
          } else if (!sym->is_callable()) {
            diag.error(span, "'" + func_name + "' is not callable")
                .emit_to(diag);
          }
          std::visit(overloaded{
              [&](const FunctionData& func_data) {
                bool is_method = std::holds_alternative<MemberExpr>(call.callee->expr);
                int expected_args = func_data.params.size();
                if (is_method) {
                  expected_args = std::min(expected_args - 1, 0); // delete self for method calls
                }
                if (expected_args != call.args.size()) {
                  diag.error(span, "Function '" 
                                        + func_name 
                                        + "' expects " 
                                        + std::to_string(func_data.params.size()) 
                                        + " arguments, but " 
                                        + std::to_string(call.args.size()) 
                                        + " were provided")
                      .emit_to(diag);
                }
              },
              [&](const ClassData& class_data) {
                if (class_data.fields.size() != call.args.size()) {
                  diag.error(span, "Class '" 
                                        + func_name 
                                        + "' expects " 
                                        + std::to_string(class_data.fields.size()) 
                                        + " arguments, but " 
                                        + std::to_string(call.args.size()) 
                                        + " were provided")
                      .emit_to(diag);
                }
              },
              [&](const auto&) {}
          }, sym->get_kind());
        }
        for (const auto &arg : call.args) {
          check_expr(*arg, arg->span, ctx, diag);
        }
      },
      [&](const MemberExpr& member){
        check_expr(*member.object, member.object->span, ctx, diag);
      },
      [&](const IndexExpr& index){
        check_expr(*index.object, index.object->span, ctx, diag);
        check_expr(*index.index, index.index->span, ctx, diag);
      },
      [&](const ListExpr& list){
        for (const auto &element : list.elements) {
          check_expr(*element, element->span, ctx, diag);
        }
      },
      [&](const auto &){}}, 
        expr.expr);
  }

  void SemaChecker::check_global_var(const GlobalVar &gv, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    if (gv.init.has_value()) {
      check_expr(*gv.init, gv.init->span, ctx, diag);
    }
  }

  void SemaChecker::check_class_defaults(const Class &cls, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    for (const auto &field : cls.fields) {
      if (field.init.has_value()) {
        check_expr(*field.init, field.init->span, ctx, diag);
      }
    }
  }
  
} // namespace cat