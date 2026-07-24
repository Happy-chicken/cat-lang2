#pragma once
#include "common.h"
#include "mlir/IR/BuiltinTypes.h"
#include "mlir/IR/Value.h"
#include "llvm/ADT/StringMap.h"

namespace cat::mmlir {

struct VarInfo {
  mlir::Value memref;
  mlir::Type elem_type;
};

struct LoopInfo {
  mlir::Block *cond_block;
  mlir::Block *exit_block;
};

struct Env {
  sptr<Env> parent;
  llvm::StringMap<VarInfo> locals;
  optional<LoopInfo> loop_info;

  Env() = default;
  explicit Env(sptr<Env> p) : parent(std::move(p)) {}

  void declare_var(const string &name, mlir::Value memref,
                   mlir::Type elem_type) {
    locals[name] = VarInfo{memref, elem_type};
  }

  VarInfo lookup_var(const string &name) const {
    auto it = locals.find(name);
    if (it != locals.end())
      return it->second;
    if (parent)
      return parent->lookup_var(name);
    return VarInfo{};
  }

  bool has_var(const string &name) const {
    if (locals.find(name) != locals.end())
      return true;
    if (parent)
      return parent->has_var(name);
    return false;
  }

  void set_loop(LoopInfo li) { loop_info = li; }

  optional<LoopInfo> lookup_loop() const {
    if (loop_info)
      return loop_info;
    if (parent)
      return parent->lookup_loop();
    return std::nullopt;
  }
};

} // namespace cat::mmlir
