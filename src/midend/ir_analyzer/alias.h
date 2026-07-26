#pragma once
#include "common.h"
#include <llvm-20/llvm/ADT/DenseMap.h>
#include <llvm-20/llvm/ADT/DenseSet.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/Instructions.h>
#include <llvm-20/llvm/IR/Value.h>
#include <llvm-20/llvm/Support/raw_ostream.h>
#include <deque>

namespace cat::opt::ana {

using PtsSet = llvm::DenseSet<const llvm::Value *>;

enum class EdgeKind {
  Simple,
  Base,
  Complex_RStar,
  Complex_LStar,
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
    if (v)
      nodes.insert(v);
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
      if (llvm::isa<llvm::AllocaInst>(v))
        return v;
      if (llvm::isa<llvm::Argument>(v))
        return v;
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

class AndersenSolver {
public:
  using PtsMap = llvm::DenseMap<const llvm::Value *, PtsSet>;

  explicit AndersenSolver(const llvm::Function &func);

  const PtsSet &points_to(const llvm::Value *v) const {
    auto *base = AndersenGraph::trace_to_base(v);
    if (!base)
      base = v;
    auto it = pts.find(base);
    return it != pts.end() ? it->second : empty_set;
  }

  const PtsMap &get_all_pts() const { return pts; }

  void dump(llvm::raw_ostream &os) const {
    os << "    " << pts.size() << " nodes, ";
    size_t nonempty = 0;
    for (auto &[_, s] : pts)
      if (!s.empty())
        ++nonempty;
    os << nonempty << " with points-to\n";
    for (auto &[v, s] : pts) {
      if (s.empty())
        continue;
      os << "    " << v->getName() << " -> {";
      bool first = true;
      for (auto *p : s) {
        if (!first)
          os << ", ";
        os << p->getName();
        first = false;
      }
      os << "}\n";
    }
  }

private:
  void solve();

  void propagate(const llvm::Value *src, const llvm::Value *dst,
                 std::deque<const llvm::Value *> &worklist) {
    auto it_src = pts.find(src);
    auto it_dst = pts.find(dst);
    if (it_src == pts.end() || it_dst == pts.end() || it_src->second.empty())
      return;

    auto &s = it_src->second;
    auto &d = it_dst->second;
    size_t old_sz = d.size();
    d.insert(s.begin(), s.end());
    if (d.size() > old_sz)
      push(worklist, dst);
  }

  void push(std::deque<const llvm::Value *> &q, const llvm::Value *v) {
    for (auto x : q)
      if (x == v)
        return;
    q.push_back(v);
  }

  AndersenGraph graph;
  PtsMap pts;
  static const PtsSet empty_set;
};

inline uptr<AndersenSolver> compute_andersen(const llvm::Function &func) {
  return std::make_unique<AndersenSolver>(func);
}

} // namespace cat::opt::ana
