#pragma once
#include "common.h"
#include <llvm-20/llvm/ADT/DenseSet.h>
#include <llvm-20/llvm/IR/BasicBlock.h>

namespace cat::opt::ana {

using ValueSet = llvm::DenseSet<const llvm::Value *>;
using BlockId = uint32_t;
using BlockIdList = std::vector<BlockId>;

static constexpr BlockId kInvalidBlockId = UINT32_MAX;

struct BlockInfo {
  BlockId id;
  const llvm::BasicBlock *bb;
  ValueSet def;
  ValueSet use;
  BlockIdList succ;
};

struct CFG {
  vector<BlockInfo> blocks;
  BlockId entry = 0;
  BlockId exit = 0;

  uint32_t size() const { return static_cast<uint32_t>(blocks.size()); }

  const BlockIdList &successors(BlockId id) const { return blocks[id].succ; }

  const BlockIdList &predecessors(BlockId id) const { return preds_cache[id]; }

  void compute_predecessors() {
    preds_cache.resize(blocks.size());
    for (auto &p : preds_cache)
      p.clear();
    for (auto &b : blocks)
      for (auto s : b.succ)
        preds_cache[s].push_back(b.id);
  }

private:
  vector<BlockIdList> preds_cache;
};

} // namespace cat::opt::ana
