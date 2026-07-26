#include "domtree.h"
#include <algorithm>
#include <deque>

namespace cat::opt::ana {

DomTree::DomTree(llvm::Function &F) : func(F) {
  build_cfg(F);
  compute_idoms();
  compute_children();
  compute_dfs_numbers();
}

void DomTree::build_cfg(llvm::Function &F) {
  for (auto &BB : F) {
    bb_to_id[&BB] = static_cast<BlockId>(id_to_bb.size());
    id_to_bb.push_back(&BB);
  }
  cfg.entry = bb_to_id[&F.getEntryBlock()];

  size_t n = id_to_bb.size();
  cfg.blocks.resize(n);
  for (BlockId i = 0; i < n; ++i) {
    cfg.blocks[i].id = i;
    cfg.blocks[i].bb = id_to_bb[i];
    auto *Term = id_to_bb[i]->getTerminator();
    if (!Term)
      continue;
    for (unsigned si = 0; si < Term->getNumSuccessors(); ++si)
      cfg.blocks[i].succ.push_back(bb_to_id[Term->getSuccessor(si)]);
  }

  cfg.compute_predecessors();
}

static BlockId intersect(BlockId finger1, BlockId finger2,
                         const vector<BlockId> &idoms,
                         const vector<BlockId> &po_idx) {
  while (finger1 != finger2) {
    if (po_idx[finger1] < po_idx[finger2])
      finger1 = idoms[finger1];
    else
      finger2 = idoms[finger2];
  }
  return finger1;
}

static vector<BlockId> compute_postorder(const CFG &cfg) {
  auto n = cfg.size();
  vector<bool> visited(n, false);
  vector<BlockId> postorder;
  postorder.reserve(n);
  auto dfs = [&](auto &self, BlockId b) -> void {
    visited[b] = true;
    for (auto s : cfg.successors(b)) {
      if (!visited[s])
        self(self, s);
    }
    postorder.push_back(b);
  };
  dfs(dfs, cfg.entry);
  return postorder;
}

void DomTree::compute_idoms() {
  auto n = cfg.size();

  auto post_order = compute_postorder(cfg);
  vector<BlockId> po_idx(n);
  for (BlockId i = 0; i < n; ++i)
    po_idx[post_order[i]] = i;

  auto rpo = std::move(post_order);
  std::reverse(rpo.begin(), rpo.end());

  idoms.assign(n, kInvalidBlockId);
  idoms[cfg.entry] = cfg.entry;

  bool changed = true;
  while (changed) {
    changed = false;
    for (auto b : rpo) {
      if (b == cfg.entry)
        continue;
      const auto &preds = cfg.predecessors(b);
      BlockId new_idom = kInvalidBlockId;
      for (auto p : preds) {
        if (idoms[p] != kInvalidBlockId) {
          if (new_idom == kInvalidBlockId)
            new_idom = p;
          else
            new_idom = intersect(p, new_idom, idoms, po_idx);
        }
      }
      if (new_idom != kInvalidBlockId && idoms[b] != new_idom) {
        idoms[b] = new_idom;
        changed = true;
      }
    }
  }
}

void DomTree::compute_children() {
  dom_children.resize(idoms.size());
  for (auto &c : dom_children)
    c.clear();
  for (BlockId i = 0; i < static_cast<BlockId>(idoms.size()); ++i) {
    if (idoms[i] != kInvalidBlockId && idoms[i] != i)
      dom_children[idoms[i]].push_back(i);
  }
}

void DomTree::compute_dfs_numbers() {
  size_t n = id_to_bb.size();
  dfs_in.resize(n);
  dfs_out.resize(n);
  BlockId timer = 0;

  std::deque<std::pair<BlockId, size_t>> stack;
  stack.emplace_back(cfg.entry, 0);
  dfs_in[cfg.entry] = ++timer;

  while (!stack.empty()) {
    auto [b, next] = stack.back();
    stack.pop_back();
    const auto &ch = dom_children[b];
    if (next < ch.size()) {
      stack.emplace_back(b, next + 1);
      BlockId child = ch[next];
      dfs_in[child] = ++timer;
      stack.emplace_back(child, 0);
    } else {
      dfs_out[b] = ++timer;
    }
  }
}

BlockId DomTree::id(const llvm::BasicBlock *BB) const {
  auto it = bb_to_id.find(BB);
  return it != bb_to_id.end() ? it->second : kInvalidBlockId;
}

llvm::BasicBlock *DomTree::block(BlockId id) const {
  return id < id_to_bb.size() ? id_to_bb[id] : nullptr;
}

BlockId DomTree::idom(BlockId id) const {
  return id < idoms.size() ? idoms[id] : kInvalidBlockId;
}

bool DomTree::dominates(BlockId a, BlockId b) const {
  return dfs_in[a] <= dfs_in[b] && dfs_out[b] <= dfs_out[a];
}

bool DomTree::dominates(const llvm::Instruction *a,
                        const llvm::Instruction *b) const {
  auto *bb_a = a->getParent();
  auto *bb_b = b->getParent();
  if (bb_a == bb_b) {
    for (auto &Inst : *bb_a) {
      if (&Inst == a)
        return true;
      if (&Inst == b)
        return false;
    }
    return false;
  }
  return dominates(id(bb_a), id(bb_b));
}

const BlockIdList &DomTree::children(BlockId id) const {
  return dom_children[id];
}

const BlockIdList &DomTree::preds(BlockId id) const {
  return cfg.predecessors(id);
}

const BlockIdList &DomTree::succs(BlockId id) const {
  return cfg.successors(id);
}

DomFrontier::DomFrontier(const DomTree &DT) {
  auto n = DT.block_count();
  std::vector<BlockId> idoms_vec(n);
  for (BlockId i = 0; i < n; ++i)
    idoms_vec[i] = DT.idom(i);

  auto df = compute_dominance_frontier(DT.get_cfg(), idoms_vec);
  frontiers.resize(n);
  for (BlockId i = 0; i < n; ++i)
    frontiers[i] = std::move(df[i]);
}

const std::set<BlockId> &DomFrontier::frontier(BlockId id) const {
  return frontiers[id];
}

vector<std::set<BlockId>>
compute_dominance_frontier(const CFG &cfg, const vector<BlockId> &idom) {
  auto n = cfg.size();
  vector<std::set<BlockId>> df(n, std::set<BlockId>());

  for (BlockId b = 0; b < n; ++b) {
    const auto &preds = cfg.predecessors(b);
    if (preds.size() < 2)
      continue;
    for (auto p : preds) {
      BlockId runner = p;
      while (runner != idom[b]) {
        df[runner].insert(b);
        runner = idom[runner];
      }
    }
  }
  return df;
}

std::set<BlockId>
compute_iterated_dominance_frontier(const CFG &cfg,
                                    const vector<BlockId> &idom,
                                    const std::set<BlockId> &initial) {
  auto df = compute_dominance_frontier(cfg, idom);
  std::set<BlockId> result = initial;
  std::vector<BlockId> worklist(initial.begin(), initial.end());

  while (!worklist.empty()) {
    BlockId b = worklist.back();
    worklist.pop_back();
    for (BlockId f : df[b]) {
      if (result.insert(f).second)
        worklist.push_back(f);
    }
  }
  return result;
}

} // namespace cat::opt::ana
