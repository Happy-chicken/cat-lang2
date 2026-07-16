#include "span.h"

namespace cat {
Span Span::merge(const Span &other) const {
  return Span(std::min(low, other.low), std::max(high, other.high));
}

void Span::print(std::ostream &os) const {
  os << "Span(" << low << ", " << high << ")";
}
} // namespace cat