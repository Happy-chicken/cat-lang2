#pragma once
#include <cstdint>
#include <ostream>
#include <string>
namespace cat {
struct Span {
  using BytePos = std::size_t;
  BytePos low;
  BytePos high;

  Span() : low(0), high(0) {}
  Span(BytePos l, BytePos h) : low(l), high(h) {}
  std::string to_string() const {
    return "[" + std::to_string(low) + ", " + std::to_string(high) + "]";
  }
  Span merge(const Span &other) const;
  void print(std::ostream &os) const;
};
} // namespace cat
