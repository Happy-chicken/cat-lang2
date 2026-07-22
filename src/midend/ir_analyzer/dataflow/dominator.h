#pragma once
#include "analysis_ctx.h"
#include "dataflow_slover.h"
#include <cstdint>
#include <vector>
#include <set>

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

  inline vector<DomState> compute_dominators(const CFG &cfg) {
    DominatorAnalysis dom_analysis(cfg);
    DataflowSolver<DominatorAnalysis> solver(cfg, dom_analysis);
    return solver.solve();
  }

  // opt
  vector<uint32_t> compute_postorder(const CFG &cfg);

  inline vector<uint32_t> compute_reverse_postorder(const CFG &cfg) {
    auto postorder = compute_postorder(cfg);
    std::reverse(postorder.begin(), postorder.end());
    return postorder;
  }

  uint32_t intersect(uint32_t finger1, uint32_t finger2, const vector<uint32_t> &idoms, const vector<uint32_t> &po_idx);

  /// compute immediate dominator (IDom) for each node
  /// 
  /// Immediate dominator definition: the closest dominator in the dominator tree
  /// that is not the node itself
  /// 
  /// Properties:
  /// - Each non-entry node has exactly one immediate dominator
  /// - Immediate dominator relationship forms a tree (dominator tree)
  /// 
  /// In the set of strict dominators, find the node that dominates all other strict dominators
  /// That is: idom(n) = the "largest" element in the set of strict dominators
  /// 快速计算直接支配者（基于 RPO + intersect）
  vector<uint32_t> compute_idoms_fast(const CFG & cfg);

  // build dominator from immediate dominator
  vector<std::set<uint32_t>> compute_dominators_fast(const CFG &cfg, const vector<uint32_t> &idoms) {
    auto n = cfg.size();
    vector<std::set<uint32_t>> dominators(n);

    for (uint32_t i = 0; i < n; ++i) {
      if (i == cfg.entry || idoms[i] == 0xFFFFFFFFu) {
        continue;
      }
      dominators[i].insert(i);
      auto current = idoms[i];
      while (current != cfg.entry ) {
        if (current == 0xFFFFFFFFu) {
          break; // should not happen, but just in case
        }
        dominators[i].insert(current);
        current = idoms[current];
      }
    }
    return dominators;
  }

  vector<vector<uint32_t>> compute_dom_tree_children(const vector<uint32_t> &idoms);

  inline vector<std::set<uint32_t>> compute_dominance_frontier(const CFG &cfg, const vector<uint32_t> &idom) {
    auto n = cfg.size();
    vector<std::set<uint32_t>> df(n, std::set<uint32_t>());

    for (uint32_t b = 0; b < n; ++b) {
        auto preds = cfg.predecessors(b);
        if (preds.size() < 2) continue;  

        for (auto p : preds) {
            uint32_t runner = p;
            while (runner != idom[b]) {
                df[runner].insert(b);
                runner = idom[runner];
            }
        }
    }
    return df;
  }

  std::set<uint32_t> compute_iterated_dominance_frontier(const CFG &cfg, 
                                                        const vector<uint32_t> &idom,
                                                        const std::set<uint32_t> &initial);

} // namespace cat::opt::ana
