#pragma once
#include "common.h"
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/Instructions.h>
#include <llvm-20/llvm/IR/Value.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace cat::opt::ana {

  struct ConstValuePtrHash {
    size_t operator()(const llvm::Value *v) const noexcept {
      return reinterpret_cast<size_t>(v);
    }
  };

  using PtsSet = std::unordered_set<const llvm::Value *, ConstValuePtrHash>;

  enum class EdgeKind {
    Simple,         // p1 = p2   (copy: pts(p2) ⊆ pts(p1))
    Base,           // p1 = &p2  (address-of: p2 ∈ pts(p1))
    Complex_RStar,  // p1 = *p2  (load: ∀x∈pts(p2), pts(x) ⊆ pts(p1))
    Complex_LStar,  // *p1 = p2  (store: ∀x∈pts(p1), pts(p2) ⊆ pts(x))
  };

  struct Edge {
    const llvm::Value *src;
    const llvm::Value *dst;
    EdgeKind kind;
  };

  struct AndersenGraph {
    PtsSet nodes;
    std::unordered_map<const llvm::Value *, PtsSet, ConstValuePtrHash> addr_of;
    std::unordered_map<const llvm::Value *, PtsSet, ConstValuePtrHash> copy_to;
    std::unordered_map<const llvm::Value *, PtsSet, ConstValuePtrHash> load_from;
    std::unordered_map<const llvm::Value *, PtsSet, ConstValuePtrHash> store_to;

    void add_node(const llvm::Value *v) { if (v) nodes.insert(v); }

    void add_addr_of(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src); add_node(dst);
      addr_of[dst].insert(src);
    }

    void add_copy(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src); add_node(dst);
      copy_to[src].insert(dst);
    }

    void add_load(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src); add_node(dst);
      load_from[dst].insert(src);
    }

    void add_store(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src); add_node(dst);
      store_to[dst].insert(src);
    }

    static const llvm::Value *trace_to_base(const llvm::Value *v) {
      while (v) {
        if (llvm::isa<llvm::AllocaInst>(v)) return v;
        if (llvm::isa<llvm::Argument>(v)) return v;
        if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(v)) {
          v = gep->getPointerOperand();
        } else if (auto *cast = llvm::dyn_cast<llvm::CastInst>(v)) {
          v = cast->getOperand(0);
        } else {
          break;
        }
      }
      return nullptr;
    }

    static bool is_ptr_val(const llvm::Value *v) {
      return v && v->getType()->isPointerTy();
    }

    static bool has_name(const llvm::Value *v) {
      return v && v->hasName() && !v->getName().empty();
    }

    void build(const llvm::Function &func) {
      for (const auto &bb : func) {
        for (const auto &inst : bb) {
          if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
            if (has_name(alloca)) {
              add_node(alloca);
            }
          }

          if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
            auto *val = store->getValueOperand();
            auto *ptr = store->getPointerOperand();
            auto *base_ptr = trace_to_base(ptr);
            auto *base_val = trace_to_base(val);

            if (base_ptr && base_val && has_name(base_ptr) && has_name(base_val)) {
              if (is_ptr_val(val)) {
                add_copy(val, base_ptr);
                add_addr_of(base_val, base_ptr);
              } else {
                add_addr_of(val, base_ptr);
              }
            }
          }

          if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
            if (!is_ptr_val(&inst)) continue;
            auto *ptr = load->getPointerOperand();
            auto *base_ptr = trace_to_base(ptr);

            if (base_ptr && has_name(base_ptr) && has_name(&inst)) {
              add_load(base_ptr, &inst);
            }
          }
        }
      }
    }
  };

} // namespace cat::opt::ana
