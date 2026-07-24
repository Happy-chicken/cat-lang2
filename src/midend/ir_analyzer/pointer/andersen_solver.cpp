#include "andersen_solver.h"

namespace cat::opt::ana {

const PtsSet AndersenSolver::empty_set;

AndersenSolver::AndersenSolver(const llvm::Function &func) {
  graph.build(func);
  solve();
}

void AndersenSolver::solve() {
  for (auto *v : graph.nodes)
    pts[v];

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

} // namespace cat::opt::ana
