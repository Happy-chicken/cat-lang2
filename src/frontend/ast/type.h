#pragma once

#include "../common/common.h"
#include <functional>
#include <type_traits>
#include <utility>
#include <variant>

namespace cat::ast {

struct Type {

  struct Int {};
  struct Float {};
  struct Bool {};
  struct Char {};
  struct Str {};
  struct Void {};

  struct Ptr {
    std::unique_ptr<Type> inner;
  };

  struct List {
    std::unique_ptr<Type> inner;
  };

  struct Class {
    std::string name;
  };

  using Variant =
      std::variant<Int, Float, Bool, Char, Str, Void, Ptr, List, Class>;

  Variant data;

  Type() : data(Int{}) {}

  template <typename T,
            typename = std::enable_if_t<!std::is_lvalue_reference_v<T>>>
  explicit Type(T &&value) : data(std::forward<T>(value)) {}

  Type(const Type &) = delete;
  Type &operator=(const Type &) = delete;
  Type(Type &&) = default;
  Type &operator=(Type &&) = default;

  bool operator==(const Type &other) const {
    return std::visit(
        [](const auto &a, const auto &b) -> bool {
          using T = std::decay_t<decltype(a)>;
          using U = std::decay_t<decltype(b)>;
          if constexpr (!std::is_same_v<T, U>) {
            return false;
          } else {
            if constexpr (std::is_same_v<T, Ptr>) {
              if (!a.inner && !b.inner)
                return true;
              if (!a.inner || !b.inner)
                return false;
              return *a.inner == *b.inner;
            } else if constexpr (std::is_same_v<T, List>) {
              if (!a.inner && !b.inner)
                return true;
              if (!a.inner || !b.inner)
                return false;
              return *a.inner == *b.inner;
            } else if constexpr (std::is_same_v<T, Class>) {
              return a.name == b.name;
            } else {
              return true;
            }
          }
        },
        data, other.data);
  }

  bool operator!=(const Type &other) const { return !(*this == other); }
};

inline Type type_int() { return Type(Type::Int{}); }
inline Type type_float() { return Type(Type::Float{}); }
inline Type type_bool() { return Type(Type::Bool{}); }
inline Type type_char() { return Type(Type::Char{}); }
inline Type type_str() { return Type(Type::Str{}); }
inline Type type_void() { return Type(Type::Void{}); }

inline Type type_ptr(Type inner) {
  return Type(Type::Ptr{std::make_unique<Type>(std::move(inner))});
}

inline Type type_list(Type inner) {
  return Type(Type::List{std::make_unique<Type>(std::move(inner))});
}

inline Type type_class(std::string name) {
  return Type(Type::Class{std::move(name)});
}

// ----- std::hash 特化（使用 std::visit 实现）-----
// namespace std {
// template <> struct hash<cat::Type> {
//   size_t operator()(const cat::Type &t) const {
//     // 使用索引作为种子，保证不同变体的哈希不同
//     size_t seed = t.data.index();
//     std::visit(
//         [&seed](const auto &v) {
//           using T = std::decay_t<decltype(v)>;
//           if constexpr (std::is_same_v<T, cat::Type::Ptr>) {
//             if (v.inner) {
//               seed ^= std::hash<cat::Type>{}(*v.inner) + 0x9e3779b9 +
//                       (seed << 6) + (seed >> 2);
//             }
//           } else if constexpr (std::is_same_v<T, cat::Type::List>) {
//             if (v.inner) {
//               seed ^= std::hash<cat::Type>{}(*v.inner) + 0x9e3779b9 +
//                       (seed << 6) + (seed >> 2);
//             }
//           } else if constexpr (std::is_same_v<T, cat::Type::Class>) {
//             seed ^= std::hash<std::string>{}(v.name) + 0x9e3779b9 +
//                     (seed << 6) + (seed >> 2);
//           }
//           // 其他空类型不改变哈希（仅依赖索引）
//         },
//         t.data);
//     return seed;
//   }
// };
// } // namespace std

} // namespace cat::ast