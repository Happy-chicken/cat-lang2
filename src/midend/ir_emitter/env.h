#pragma once
#include "codegen_ctx.h"
#include "cleanup.h"

namespace cat::ir {

struct Env {
  sptr<Env> parent;
  llvm::StringMap<VarInfo> locals;
  optional<LoopInfo> loop_info;
  vector<CleanupInfo> cleanups;

  Env() = default;
  explicit Env(sptr<Env> p) : parent(std::move(p)) {}

  void declare_var(const string &name, llvm::Value *ptr, llvm::Type *alloca_ty,
                   llvm::Type *value_ty, BorrowKind borrow_kind = BorrowKind::None,
                   vector<llvm::Type *> deref_chain = {},
                   llvm::FunctionType *func_ty = nullptr) {
    locals[name] = VarInfo{ptr, alloca_ty, value_ty, borrow_kind,
                           std::move(deref_chain), func_ty};
  }

  VarInfo lookup_var(const string &name) const {
    auto it = locals.find(name);
    if (it != locals.end())
      return it->second;
    if (parent)
      return parent->lookup_var(name);
    return VarInfo{nullptr, nullptr, nullptr};
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

  void add_cleanup(llvm::Value *a, llvm::Type *aty, bool is_class,
                   llvm::StructType *list_st = nullptr,
                   bool free_base_ptr = false) {
    cleanups.push_back({a, aty, is_class, list_st, false, free_base_ptr});
  }

  bool cancel_cleanup(llvm::Value *a) {
    for (auto &c : cleanups)
      if (c.alloca == a) {
        c.cancelled = true;
        return true;
      }
    if (parent)
      return parent->cancel_cleanup(a);
    return false;
  }
};

} // namespace cat::ir
