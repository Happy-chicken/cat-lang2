#pragma once

#include "common.h"
#include <cstdint>
#include <variant>

namespace cat::semantics {
using TypedVar = uint32_t;
enum class PrimType { Int, Float, Bool, Char, Void };

class Type {
public:
  using ExprId = uint32_t;
  struct Prim {
    PrimType kind;
  };
  struct Var {
    uint32_t id;
  };
  struct Ptr {
    uptr<Type> inner;
  };
  struct Ref {
    uptr<Type> inner;
  };
  struct CRef {
    uptr<Type> inner;
  };
  struct Own {
    uptr<Type> inner;
  };
  struct Func {
    vector<uptr<Type>> params;
    uptr<Type> ret;
  };
  struct Error {};
  class StructType { 
    public:
    struct Str {
      uint32_t length;
    }; 
    struct List {
      uptr<Type> inner;
    }; 
      struct Class {
      string name;
    };
    struct TraitObject {
      string name;
    };
    using Variant = std::variant<Str, List, Class, TraitObject>;
    explicit StructType(Str s) : data(std::move(s)) {}
    explicit StructType(List l) : data(std::move(l)) {}
    explicit StructType(Class c) : data(std::move(c)) {}
    explicit StructType(TraitObject t) : data(std::move(t)) {}
    StructType() = delete;
    StructType(const StructType &) = delete;
    StructType &operator=(const StructType &) = delete;
    StructType(StructType &&) = default;
    StructType &operator=(StructType &&) = default;
    const Variant &get_data() const noexcept { return data; }
    private:
      Variant data;
  };

  using Variant = std::variant<Prim, Var, Ptr, Ref, CRef, Own, Func, StructType, Error>;

  Type() : data(Error{}) {}

  template <typename T,
            typename = std::enable_if_t<!std::is_lvalue_reference_v<T>>>
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
  static Type str(uint32_t length) { return Type(StructType(StructType::Str{length})); }
  static Type list(Type inner) {
    return Type(StructType(StructType::List{std::make_unique<Type>(std::move(inner))}));
  }
  static Type ptr(Type inner) {
    return Type(Ptr{std::make_unique<Type>(std::move(inner))});
  }
  static Type ref(Type inner) {
    return Type(Ref{std::make_unique<Type>(std::move(inner))});
  }
  static Type cref(Type inner) {
    return Type(CRef{std::make_unique<Type>(std::move(inner))});
  }
  static Type own(Type inner) {
    return Type(Own{std::make_unique<Type>(std::move(inner))});
  }
  static Type func(vector<uptr<Type>> params, Type ret) {
    return Type(
        Func{std::move(params), std::make_unique<Type>(std::move(ret))});
  }
  static Type type_str(uint32_t length) { return Type(StructType(StructType::Str{length})); }
  static Type class_(string name) { return Type(StructType(StructType::Class{std::move(name)})); }
  static Type trait(string name) { return Type(StructType(StructType::TraitObject{std::move(name)})); }
  static Type error() { return Type(Error{}); }

  const Variant &get_data() const noexcept { return data; }

private:
  Variant data;
};

inline Type type_int() { return Type::prim(PrimType::Int); }
inline Type type_float() { return Type::prim(PrimType::Float); }
inline Type type_bool() { return Type::prim(PrimType::Bool); }
inline Type type_char() { return Type::prim(PrimType::Char); }
inline Type type_void() { return Type::prim(PrimType::Void); }

} // namespace cat::semantics
