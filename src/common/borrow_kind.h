#pragma once
#include <cstdint>
namespace cat {
enum class BorrowKind : uint8_t { None, Ref, CRef, Own };
} // namespace cat
