#include "dominator.h"
#include "analysis_ctx.h"
namespace cat::opt::ana {
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
}