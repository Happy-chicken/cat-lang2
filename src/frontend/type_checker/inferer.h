#pragma once
#include "diag.h"
#include "expr.h"
#include "sema_ctx.h"
#include "type.h"
namespace cat::semantics {
class Inferer {
public:
  Inferer() {}

  Type infer_expr(const Expr &expr, Span span, SemaCtxt &ctxt,
                  error::DiagCtxt &diag);

  Type infer_let_binding(const optional<Type> &declared_type,
                         const optional<ExprNode> &init_expr, Span span,
                         SemaCtxt &ctxt, error::DiagCtxt &diag);

  static Type ast_type_to_semantic_type(const ast::Type &ast_type);

private:
  Type infer_literal(const LiteralExpr &lit);

  Type infer_identifier(const Variable &var, Span span, SemaCtxt &ctxt,
                        error::DiagCtxt &diag);

  Type infer_assignment(const AssignExpr &assign, Span span, SemaCtxt &ctxt,
                        error::DiagCtxt &diag);

  Type infer_binary(const BinaryExpr &bin, Span span, SemaCtxt &ctxt,
                    error::DiagCtxt &diag);

  Type infer_unary(const UnaryExpr &unary, Span span, SemaCtxt &ctxt,
                   error::DiagCtxt &diag);

  Type infer_call(const CallExpr &call, Span span, SemaCtxt &ctxt,
                  error::DiagCtxt &diag);

  Type infer_member(const MemberExpr &member, Span span, SemaCtxt &ctxt,
                    error::DiagCtxt &diag);

  Type infer_index(const IndexExpr &index, Span span, SemaCtxt &ctxt,
                   error::DiagCtxt &diag);

  Type infer_list_literal(const ListExpr &list, Span span, SemaCtxt &ctxt,
                          error::DiagCtxt &diag);
};
} // namespace cat::semantics
