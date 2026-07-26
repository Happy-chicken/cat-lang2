#pragma once
#include "cfg.h"
#include <llvm/ADT/DenseMap.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <cstdint>
#include <set>
#include <vector>

namespace cat::opt::ana {

vector<std::set<BlockId>>
compute_dominance_frontier(const CFG &cfg, const vector<BlockId> &idom);

std::set<BlockId>
compute_iterated_dominance_frontier(const CFG &cfg,
                                    const vector<BlockId> &idom,
                                    const std::set<BlockId> &initial);

class DomTree {
public:
  explicit DomTree(llvm::Function &F);

  BlockId entry_id() const { return cfg.entry; }
  uint32_t block_count() const { return static_cast<uint32_t>(id_to_bb.size()); }

  BlockId id(const llvm::BasicBlock *BB) const;
  llvm::BasicBlock *block(BlockId id) const;

  BlockId idom(BlockId id) const;

  bool dominates(BlockId a, BlockId b) const;

  bool dominates(const llvm::Instruction *a, const llvm::Instruction *b) const;

  const BlockIdList &children(BlockId id) const;

  const BlockIdList &preds(BlockId id) const;

  const BlockIdList &succs(BlockId id) const;

  const CFG &get_cfg() const { return cfg; }

private:
  void build_cfg(llvm::Function &F);
  void compute_idoms();
  void compute_children();
  void compute_dfs_numbers();

  llvm::Function &func;
  CFG cfg;

  std::vector<llvm::BasicBlock *> id_to_bb;
  llvm::DenseMap<const llvm::BasicBlock *, BlockId> bb_to_id;

  std::vector<BlockId> idoms;
  std::vector<BlockIdList> dom_children;
  std::vector<BlockId> dfs_in;
  std::vector<BlockId> dfs_out;
};

class DomFrontier {
public:
  explicit DomFrontier(const DomTree &DT);

  const std::set<BlockId> &frontier(BlockId id) const;

private:
  std::vector<std::set<BlockId>> frontiers;
};

} // namespace cat::opt::ana
