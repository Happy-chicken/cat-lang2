#pragma once
#include <cstdint>
#include <ostream>
namespace cat {
  struct Span {
    using BytePos = std::size_t;
    BytePos low;
    BytePos high;

    Span() : low(0), high(0) {}
    Span(BytePos l, BytePos h) : low(l), high(h) {}
    Span merge(const Span &other) const;
    void print(std::ostream &os) const;
  };
}// namespace cat
