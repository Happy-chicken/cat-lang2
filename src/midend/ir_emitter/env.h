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

    void declare_var(const string &name, llvm::Value *ptr, llvm::Type *ty,
                      vector<llvm::Type *> deref_chain = {}) {
      locals[name] = VarInfo{ptr, ty, std::move(deref_chain)};
    }

    VarInfo lookup_var(const string &name) const {
      auto it = locals.find(name);
      if (it != locals.end()) return it->second;
      if (parent) return parent->lookup_var(name);
      return VarInfo{nullptr, nullptr};
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
