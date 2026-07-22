#pragma once
#include "analysis_ctx.h"
#include "dataflow_slover.h"
#include <cstdint>
#include <vector>

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
  vector<uint32_t> compute_postorder(const CFG &cfg) {
    auto n = cfg.size();
    vector<bool> visited(n, false);
    vector<uint32_t> postorder;
    postorder.reserve(n);
    auto dfs = [&](auto &self, uint32_t b) -> void {
      visited[b] = true;
      for (auto s : cfg.blocks[b].succ) {
        if (!visited[s]) self(self, s);
      }
      postorder.push_back(b);
    };
    dfs(dfs, cfg.entry);
    return postorder;
  }

  inline vector<uint32_t> compute_reverse_postorder(const CFG &cfg) {
    auto postorder = compute_postorder(cfg);
    std::reverse(postorder.begin(), postorder.end());
    return postorder;
  }

  uint32_t intersect(uint32_t finger1, uint32_t finger2, const vector<uint32_t> &idoms, const vector<uint32_t> &po_idx) {
    while (finger1 != finger2) {
      if (po_idx[finger1] < po_idx[finger2]) {
        finger1 = idoms[finger1];
      } else {
        finger2 = idoms[finger2];
      }
    }
    return finger1;
  }

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
  vector<uint32_t> compute_idoms_fast(const CFG & cfg) {
    auto n = cfg.size();
    auto entry = cfg.entry;
    auto post_order = compute_postorder(cfg);
    vector<uint32_t> po_idx(n);
    for (uint32_t i = 0; i < n; ++i) {
      po_idx[post_order[i]] = i;
    }

    auto rpo = compute_reverse_postorder(cfg);
    vector<uint32_t> idoms(n, 0xFFFFFFFFu); // 0xFFFFFFFFu means undefined
    idoms[entry] = entry;
    bool changed = true;
    while (changed) {
      changed = false;
      for (auto b : rpo) {
        if (b == entry) continue;
        auto preds = cfg.predecessors(b);
        uint32_t new_idom = 0xFFFFFFFFu;
        for (auto p : preds) {
          if (idoms[p] != 0xFFFFFFFFu) {
            if (new_idom == 0xFFFFFFFFu) {
              new_idom = p;
            } else {
              new_idom = intersect(p, new_idom, idoms, po_idx);
            }
          }
        }
        if (new_idom != 0xFFFFFFFFu && idoms[b] != new_idom) {
          idoms[b] = new_idom;
          changed = true;
        }
      }
    }
    return idoms;
  }

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

  vector<vector<uint32_t>> compute_dom_tree_children(const vector<uint32_t> &idoms) {
    auto n = idoms.size();
    vector<vector<uint32_t>> dom_tree_children(n, vector<uint32_t>());
    for (uint32_t i = 0; i < n; ++i) {
      if (idoms[i] != 0xFFFFFFFFu && idoms[i] != i) {
        dom_tree_children[idoms[i]].push_back(i);
      }
    }
    return dom_tree_children;
  }

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
                                                        const std::set<uint32_t> &initial) {
    auto df = compute_dominance_frontier(cfg, idom);
    std::set<uint32_t> result = initial;
    std::vector<uint32_t> worklist(initial.begin(), initial.end());

    while (!worklist.empty()) {
        uint32_t b = worklist.back();
        worklist.pop_back();
        
        for (uint32_t f : df[b]) {
            if (result.insert(f).second) {
                worklist.push_back(f);
            }
        }
    }
    return result;
  }

} // namespace cat::opt::ana
