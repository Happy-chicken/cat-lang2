#include "unifier.h"
#include <variant>

namespace cat::semantics {
template <class... Ts>
struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;

error::UnifyResult<semantics::Type> Unifier::unify(const Type &t1,
                                                    const Type &t2) {
  auto a = type_ctxt.resolve_type(t1);
  auto b = type_ctxt.resolve_type(t2);

  if (std::holds_alternative<Type::Error>(a.get_data()) ||
      std::holds_alternative<Type::Error>(b.get_data())) {
    return Type::error();
  }

  if (auto *va = std::get_if<Type::Var>(&a.get_data())) {
    if (auto *vb = std::get_if<Type::Var>(&b.get_data())) {
      if (va->id == vb->id)
        return a.clone();
    }
    return unify_var(va->id, b);
  }
  if (auto *vb = std::get_if<Type::Var>(&b.get_data())) {
    return unify_var(vb->id, a);
  }

  return std::visit(
      overloaded{
          [&](const Type::Prim &pa, const Type::Prim &pb)
              -> error::UnifyResult<semantics::Type> {
            if (pa.kind == pb.kind)
              return a.clone();
            return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
          },
          [&](const Type::List &la, const Type::List &lb)
              -> error::UnifyResult<semantics::Type> {
            if (!la.inner || !lb.inner)
              return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
            auto inner = unify(*la.inner, *lb.inner);
            if (std::holds_alternative<error::UnifyError>(inner))
              return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
            return Type::list(std::move(std::get<Type>(inner)));
          },
          [&](const Type::Ptr &pa, const Type::Ptr &pb)
              -> error::UnifyResult<semantics::Type> {
            if (!pa.inner || !pb.inner)
              return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
            auto inner = unify(*pa.inner, *pb.inner);
            if (std::holds_alternative<error::UnifyError>(inner))
              return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
            return Type::ptr(std::move(std::get<Type>(inner)));
          },
          [&](const Type::Func &fa, const Type::Func &fb)
              -> error::UnifyResult<semantics::Type> {
            if (fa.params.size() != fb.params.size())
              return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
            std::vector<Type> unified_params;
            for (size_t i = 0; i < fa.params.size(); ++i) {
              auto p = unify(fa.params[i], fb.params[i]);
              if (std::holds_alternative<error::UnifyError>(p))
                return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
              unified_params.push_back(std::move(std::get<Type>(p)));
            }
            if (!fa.ret || !fb.ret)
              return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
            auto ret = unify(*fa.ret, *fb.ret);
            if (std::holds_alternative<error::UnifyError>(ret))
              return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
            return Type::func(std::move(unified_params),
                              std::move(std::get<Type>(ret)));
          },
          [&](const Type::Class &ca, const Type::Class &cb)
              -> error::UnifyResult<semantics::Type> {
            if (ca.name == cb.name)
              return a.clone();
            return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
          },
          [&](const Type::TraitObject &ta, const Type::TraitObject &tb)
              -> error::UnifyResult<semantics::Type> {
            if (ta.name == tb.name)
              return a.clone();
            return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
          },
          [&](const auto &, const auto &)
              -> error::UnifyResult<semantics::Type> {
            return error::UnifyError{error::Mismatch{a.clone(), b.clone()}};
          },
      },
      a.get_data(), b.get_data());
}

error::UnifyResult<semantics::Type> Unifier::unify_var(TypedVar var,
                                                       const Type &ty) {
  auto existing = type_ctxt.lookup_subst(var);
  if (existing) {
    return unify(*existing, ty);
  }
  if (occurs_check(var, ty)) {
    return error::UnifyError{error::InfiniteType{ty.clone(), var}};
  }
  auto cloned = ty.clone();
  type_ctxt.substitute(var, std::move(cloned));
  return ty.clone();
}

bool Unifier::occurs_check(TypedVar var, const Type &ty) {
  auto resolved = type_ctxt.resolve_type(ty);
  return std::visit(
      overloaded{
          [&](const Type::Var &v) { return v.id == var; },
          [&](const Type::List &l) {
            return l.inner && occurs_check(var, *l.inner);
          },
          [&](const Type::Ptr &p) {
            return p.inner && occurs_check(var, *p.inner);
          },
          [&](const Type::Func &f) {
            for (const auto &param : f.params) {
              if (occurs_check(var, param))
                return true;
            }
            return f.ret && occurs_check(var, *f.ret);
          },
          [&](const auto &) { return false; },
      },
      resolved.get_data());
}

} // namespace cat::semantics
