#pragma once
#include "common.h"
namespace cat {
struct Span {
  using BytePos = size_t;
  BytePos low;
  BytePos high;

  Span(BytePos l, BytePos h) : low(l), high(h) {}
  Span merge(const Span &other) const;
  void print(std::ostream &os) const;
};
} // namespace cat