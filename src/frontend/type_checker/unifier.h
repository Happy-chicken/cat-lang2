#pragma once
#include "error.h"
#include "type_ctx.h"
namespace cat::semantics {
class Unifier {
public:
  Unifier(TypeCtxt &type_ctxt) : type_ctxt(type_ctxt) {}

  error::UnifyResult<semantics::Type> unify(const Type &t1, const Type &t2);

  error::UnifyResult<semantics::Type> unify_var(TypedVar var, const Type &ty);

  bool occurs_check(TypedVar var, const Type &ty);

private:
  TypeCtxt &type_ctxt;
};
} // namespace cat::semantics
