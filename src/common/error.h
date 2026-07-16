#pragma once
#include "common.h"
namespace cat::error {
  template<typename T>
  using ParseResult = optional<T>;

  struct ParseError {};
}// namespace cat::error
