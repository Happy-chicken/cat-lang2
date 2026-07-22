#pragma once
#include "andersen_data.h"
#include <deque>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/Support/raw_ostream.h>

namespace cat::opt::ana {

  class AndersenSolver {
public:
    using PtsMap = std::unordered_map<const llvm::Value *, PtsSet, ConstValuePtrHash>;

    explicit AndersenSolver(const llvm::Function &func) {
      graph.build(func);
      solve();
    }

    const PtsSet &points_to(const llvm::Value *v) const {
      auto *base = AndersenGraph::trace_to_base(v);
      if (!base) base = v;
      auto it = pts.find(base);
      return it != pts.end() ? it->second : empty_set;
    }

    const PtsMap &get_all_pts() const { return pts; }

    void dump(llvm::raw_ostream &os) const {
      os << "    " << pts.size() << " nodes, ";
      size_t nonempty = 0;
      for (auto &[_, s] : pts) if (!s.empty()) ++nonempty;
      os << nonempty << " with points-to\n";
      for (auto &[v, s] : pts) {
        if (s.empty()) continue;
        os << "    " << v->getName() << " -> {";
        bool first = true;
        for (auto *p : s) {
          if (!first) os << ", ";
          os << p->getName();
          first = false;
        }
        os << "}\n";
      }
    }

private:
    void solve() {
      for (auto *v : graph.nodes) pts[v];

      std::deque<const llvm::Value *> worklist;

      for (auto &[dst, srcs] : graph.addr_of) {
        auto &s = pts[dst];
        for (auto *src : srcs) {
          if (s.insert(src).second)
            push(worklist, dst);
        }
      }

      while (!worklist.empty()) {
        auto *v = worklist.front();
        worklist.pop_front();

        {
          auto it = graph.copy_to.find(v);
          if (it != graph.copy_to.end()) {
            for (auto *dst : it->second)
              propagate(v, dst, worklist);
          }
        }

        {
          auto it = graph.load_from.find(v);
          if (it != graph.load_from.end()) {
            auto &pv = pts[v];
            for (auto *dst : it->second) {
              for (auto *x : pv)
                propagate(x, dst, worklist);
            }
          }
        }

        {
          auto it = graph.store_to.find(v);
          if (it != graph.store_to.end()) {
            auto &pv = pts[v];
            for (auto *src : it->second) {
              for (auto *x : pv)
                propagate(src, x, worklist);
            }
          }
        }
      }
    }

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
      for (auto x : q) if (x == v) return;
      q.push_back(v);
    }

    AndersenGraph graph;
    PtsMap pts;
    static const PtsSet empty_set;
  };

  const PtsSet AndersenSolver::empty_set;

  inline uptr<AndersenSolver> compute_andersen(const llvm::Function &func) {
    return std::make_unique<AndersenSolver>(func);
  }

} // namespace cat::opt::ana
