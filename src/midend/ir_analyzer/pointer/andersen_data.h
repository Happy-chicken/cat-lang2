#pragma once
#include <llvm-20/llvm/ADT/DenseMap.h>
#include <llvm-20/llvm/ADT/DenseSet.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/Instructions.h>
#include <llvm-20/llvm/IR/Value.h>
namespace cat::opt::ana {

  using PtsSet = llvm::DenseSet<const llvm::Value *>;

  enum class EdgeKind {
    Simple,       // p1 = p2   (copy: pts(p2) ⊆ pts(p1))
    Base,         // p1 = &p2  (address-of: p2 ∈ pts(p1))
    Complex_RStar,// p1 = *p2  (load: ∀x∈pts(p2), pts(x) ⊆ pts(p1))
    Complex_LStar,// *p1 = p2  (store: ∀x∈pts(p1), pts(p2) ⊆ pts(x))
  };

  struct Edge {
    const llvm::Value *src;
    const llvm::Value *dst;
    EdgeKind kind;
  };

  struct AndersenGraph {
    PtsSet nodes;
    llvm::DenseMap<const llvm::Value *, PtsSet> addr_of;
    llvm::DenseMap<const llvm::Value *, PtsSet> copy_to;
    llvm::DenseMap<const llvm::Value *, PtsSet> load_from;
    llvm::DenseMap<const llvm::Value *, PtsSet> store_to;

    void add_node(const llvm::Value *v) {
      if (v) nodes.insert(v);
    }

    void add_addr_of(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src);
      add_node(dst);
      addr_of[dst].insert(src);
    }

    void add_copy(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src);
      add_node(dst);
      copy_to[src].insert(dst);
    }

    void add_load(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src);
      add_node(dst);
      load_from[dst].insert(src);
    }

    void add_store(const llvm::Value *src, const llvm::Value *dst) {
      add_node(src);
      add_node(dst);
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

    void build(const llvm::Function &func);
  };

}// namespace cat::opt::ana
