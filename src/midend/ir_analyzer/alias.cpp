#include "alias.h"

namespace cat::opt::ana {

const PtsSet AndersenSolver::empty_set;

AndersenSolver::AndersenSolver(const llvm::Function &func) {
  graph.build(func);
  solve();
}

void AndersenGraph::build(const llvm::Function &func) {
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
        if (!is_ptr_val(&inst))
          continue;
        auto *ptr = load->getPointerOperand();
        auto *base_ptr = trace_to_base(ptr);

        if (base_ptr && has_name(base_ptr) && has_name(&inst)) {
          add_load(base_ptr, &inst);
        }
      }
    }
  }
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
