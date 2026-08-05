#include "type.h"
#include "type_checker.h"
#include <sstream>
#include <type_traits>
#include <variant>

namespace cat::semantics {

Type Type::clone() const {
  return std::visit(
      [](const auto &v) -> Type {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Prim>) {
          return Type(Prim{v.kind});
        } else if constexpr (std::is_same_v<T, Var>) {
          return Type(Var{v.id});
        } else if constexpr (std::is_same_v<T, Ptr>) {
          return Type(
              Ptr{std::make_unique<Type>(v.inner ? v.inner->clone() : Type())});
        } else if constexpr (std::is_same_v<T, Ref>) {
          return Type(
              Ref{std::make_unique<Type>(v.inner ? v.inner->clone() : Type())});
        } else if constexpr (std::is_same_v<T, CRef>) {
          return Type(
              CRef{std::make_unique<Type>(v.inner ? v.inner->clone() : Type())});
        } else if constexpr (std::is_same_v<T, Own>) {
          return Type(
              Own{std::make_unique<Type>(v.inner ? v.inner->clone() : Type())});
        } else if constexpr (std::is_same_v<T, Func>) {
          vector<uptr<Type>> cloned_params;
          cloned_params.reserve(v.params.size());
          for (const auto &p : v.params) {
            cloned_params.push_back(std::make_unique<Type>(p->clone()));
          }
          return Type(
              Func{std::move(cloned_params),
                   std::make_unique<Type>(v.ret ? v.ret->clone() : Type())});
        } else if constexpr (std::is_same_v<T, StructType>) {
          StructType cloned = std::visit(
            [](const auto &innner) -> StructType {
              using InnerT = std::decay_t<decltype(innner)>;
              if constexpr (std::is_same_v<InnerT, StructType::Struct>) {
                return StructType(StructType::Struct{innner.name});
              } else if constexpr (std::is_same_v<InnerT, StructType::TraitObject>) {
                return StructType(StructType::TraitObject{innner.name});
              } else if constexpr (std::is_same_v<InnerT, StructType::Str>) {
                return StructType(StructType::Str{});
              } else if constexpr (std::is_same_v<InnerT, StructType::List>) {
                return StructType(StructType::List{
                    std::make_unique<Type>(innner.inner ? innner.inner->clone() : Type())});
              }
            },
            v.get_data());
          return Type(std::move(cloned));
        }
        else { // Error
          return Type(Error{});
        }
      },
      data);
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
      data);
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
      data);
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
      data);
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
      data);
}

bool Type::is_error() const {
  return std::visit(
      [](const auto &v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Error>) {
          return true;
        }
        return false;
      },
      data);
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
          case PrimType::Void:
            return "none";
          }
          return "(unknown)";
        } else if constexpr (std::is_same_v<T, Var>) {
          return "?" + std::to_string(v.id);
        } else if constexpr (std::is_same_v<T, Ptr>) {
          return "ptr<" + (v.inner ? v.inner->to_string() : string("?")) + ">";
        } else if constexpr (std::is_same_v<T, Ref>) {
          return "ref<" + (v.inner ? v.inner->to_string() : string("?")) + ">";
        } else if constexpr (std::is_same_v<T, CRef>) {
          return "cref<" + (v.inner ? v.inner->to_string() : string("?")) + ">";
        } else if constexpr (std::is_same_v<T, Own>) {
          return "own<" + (v.inner ? v.inner->to_string() : string("?")) + ">";
        } else if constexpr (std::is_same_v<T, Func>) {
          std::ostringstream oss;
          oss << "(";
          for (size_t i = 0; i < v.params.size(); ++i) {
            if (i > 0)
              oss << ", ";
            oss << v.params[i]->to_string();
          }
          oss << ") -> " << (v.ret ? v.ret->to_string() : string("?"));
          return oss.str();
        } else if constexpr (std::is_same_v<T, StructType>) {
          return std::visit(overloaded{
            [](const StructType::Str &) { return string("str"); },
            [](const StructType::List &l) { return "list<" + (l.inner ? l.inner->to_string() : "?") + ">"; },
            [](const StructType::Struct &c) { return c.name; },
            [](const StructType::TraitObject &t) { return "dyn " + t.name; }
          }, v.get_data());
        } else {
          return "{error}";
        }
      },
      data);
}

} // namespace cat::semantics
