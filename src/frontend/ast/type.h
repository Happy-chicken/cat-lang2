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
      uptr<Type> inner;
    };

    struct Ref {
      uptr<Type> inner;
    };

    struct Own {
      uptr<Type> inner;
    };

    struct List {
      uptr<Type> inner;
    };

    struct Func {
      vector<uptr<Type>> params;
      uptr<Type> ret;
    };

    struct Class {
      string name;
    };

    using Variant =
        std::variant<Int, Float, Bool, Char, Str, Void, Ptr, Ref, Own, List, Func, Class>;

    Variant data;

    Type() : data(Int{}) {}

    template<typename T, typename = std::enable_if_t<!std::is_lvalue_reference_v<T>>>
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
              if constexpr (std::is_same_v<T, Ptr> ||
                            std::is_same_v<T, Ref> ||
                            std::is_same_v<T, Own> ||
                            std::is_same_v<T, List>) {
                if (!a.inner && !b.inner) return true;
                if (!a.inner || !b.inner) return false;
                return *a.inner == *b.inner;
              } else if constexpr (std::is_same_v<T, Func>) {
                if (a.params.size() != b.params.size()) return false;

                bool params_equal = std::ranges::equal(
                    a.params, b.params,
                    [](const auto &p1, const auto &p2) {
                      return *p1 == *p2;
                    }
                );
                if (!params_equal) return false;
                if (!a.ret && !b.ret) return true;
                if (!a.ret || !b.ret) return false;
                return *a.ret == *b.ret;
              } else if constexpr (std::is_same_v<T, Class>) {
                return a.name == b.name;
              } else {
                return true;
              }
            }
          },
          data, other.data
      );
    }

    bool operator!=(const Type &other) const { return !(*this == other); }

    Type clone() const {
      return std::visit([](const auto &v) -> Type {
        using T = std::decay_t<decltype(v)>;

        if constexpr (std::is_same_v<T, Int>) {
          return Type(Int{});
        } else if constexpr (std::is_same_v<T, Float>) {
          return Type(Float{});
        } else if constexpr (std::is_same_v<T, Bool>) {
          return Type(Bool{});
        } else if constexpr (std::is_same_v<T, Char>) {
          return Type(Char{});
        } else if constexpr (std::is_same_v<T, Str>) {
          return Type(Str{});
        } else if constexpr (std::is_same_v<T, Void>) {
          return Type(Void{});
        } else if constexpr (std::is_same_v<T, Ptr> ||
                             std::is_same_v<T, Ref> ||
                             std::is_same_v<T, Own> ||
                             std::is_same_v<T, List>) {
          return Type(std::decay_t<decltype(v)>{v.inner ? std::make_unique<Type>(v.inner->clone()) : nullptr});
        } else if constexpr (std::is_same_v<T, Func>) {
          std::vector<uptr<Type>> cloned_params;
          cloned_params.reserve(v.params.size());
          for (const auto &param: v.params) {
            if (param) {
              cloned_params.push_back(std::make_unique<Type>(param->clone()));
            } else {
              cloned_params.push_back(nullptr);
            }
          }

          uptr<Type> cloned_ret = v.ret ? std::make_unique<Type>(v.ret->clone()) : nullptr;

          return Type(Func{std::move(cloned_params), std::move(cloned_ret)});
        } else if constexpr (std::is_same_v<T, Class>) {
          return Type(Class{v.name});
        } else {
          return Type(Int{});
        }
      },
                        data);
    }
    string to_string() const {
      return std::visit([](const auto &v) -> std::string {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, Type::Int>) {
          return "int";
        } else if constexpr (std::is_same_v<T, Type::Float>) {
          return "float";
        } else if constexpr (std::is_same_v<T, Type::Bool>) {
          return "bool";
        } else if constexpr (std::is_same_v<T, Type::Char>) {
          return "char";
        } else if constexpr (std::is_same_v<T, Type::Str>) {
          return "str";
        } else if constexpr (std::is_same_v<T, Type::Void>) {
          return "void";
        } else if constexpr (std::is_same_v<T, Type::Ptr>) {
          return "ptr<" + (v.inner ? v.inner->to_string() : "?") + ">";
        } else if constexpr (std::is_same_v<T, Type::List>) {
          return "list<" + (v.inner ? v.inner->to_string() : "?") + ">";
        } else if constexpr (std::is_same_v<T, Type::Ref>) {
          return "ref<" + (v.inner ? v.inner->to_string() : "?") + ">";
        } else if constexpr (std::is_same_v<T, Type::Own>) {
          return "own<" + (v.inner ? v.inner->to_string() : "?") + ">";
        } else if constexpr (std::is_same_v<T, Type::Func>) {
          std::string result = "(";
          for (size_t i = 0; i < v.params.size(); ++i) {
            if (i > 0) result += ", ";
            result += v.params[i] ? v.params[i]->to_string() : "?";
          }
          result += ") -> ";
          result += v.ret ? v.ret->to_string() : "?";
          return result;
        } else if constexpr (std::is_same_v<T, Type::Class>) {
          return v.name;
        } else {
          return "unknown";
        }
      },
                        data);
    }
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

  inline Type type_ref(Type inner) {
    return Type(Type::Ref{std::make_unique<Type>(std::move(inner))});
  }

  inline Type type_own(Type inner) {
    return Type(Type::Own{std::make_unique<Type>(std::move(inner))});
  }

  inline Type type_list(Type inner) {
    return Type(Type::List{std::make_unique<Type>(std::move(inner))});
  }

  inline Type type_class(std::string name) {
    return Type(Type::Class{std::move(name)});
  }

  inline Type type_func(vector<uptr<Type>> params, uptr<Type> ret) {
    return Type(Type::Func{std::move(params), std::move(ret)});
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

}// namespace cat::ast
