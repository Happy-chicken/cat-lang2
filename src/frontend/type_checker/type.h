#pragma once

#include "common.h"
#include <cstdint>
#include <variant>

namespace cat::semantics {
  using TypedVar = uint32_t;
  enum class PrimType { Int,
                        Float,
                        Bool,
                        Char,
                        Str,
                        Void };

  class Type {
public:
    using ExprId = uint32_t;
    struct Prim {
      PrimType kind;
    };
    struct Var {
      uint32_t id;
    };
    struct List {
      uptr<Type> inner;
    };
    struct Ptr {
      uptr<Type> inner;
    };
    struct Ref {
      uptr<Type> inner;
    };
    struct Own {
      uptr<Type> inner;
    };
    struct Func {
      vector<Type> params;
      uptr<Type> ret;
    };
    struct Class {
      string name;
    };
    struct TraitObject {
      string name;
    };
    struct Error {};

    using Variant =
        std::variant<Prim, Var, List, Ptr, Ref, Own, Func, Class, TraitObject, Error>;

    Type() : data(Error{}) {}

    template<typename T, typename = std::enable_if_t<!std::is_lvalue_reference_v<T>>>
    explicit Type(T &&value) : data(std::forward<T>(value)) {}

    Type(const Type &) = delete;
    Type &operator=(const Type &) = delete;
    Type(Type &&) = default;
    Type &operator=(Type &&) = default;

    Type clone() const;
    bool is_numeric() const;
    bool is_integer() const;
    bool is_bool() const;
    bool is_void() const;
    bool is_error() const;
    string to_string() const;

    static Type prim(PrimType kind) { return Type(Prim{kind}); }
    static Type var(uint32_t id) { return Type(Var{id}); }
    static Type list(Type inner) {
      return Type(List{std::make_unique<Type>(std::move(inner))});
    }
    static Type ptr(Type inner) {
      return Type(Ptr{std::make_unique<Type>(std::move(inner))});
    }
    static Type ref(Type inner) {
      return Type(Ref{std::make_unique<Type>(std::move(inner))});
    }
    static Type own(Type inner) {
      return Type(Own{std::make_unique<Type>(std::move(inner))});
    }
    static Type func(vector<Type> params, Type ret) {
      return Type(
          Func{std::move(params), std::make_unique<Type>(std::move(ret))}
      );
    }
    static Type class_(string name) { return Type(Class{std::move(name)}); }
    static Type trait(string name) { return Type(TraitObject{std::move(name)}); }
    static Type error() { return Type(Error{}); }

    const Variant &get_data() const noexcept { return data; }

private:
    Variant data;
  };

  inline Type type_int() { return Type::prim(PrimType::Int); }
  inline Type type_float() { return Type::prim(PrimType::Float); }
  inline Type type_bool() { return Type::prim(PrimType::Bool); }
  inline Type type_char() { return Type::prim(PrimType::Char); }
  inline Type type_str() { return Type::prim(PrimType::Str); }
  inline Type type_void() { return Type::prim(PrimType::Void); }

}// namespace cat::semantics
