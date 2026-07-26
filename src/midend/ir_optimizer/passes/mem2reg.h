#pragma once

#include "domtree.h"

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>

#include <vector>

namespace llvm {
class BasicBlock;
class Function;
} // namespace llvm

namespace cat::opt {

// PromoteMem2Reg — promotes allocas to SSA registers (LLVM mem2reg equivalent).
//
// Six-stage pipeline:
//   1. Filter promotable allocas (scalar type, only load/store users, no
//   escapes).
//   2. Collect def/use block info, detect fast-path candidates.
//   3. Fast path: single-store and single-block promotion without phi nodes.
//   4. Insert phi nodes at iterated dominance frontiers for remaining allocas.
//   5. Dominator-tree DFS renaming with per-alloca value stacks.
//   6. Cleanup: erase promoted allocas and simplify redundant phi nodes.
//
// Uses the custom DomTree / DomFrontier (based on block IDs).
class PromoteMem2Reg {
public:
  PromoteMem2Reg(llvm::Function &F, ana::DomTree &DT, ana::DomFrontier &DF);
  void run();

private:
  llvm::Function &F;
  ana::DomTree &DT;
  ana::DomFrontier &DF;

  struct AllocaInfo {
    llvm::AllocaInst *Alloca = nullptr;
    llvm::SmallVector<uint32_t, 8> defining_blocks; // block IDs with stores
    llvm::SmallVector<uint32_t, 8> using_blocks;    // block IDs with loads
    llvm::StoreInst *only_store = nullptr; // non-null iff exactly one store
    uint32_t only_block = UINT32_MAX;      // block ID if all uses in one block
    bool only_used_in_one_block = true;
  };

  // Allocas that remain after fast-path promotion and require full SSA
  // construction.
  std::vector<llvm::AllocaInst *> allocas;
  // Maps an alloca to its index in |allocas|.
  llvm::DenseMap<llvm::AllocaInst *, unsigned> alloca_lookup;
  // (block_id, alloca_index) -> inserted phi node.
  llvm::DenseMap<std::pair<uint32_t, unsigned>, llvm::PHINode *> new_phi_nodes;
  // Per-alloca value stack used during dominator-tree renaming.
  llvm::DenseMap<llvm::AllocaInst *, std::vector<llvm::Value *>> value_stacks;
  // Instructions to erase after traversal (deferred to avoid iterator
  // invalidation).
  llvm::SmallPtrSet<llvm::Instruction *, 32> to_erase;

  // Stage 1: return true if the alloca can be promoted to SSA registers.
  bool is_promotable(llvm::AllocaInst *AI);

  // Stage 2: collect defining/using blocks and detect fast-path eligibility.
  void compute_alloca_info(llvm::AllocaInst *AI, AllocaInfo &Info);

  // Stage 3 (fast path): exactly one store dominates all loads.
  bool try_promote_single_store(AllocaInfo &Info);

  // Stage 3 (fast path): all loads/stores are within a single basic block.
  bool try_promote_single_block(AllocaInfo &Info);

  // Stage 4: insert phi nodes at iterated dominance frontiers.
  void insert_phi_nodes();

  // Stage 5: DFS over the dominator tree, replacing loads/stores with SSA
  // values.
  void rename_block(uint32_t block_id);

  // Stage 6: remove unused phis and phis whose incoming values are identical.
  void cleanup_dead_phis();
};

} // namespace cat::opt
