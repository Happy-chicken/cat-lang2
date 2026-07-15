#pragma once
#include "common.h"
namespace cat {
    struct Span {
        using BytePos = size_t;
        BytePos start;
        BytePos end;

        Span(BytePos s, BytePos e) : start(s), end(e) {}
        Span merge(const Span& other) const;
        void print(std::ostream& os) const;
    };
}