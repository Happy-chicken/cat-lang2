#pragma once
#include "analysis_ctx.h"
#include "dataflow_slover.h"

namespace cat::opt::ana {

  struct DomState {
    std::set<uint32_t> doms;
    bool operator==(const DomState &o) const { return doms == o.doms; }
    bool operator!=(const DomState &o) const { return !(*this == o); }
  };

  class DominatorAnalysis {
  public:
    using State = DomState;

    explicit DominatorAnalysis(const CFG &cfg) : cfg(cfg) {}

    Direction direction() const { return Direction::Forward; }

    State initial_state() const {
      State s;
      for (uint32_t i = 0; i < cfg.size(); ++i) s.doms.insert(i);
      return s;
    }

    State boundary_state() const {
      State s;
      s.doms.insert(cfg.entry);
      return s;
    }

    State transfer(const BlockInfo &block, const State &input) const {
      State out = input;
      out.doms.insert(block.id);
      return out;
    }

    State meet(const vector<State> &states) const {
      if (states.empty()) return initial_state();
      State result = states[0];
      for (size_t i = 1; i < states.size(); ++i) {
        State inter;
        for (auto d : result.doms)
          if (states[i].doms.count(d)) inter.doms.insert(d);
        result = std::move(inter);
      }
      return result;
    }

  private:
    const CFG &cfg;
  };

  inline vector<uint32_t> compute_dom_tree_parents(const CFG &cfg) {
    DominatorAnalysis dom_analysis(cfg);
    DataflowSolver<DominatorAnalysis> solver(cfg, dom_analysis);
    auto dom_states = solver.solve();

    auto n = cfg.size();
    vector<uint32_t> idom(n, 0xFFFFFFFFu);

    for (uint32_t i = 0; i < n; ++i) {
      auto &ds = dom_states[i].doms;
      for (auto d : ds) {
        if (d == i) continue;
        bool is_immediate = true;
        for (auto other : ds) {
          if (other == i || other == d) continue;
          if (dom_states[other].doms.count(d) && dom_states[i].doms.count(other)) {
            is_immediate = false;
            break;
          }
        }
        if (is_immediate) { idom[i] = d; break; }
      }
    }
    idom[cfg.entry] = cfg.entry;
    return idom;
  }

  inline vector<std::set<uint32_t>> compute_dominance_frontier(const CFG &cfg,
                                                                const vector<uint32_t> &idom) {
    auto n = cfg.size();
    DominatorAnalysis dom_a(cfg);
    DataflowSolver<DominatorAnalysis> solver(cfg, dom_a);
    auto ds = solver.solve();
    vector<std::set<uint32_t>> full_dom(n);
    for (uint32_t i = 0; i < n; ++i) full_dom[i] = ds[i].doms;

    vector<std::set<uint32_t>> df(n);
    for (uint32_t b = 0; b < n; ++b) {
      auto preds = cfg.predecessors(b);
      if (preds.size() < 2) continue;
      for (auto p : preds) {
        auto runner = p;
        while (!full_dom[b].count(runner) && runner != cfg.entry) {
          df[runner].insert(b);
          runner = idom[runner];
        }
      }
    }
    return df;
  }

} // namespace cat::opt::ana
