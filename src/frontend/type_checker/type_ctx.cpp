#include "type_ctx.h"
#include <unordered_set>

namespace cat::semantics {

  Type TypeCtxt::fresh_type_var() {
    uint32_t id = next_var_id++;
    return Type::var(id);
  }

  Type TypeCtxt::resolve_type(const Type &ty) const {
    std::unordered_set<TypedVar> visited;

    auto resolve = [&](auto &&self, const Type &t) -> Type {
      return std::visit(
          [&](const auto &v) -> Type {
            using T = std::decay_t<decltype(v)>;
            if constexpr (std::is_same_v<T, Type::Var>) {
              TypedVar tv{v.id};
              if (!visited.insert(tv).second) {
                return Type::var(tv);// occurs check — circular, unresolved
              }
              auto it = substitutions.find(tv);
              if (it != substitutions.end()) {
                Type resolved = self(self, it->second);
                visited.erase(tv);
                return resolved;
              }
              visited.erase(tv);
              return Type::var(v.id);
            } else if constexpr (std::is_same_v<T, Type::List>) {
              if (v.inner) {
                return Type::list(self(self, *v.inner));
              }
              return Type::list(Type::error());
            } else if constexpr (std::is_same_v<T, Type::Ptr>) {
              if (v.inner) {
                return Type::ptr(self(self, *v.inner));
              }
              return Type::ptr(Type::error());
            } else if constexpr (std::is_same_v<T, Type::Ref>) {
              if (v.inner) {
                return Type::ref(self(self, *v.inner));
              }
              return Type::ref(Type::error());
            } else if constexpr (std::is_same_v<T, Type::Own>) {
              if (v.inner) {
                return Type::own(self(self, *v.inner));
              }
              return Type::own(Type::error());
            } else if constexpr (std::is_same_v<T, Type::Func>) {
              std::vector<Type> resolved_params;
              resolved_params.reserve(v.params.size());
              for (const auto &p: v.params) {
                resolved_params.push_back(self(self, p));
              }
              Type resolved_ret = v.ret ? self(self, *v.ret) : Type::error();
              return Type::func(std::move(resolved_params), std::move(resolved_ret));
            } else {
              return t.clone();
            }
          },
          t.get_data()
      );
    };

    return resolve(resolve, ty);
  }

}// namespace cat::semantics
