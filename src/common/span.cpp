#include "span.h"

namespace cat {
    Span Span::merge(const Span& other) const {
        return Span(std::min(start, other.start), std::max(end, other.end));
    }

    void Span::print(std::ostream& os) const {
        os << "Span(" << start << ", " << end << ")";
    }
}