#include "inferer.h"
#include "error.h"
#include "symbol.h"
#include "unifier.h"
#include "type.h"
#include <memory>
#include <variant>

namespace cat::semantics {
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

Type Inferer::ast_type_to_semantic_type(const ast::Type &ast_type) {
  return std::visit(
      overloaded{
          [](const ast::Type::Int &) -> Type {
            return Type::prim(PrimType::Int);
          },
          [](const ast::Type::Float &) -> Type {
            return Type::prim(PrimType::Float);
          },
          [](const ast::Type::Bool &) -> Type {
            return Type::prim(PrimType::Bool);
          },
          [](const ast::Type::Char &) -> Type {
            return Type::prim(PrimType::Char);
          },
          [](const ast::Type::Str &) -> Type {
            return Type::prim(PrimType::Str);
          },
          [](const ast::Type::Void &) -> Type {
            return Type::prim(PrimType::Void);
          },
          [](const ast::Type::Ptr &ptr) -> Type {
            return Type::ptr(ast_type_to_semantic_type(*ptr.inner));
          },
          [](const ast::Type::List &list) -> Type {
            return Type::list(ast_type_to_semantic_type(*list.inner));
          },
          [](const ast::Type::Class &cls) -> Type {
            return Type::class_(cls.name);
          },
          [](const auto &) -> Type { return Type::error(); },
      },
      ast_type.data);
}

Type Inferer::infer_expr(const Expr &expr, Span span, SemaCtxt &ctxt,
                         error::DiagCtxt &diag) {
  return std::visit(
      overloaded{
          [&](const LiteralExpr &lit) { return infer_literal(lit); },
          [&](const Variable &var) {
            return infer_identifier(var, span, ctxt, diag);
          },
          [&](const AssignExpr &assign) {
            return infer_assignment(assign, span, ctxt, diag);
          },
          [&](const BinaryExpr &bin) {
            return infer_binary(bin, span, ctxt, diag);
          },
          [&](const UnaryExpr &unary) {
            return infer_unary(unary, span, ctxt, diag);
          },
          [&](const CallExpr &call) {
            return infer_call(call, span, ctxt, diag);
          },
          [&](const MemberExpr &member) {
            return infer_member(member, span, ctxt, diag);
          },
          [&](const IndexExpr &index) {
            return infer_index(index, span, ctxt, diag);
          },
          [&](const ListExpr &list) {
            return infer_list_literal(list, span, ctxt, diag);
          },
          [&](const auto &) -> Type {
            diag.error(span, "Unsupported expression type for type inference")
                .emit_to(diag);
            return Type::error();
          },
      },
      expr);
}

Type Inferer::infer_let_binding(const optional<Type> &declared_type,
                                const optional<ExprNode> &init_expr, Span span,
                                SemaCtxt &ctxt, error::DiagCtxt &diag) {
  auto inferred_type =
      init_expr ? infer_expr(init_expr->expr, init_expr->span, ctxt, diag)
                : ctxt.get_type_ctxt().fresh_type_var();
  if (!declared_type.has_value()) {
    return inferred_type;
  }
  auto unifier = Unifier(ctxt.get_type_ctxt());
  auto unify_result = unifier.unify(inferred_type, *declared_type);

  return std::visit(
      overloaded{
          [&](const Type &ok_type) -> Type { return ok_type.clone(); },
          [&](const error::UnifyError &err) -> Type {
            std::visit(
                overloaded{
                    [&](const error::Mismatch &mismatch) {
                      diag.error(span, "Type mismatch in let binding")
                          .note("Expected: " +
                                mismatch.expected.to_string())
                          .note("Found: " + mismatch.found.to_string())
                          .emit_to(diag);
                    },
                    [&](const error::InfiniteType &infinite) {
                      diag.error(span, "Infinite type in let binding")
                          .note("Type variable " +
                                std::to_string(infinite.var) +
                                " appears in its own definition")
                          .emit_to(diag);
                    },
                },
                err);
            return Type::error();
          },
      },
      unify_result);
}

Type Inferer::infer_literal(const LiteralExpr &lit) {
  return std::visit(
      overloaded{
          [](int64_t) -> Type { return Type::prim(PrimType::Int); },
          [](bool) -> Type { return Type::prim(PrimType::Bool); },
          [](float) -> Type { return Type::prim(PrimType::Float); },
          [](char) -> Type { return Type::prim(PrimType::Char); },
          [](const std::string &) -> Type {
            return Type::prim(PrimType::Str);
          },
      },
      lit.lit);
}

Type Inferer::infer_identifier(const Variable &var, Span span, SemaCtxt &ctxt,
                               error::DiagCtxt &diag) {
  auto sym = ctxt.get_symbol_table().resolve(var.name);
  if (!sym) {
    diag.error(span, "Variable '" + var.name + "' is not declared")
        .emit_to(diag);
    return Type::error();
  }
  return std::visit(
      overloaded{
          [&](const VariableData &) -> Type {
            if (sym->get_type().has_value())
              return ast_type_to_semantic_type(*sym->get_type());
            return Type::error();
          },
          [&](const ParameterData &) -> Type {
            if (sym->get_type().has_value())
              return ast_type_to_semantic_type(*sym->get_type());
            return Type::error();
          },
          [&](const FunctionData &func) -> Type {
            std::vector<Type> param_types;
            param_types.reserve(func.params.size());
            for (const auto &ast_ty : func.params) {
              param_types.push_back(ast_type_to_semantic_type(ast_ty));
            }
            Type ret_type = ast_type_to_semantic_type(func.return_type);
            return Type::func(std::move(param_types), std::move(ret_type));
          },
          [&](const ClassData &) -> Type {
            return Type::class_(sym->get_name());
          },
          [&](const auto &) -> Type { return Type::error(); },
      },
      sym->get_kind());
}

Type Inferer::infer_assignment(const AssignExpr &assign, Span span,
                               SemaCtxt &ctx, error::DiagCtxt &diag) {
  Type lhs_ty = infer_expr(assign.target->expr, span, ctx, diag);
  Type rhs_ty = infer_expr(assign.value->expr, span, ctx, diag);

  Unifier unifier(ctx.get_type_ctxt());
  auto result = unifier.unify(lhs_ty, rhs_ty);

  return std::visit(
      overloaded{
          [&](const Type &ty) -> Type { return ty.clone(); },
          [&](const error::UnifyError &err) -> Type {
            std::visit(
                overloaded{
                    [&](const error::Mismatch &mismatch) {
                      diag.error(span, "Type mismatch in assignment")
                          .note("Expected: " +
                                mismatch.expected.to_string())
                          .note("Found: " + mismatch.found.to_string())
                          .emit_to(diag);
                    },
                    [&](const error::InfiniteType &infinite) {
                      diag.error(span, "Infinite type in assignment")
                          .note("Type variable " +
                                std::to_string(infinite.var) +
                                " appears recursively")
                          .emit_to(diag);
                    },
                },
                err);
            return Type::error();
          },
      },
      result);
}

Type Inferer::infer_binary(const BinaryExpr &bin, Span span, SemaCtxt &ctxt,
                           error::DiagCtxt &diag) {
  Type lhs_ty = infer_expr(bin.lhs->expr, bin.lhs->span, ctxt, diag);
  Type rhs_ty = infer_expr(bin.rhs->expr, bin.rhs->span, ctxt, diag);

  switch (bin.op) {
  case BinaryOp::Add:
  case BinaryOp::Sub:
  case BinaryOp::Mul:
  case BinaryOp::Div: {
    Unifier unifier(ctxt.get_type_ctxt());
    auto result = unifier.unify(lhs_ty, rhs_ty);
    if (std::holds_alternative<error::UnifyError>(result)) {
      diag.error(span, "Type mismatch in arithmetic operation")
          .note("Left: " + lhs_ty.to_string())
          .note("Right: " + rhs_ty.to_string())
          .emit_to(diag);
      return Type::error();
    }
    Type resolved = ctxt.get_type_ctxt().resolve_type(lhs_ty);
    if (!resolved.is_numeric() && !resolved.is_error()) {
      diag.error(span, "Arithmetic operation requires numeric types")
          .note("Got: " + resolved.to_string())
          .emit_to(diag);
      return Type::error();
    }
    return resolved;
  }
  case BinaryOp::Eq:
  case BinaryOp::NotEq:
  case BinaryOp::Lt:
  case BinaryOp::Gt:
  case BinaryOp::Le:
  case BinaryOp::Ge: {
    Unifier unifier(ctxt.get_type_ctxt());
    (void)unifier.unify(lhs_ty, rhs_ty);
    return Type::prim(PrimType::Bool);
  }
  case BinaryOp::And:
  case BinaryOp::Or: {
    Unifier unifier(ctxt.get_type_ctxt());
    (void)unifier.unify(lhs_ty, Type::prim(PrimType::Bool));
    (void)unifier.unify(rhs_ty, Type::prim(PrimType::Bool));
    return Type::prim(PrimType::Bool);
  }
  default:
    return Type::error();
  }
}

Type Inferer::infer_unary(const UnaryExpr &unary, Span span, SemaCtxt &ctxt,
                          error::DiagCtxt &diag) {
  Type inner_ty = infer_expr(unary.expr->expr, unary.expr->span, ctxt, diag);

  switch (unary.op) {
  case UnaryOp::Neg: {
    Type resolved = ctxt.get_type_ctxt().resolve_type(inner_ty);
    if (!resolved.is_numeric() && !resolved.is_error()) {
      diag.error(span, "Negation requires numeric type")
          .note("Got: " + resolved.to_string())
          .emit_to(diag);
      return Type::error();
    }
    return resolved;
  }
  case UnaryOp::Not: {
    Unifier unifier(ctxt.get_type_ctxt());
    (void)unifier.unify(inner_ty, Type::prim(PrimType::Bool));
    return Type::prim(PrimType::Bool);
  }
  case UnaryOp::Deref: {
    Type resolved = ctxt.get_type_ctxt().resolve_type(inner_ty);
    if (auto *ptr = std::get_if<Type::Ptr>(&resolved.get_data())) {
      if (ptr->inner) {
        return ptr->inner->clone();
      }
    }
    diag.error(span, "Dereference requires pointer type")
        .note("Got: " + resolved.to_string())
        .emit_to(diag);
    return Type::error();
  }
  case UnaryOp::AddrOf: {
    return Type::ptr(std::move(inner_ty));
  }
  case UnaryOp::Inc:
  case UnaryOp::Dec: {
    return inner_ty;
  }
  default:
    return Type::error();
  }
}

Type Inferer::infer_call(const CallExpr &call, Span span, SemaCtxt &ctxt,
                         error::DiagCtxt &diag) {
  Type callee_ty = infer_expr(call.callee->expr, call.callee->span, ctxt, diag);

  std::vector<Type> arg_types;
  arg_types.reserve(call.args.size());
  for (const auto &arg : call.args) {
    arg_types.push_back(infer_expr(arg->expr, arg->span, ctxt, diag));
  }

  Type resolved = ctxt.get_type_ctxt().resolve_type(callee_ty);

  if (auto *func = std::get_if<Type::Func>(&resolved.get_data())) {
    bool is_method = std::holds_alternative<MemberExpr>(call.callee->expr);
    size_t param_offset = is_method ? 1 : 0;
    size_t expected_params = func->params.size() - param_offset;
    if (expected_params != arg_types.size()) {
      diag.error(span, "Argument count mismatch: expected " +
                           std::to_string(expected_params) + ", got " +
                           std::to_string(arg_types.size()))
          .emit_to(diag);
      return Type::error();
    }

    Unifier unifier(ctxt.get_type_ctxt());
    for (size_t i = 0; i < arg_types.size(); ++i) {
      auto result = unifier.unify(arg_types[i], func->params[i + param_offset]);
      if (std::holds_alternative<error::UnifyError>(result)) {
        diag.error(span, "Argument " + std::to_string(i + 1) + " type mismatch")
            .note("Expected: " + func->params[i + param_offset].to_string())
            .note("Found: " + arg_types[i].to_string())
            .emit_to(diag);
        return Type::error();
      }
    }

    if (func->ret) {
      return func->ret->clone();
    }
    return Type::error();
  }

  if (auto *cls = std::get_if<Type::Class>(&resolved.get_data())) {
    auto sym = ctxt.get_symbol_table().resolve_global(cls->name);
    if (sym) {
      if (auto *class_data = std::get_if<ClassData>(&sym->get_kind())) {
        size_t required = 0;
        for (bool has_def : class_data->has_default)
          if (!has_def) ++required;
        if (arg_types.size() < required || arg_types.size() > class_data->fields.size()) {
          diag.error(span, "Constructor argument count mismatch: expected " +
                               std::to_string(required) + " to " +
                               std::to_string(class_data->fields.size()) +
                               ", got " + std::to_string(arg_types.size()))
              .emit_to(diag);
          return Type::error();
        }
        Unifier unifier(ctxt.get_type_ctxt());
        for (size_t i = 0; i < arg_types.size(); ++i) {
          auto expected = ast_type_to_semantic_type(class_data->fields[i].second);
          auto result = unifier.unify(arg_types[i], expected);
          if (std::holds_alternative<error::UnifyError>(result)) {
            diag.error(span, "Constructor argument " + std::to_string(i + 1) +
                                 " type mismatch for field '" +
                                 class_data->fields[i].first + "'")
                .emit_to(diag);
            return Type::error();
          }
        }
        return Type::class_(cls->name);
      }
    }
    diag.error(span, "Class '" + cls->name + "' not found")
        .emit_to(diag);
    return Type::error();
  }

  diag.error(span, "Called object is not a function or class")
      .emit_to(diag);
  return Type::error();
}

Type Inferer::infer_member(const MemberExpr &member, Span span, SemaCtxt &ctxt,
                           error::DiagCtxt &diag) {
  Type obj_ty =
      infer_expr(member.object->expr, member.object->span, ctxt, diag);
  Type resolved = ctxt.get_type_ctxt().resolve_type(obj_ty);

  if (auto *cls = std::get_if<Type::Class>(&resolved.get_data())) {
    auto sym = ctxt.get_symbol_table().resolve_global(cls->name);
    if (!sym) {
      diag.error(span, "Class '" + cls->name + "' not found")
          .emit_to(diag);
      return Type::error();
    }

    if (auto *class_data = std::get_if<ClassData>(&sym->get_kind())) {
      for (const auto &[field_name, field_ty] : class_data->fields) {
        if (field_name == member.field) {
          return ast_type_to_semantic_type(field_ty);
        }
      }
    }

    std::string mangled = cls->name + "_" + member.field;
    auto method_sym = ctxt.get_symbol_table().resolve_global(mangled);
    if (method_sym) {
      if (auto *func_data = std::get_if<FunctionData>(&method_sym->get_kind())) {
        std::vector<Type> param_types;
        param_types.reserve(func_data->params.size());
        for (const auto &ast_ty : func_data->params) {
          param_types.push_back(ast_type_to_semantic_type(ast_ty));
        }
        Type ret_type = ast_type_to_semantic_type(func_data->return_type);
        return Type::func(std::move(param_types), std::move(ret_type));
      }
    }

    diag.error(span, "Field or method '" + member.field +
                         "' not found in class '" + cls->name + "'")
        .emit_to(diag);
    return Type::error();
  }

  diag.error(span, "Member access requires a class type")
      .note("Got: " + resolved.to_string())
      .emit_to(diag);
  return Type::error();
}

Type Inferer::infer_index(const IndexExpr &index, Span span, SemaCtxt &ctxt,
                          error::DiagCtxt &diag) {
  Type obj_ty =
      infer_expr(index.object->expr, index.object->span, ctxt, diag);
  Type resolved = ctxt.get_type_ctxt().resolve_type(obj_ty);

  if (auto *list = std::get_if<Type::List>(&resolved.get_data())) {
    Type idx_ty =
        infer_expr(index.index->expr, index.index->span, ctxt, diag);

    Unifier unifier(ctxt.get_type_ctxt());
    auto result = unifier.unify(idx_ty, Type::prim(PrimType::Int));
    if (std::holds_alternative<error::UnifyError>(result)) {
      diag.error(span, "Index must be an integer type")
          .note("Got: " + idx_ty.to_string())
          .emit_to(diag);
      return Type::error();
    }

    if (list->inner) {
      return list->inner->clone();
    }
    return Type::error();
  }

  diag.error(span, "Indexing requires a list type")
      .note("Got: " + resolved.to_string())
      .emit_to(diag);
  return Type::error();
}

Type Inferer::infer_list_literal(const ListExpr &list, Span span,
                                 SemaCtxt &ctxt, error::DiagCtxt &diag) {
  if (list.elements.empty()) {
    Type elem_ty = ctxt.get_type_ctxt().fresh_type_var();
    return Type::list(std::move(elem_ty));
  }

  Type elem_ty =
      infer_expr(list.elements[0]->expr, list.elements[0]->span, ctxt, diag);

  Unifier unifier(ctxt.get_type_ctxt());
  for (size_t i = 1; i < list.elements.size(); ++i) {
    Type ty = infer_expr(list.elements[i]->expr, list.elements[i]->span, ctxt,
                         diag);
    auto result = unifier.unify(elem_ty, ty);
    if (std::holds_alternative<error::UnifyError>(result)) {
      diag.error(span, "List element type mismatch at index " +
                           std::to_string(i))
          .note("Expected: " + elem_ty.to_string())
          .note("Found: " + ty.to_string())
          .emit_to(diag);
      return Type::error();
    }
  }

  return Type::list(ctxt.get_type_ctxt().resolve_type(elem_ty));
}

} // namespace cat::semantics
