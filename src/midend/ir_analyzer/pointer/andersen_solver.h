#pragma once
#include "andersen_data.h"
#include "common.h"
#include <deque>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/Support/raw_ostream.h>

namespace cat::opt::ana {

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
