#pragma once
#include "../frontend/type_checker/type.h"
#include "common.h"
namespace cat::error {
template <typename T> using ParseResult = optional<T>;

struct ParseError {};

struct Mismatch {
  semantics::Type expected;
  semantics::Type found;
};

struct InfiniteType {
  semantics::Type type;
  semantics::TypedVar var;
};

using UnifyError = std::variant<Mismatch, InfiniteType>;

template <typename T> using UnifyResult = std::variant<T, UnifyError>;

} // namespace cat::error
