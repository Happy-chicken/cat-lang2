#include "type.h"
#include <sstream>

namespace cat::semantics {

  Type Type::clone() const {
    return std::visit(
        [](const auto &v) -> Type {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Prim>) {
            return Type(Prim{v.kind});
          } else if constexpr (std::is_same_v<T, Var>) {
            return Type(Var{v.id});
          } else if constexpr (std::is_same_v<T, List>) {
            return Type(List{std::make_unique<Type>(v.inner ? v.inner->clone() : Type())});
          } else if constexpr (std::is_same_v<T, Ptr>) {
            return Type(Ptr{std::make_unique<Type>(v.inner ? v.inner->clone() : Type())});
          } else if constexpr (std::is_same_v<T, Func>) {
            vector<Type> cloned_params;
            cloned_params.reserve(v.params.size());
            for (const auto &p: v.params) {
              cloned_params.push_back(p.clone());
            }
            return Type(
                Func{std::move(cloned_params), std::make_unique<Type>(v.ret ? v.ret->clone() : Type())}
            );
          } else if constexpr (std::is_same_v<T, Class>) {
            return Type(Class{v.name});
          } else if constexpr (std::is_same_v<T, TraitObject>) {
            return Type(TraitObject{v.name});
          } else {// Error
            return Type(Error{});
          }
        },
        data
    );
  }

  bool Type::is_numeric() const {
    return std::visit(
        [](const auto &v) -> bool {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Prim>) {
            return v.kind == PrimType::Int || v.kind == PrimType::Float;
          }
          return false;
        },
        data
    );
  }

  bool Type::is_integer() const {
    return std::visit(
        [](const auto &v) -> bool {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Prim>) {
            return v.kind == PrimType::Int;
          }
          return false;
        },
        data
    );
  }

  bool Type::is_bool() const {
    return std::visit(
        [](const auto &v) -> bool {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Prim>) {
            return v.kind == PrimType::Bool;
          }
          return false;
        },
        data
    );
  }

  bool Type::is_void() const {
    return std::visit(
        [](const auto &v) -> bool {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Prim>) {
            return v.kind == PrimType::Void;
          }
          return false;
        },
        data
    );
  }

  string Type::to_string() const {
    return std::visit(
        [](const auto &v) -> string {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, Prim>) {
            switch (v.kind) {
              case PrimType::Int:
                return "int";
              case PrimType::Float:
                return "float";
              case PrimType::Bool:
                return "bool";
              case PrimType::Char:
                return "char";
              case PrimType::Str:
                return "str";
              case PrimType::Void:
                return "none";
            }
            return "(unknown)";
          } else if constexpr (std::is_same_v<T, Var>) {
            return "?" + std::to_string(v.id);
          } else if constexpr (std::is_same_v<T, List>) {
            return "list<" + (v.inner ? v.inner->to_string() : string("?")) +
                   ">";
          } else if constexpr (std::is_same_v<T, Ptr>) {
            return "ptr<" + (v.inner ? v.inner->to_string() : string("?")) +
                   ">";
          } else if constexpr (std::is_same_v<T, Func>) {
            std::ostringstream oss;
            oss << "(";
            for (size_t i = 0; i < v.params.size(); ++i) {
              if (i > 0) oss << ", ";
              oss << v.params[i].to_string();
            }
            oss << ") -> " << (v.ret ? v.ret->to_string() : string("?"));
            return oss.str();
          } else if constexpr (std::is_same_v<T, Class>) {
            return v.name;
          } else if constexpr (std::is_same_v<T, TraitObject>) {
            return "dyn " + v.name;
          } else {// Error
            return "{error}";
          }
        },
        data
    );
  }

}// namespace cat::semantics
