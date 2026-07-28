#pragma once
#include "common.h"
#include <cstddef>
#include <functional>
#include <llvm-20/llvm/ADT/ArrayRef.h>
#include <llvm-20/llvm/IR/IRBuilder.h>
#include <llvm-20/llvm/IR/LLVMContext.h>
#include <llvm-20/llvm/IR/Module.h>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace llvm {
class Function;
class Type;
class Value;
class StructType;
} // namespace llvm

namespace cat {
namespace error {
class DiagCtxt;
}
namespace semantics {
class Type;
}
struct Span;
} // namespace cat

namespace cat::runtime {

inline constexpr std::string_view LIST_TAG = "list";

// ── IR generation context ──

struct IrGenCtxtRef {
  llvm::IRBuilder<> &builder;
  llvm::Module &module;
  // error::DiagCtxt &diag;

  auto ctx() const -> llvm::LLVMContext & { return builder.getContext(); }

  auto declare_runtime(std::string_view name, llvm::Type *ret,
                       std::vector<llvm::Type *> param_tys,
                       bool var_arg = false) const -> llvm::Function *;
};

// ── callable types ──

using MethodEmitter = std::function<llvm::Value *(
    const IrGenCtxtRef &, llvm::Value *self_ptr, llvm::StructType *,
    llvm::Type *elem_ty, llvm::ArrayRef<llvm::Value *> args, Span span)>;

using FuncEmitter = std::function<llvm::Value *(
    const IrGenCtxtRef &, llvm::ArrayRef<llvm::Value *> args, Span span)>;

using MethodType = std::function<semantics::Type(const semantics::Type &)>;

using FuncType = std::function<semantics::Type()>;

// ── descriptors ──

struct BuiltinMethodDesc {
  std::string name;
  size_t arity;
  MethodType build_func_type;
  MethodEmitter emit;
};

struct BuiltinFuncDesc {
  std::string name;
  size_t arity;
  bool is_variadic = false;
  FuncType build_func_type;
  FuncEmitter emit;
};

// ── registry ──

class BuiltinRegistry {
public:
  void register_type(std::string_view tag,
                     std::vector<BuiltinMethodDesc> methods);

  void register_func(BuiltinFuncDesc desc);

  auto lookup(std::string_view tag, std::string_view method) const
      -> std::optional<std::reference_wrapper<const BuiltinMethodDesc>>;

  auto is_method_declared(std::string_view tag, std::string_view method) const
      -> bool;

  auto lookup_standalone(std::string_view name) const
      -> std::optional<std::reference_wrapper<const BuiltinFuncDesc>>;

  auto is_standalone_declared(std::string_view name) const -> bool;

  void init_defaults();

private:
  struct Key {
    std::string tag;
    std::string method;
    bool operator==(const Key &o) const {
      return tag == o.tag && method == o.method;
    }
  };
  struct KeyHash {
    size_t operator()(const Key &k) const {
      return std::hash<std::string>{}(k.tag + "::" + k.method);
    }
  };
  std::unordered_map<Key, BuiltinMethodDesc, KeyHash> methods_;
  std::unordered_map<std::string, BuiltinFuncDesc> funcs_;
};

} // namespace cat::runtime
