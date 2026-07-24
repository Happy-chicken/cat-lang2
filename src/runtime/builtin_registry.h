#pragma once
#include "common.h"
#include <functional>
#include <llvm-20/llvm/IR/IRBuilder.h>
#include <llvm-20/llvm/IR/LLVMContext.h>
#include <llvm-20/llvm/IR/Module.h>
#include <optional>
#include <string>
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

struct IrGenParams {
  llvm::IRBuilder<> &builder;
  llvm::Module &module;
  llvm::LLVMContext &llvm_ctx;
  std::function<llvm::Function *(const std::string &, llvm::Type *,
                                 std::vector<llvm::Type *>, bool)>
      declare_runtime;
  error::DiagCtxt &diag;
};

using IrGenFunc = std::function<llvm::Value *(
    const IrGenParams &p, llvm::Value *self_alloca, llvm::StructType *st,
    llvm::Type *elem_ty, const std::vector<llvm::Value *> &args, Span span)>;

using FuncTypeBuilder =
    std::function<semantics::Type(const semantics::Type &elem_ty)>;

struct BuiltinMethodDesc {
  std::string name;
  size_t arity;
  FuncTypeBuilder build_func_type;
  IrGenFunc ir_generate;
};

class BuiltinRegistry {
public:
  void register_type(const std::string &tag,
                     std::vector<BuiltinMethodDesc> methods);

  std::optional<std::reference_wrapper<const BuiltinMethodDesc>>
  lookup(const std::string &tag, const std::string &method) const;

  bool is_method_declared(const std::string &tag,
                          const std::string &method) const;

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
};

} // namespace cat::runtime
