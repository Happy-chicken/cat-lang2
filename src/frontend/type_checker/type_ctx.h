#pragma once

#include "common.h"
#include "type.h"
#include <cstdint>

namespace cat::semantics {

  class TypeCtxt {
public:
    TypeCtxt() : next_var_id(0) {}

    TypeCtxt(const TypeCtxt &) = delete;
    TypeCtxt &operator=(const TypeCtxt &) = delete;
    TypeCtxt(TypeCtxt &&) = default;
    TypeCtxt &operator=(TypeCtxt &&) = default;

    Type fresh_type_var();
    TypedVar fresh_var() { return TypedVar{next_var_id++}; }

    void substitute(TypedVar var, Type ty) { substitutions[var] = std::move(ty); }

    Type resolve_type(const Type &ty) const;

    void record_expr_type(Type::ExprId id, Type ty) {
      expr_types[id] = std::move(ty);
    }

    const Type *get_expr_type(Type::ExprId id) const {
      auto it = expr_types.find(id);
      if (it != expr_types.end()) {
        return &it->second;
      }
      return nullptr;
    }

    const Type *lookup_subst(TypedVar var) const {
      auto it = substitutions.find(var);
      if (it != substitutions.end()) {
        return &it->second;
      }
      return nullptr;
    }

    void record_list_length(Type::ExprId id, size_t len) {
      list_lengths[id] = len;
    }

    optional<size_t> get_list_length(Type::ExprId id) const {
      auto it = list_lengths.find(id);
      if (it != list_lengths.end()) {
        return it->second;
      }
      return std::nullopt;
    }

private:
    uint32_t next_var_id;
    unordered_map<TypedVar, Type> substitutions;
    unordered_map<Type::ExprId, Type> expr_types;
    unordered_map<Type::ExprId, size_t> list_lengths;
  };

}// namespace cat::semantics
