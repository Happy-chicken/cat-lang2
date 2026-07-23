#pragma once
#include "codegen_ctx.h"
#include <llvm-20/llvm/ADT/StringMap.h>
namespace cat::ir {

  struct Env {
    sptr<Env> parent;
    llvm::StringMap<VarInfo> locals;
    optional<LoopInfo> loop_info;

    Env() = default;
    explicit Env(sptr<Env> p) : parent(std::move(p)) {}

    void declare_var(const string &name, llvm::Value *ptr,
                      llvm::Type *alloca_ty, llvm::Type *value_ty,
                      bool is_ref = false,
                      vector<llvm::Type *> deref_chain = {},
                      bool is_own = false) {
      locals[name] = VarInfo{ptr, alloca_ty, value_ty, is_ref, is_own, std::move(deref_chain)};
    }

    VarInfo lookup_var(const string &name) const {
      auto it = locals.find(name);
      if (it != locals.end()) return it->second;
      if (parent) return parent->lookup_var(name);
      return VarInfo{nullptr, nullptr, nullptr};
    }

    bool has_var(const string &name) const {
      if (locals.find(name) != locals.end()) return true;
      if (parent) return parent->has_var(name);
      return false;
    }

    void set_loop(LoopInfo li) { loop_info = li; }

    optional<LoopInfo> lookup_loop() const {
      if (loop_info) return loop_info;
      if (parent) return parent->lookup_loop();
      return std::nullopt;
    }
  };

}// namespace cat::ir
