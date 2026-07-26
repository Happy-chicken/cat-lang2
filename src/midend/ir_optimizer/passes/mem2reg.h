#pragma once

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/IR/Dominators.h>
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
// Requires an already computed DominatorTree and DominanceFrontier.
class PromoteMem2Reg {
public:
  PromoteMem2Reg(llvm::Function &F, llvm::DominatorTree &DT,
                 llvm::DominanceFrontier &DF);
  void run();

private:
  llvm::Function &F;
  llvm::DominatorTree &DT;
  llvm::DominanceFrontier &DF;

  struct AllocaInfo {
    llvm::AllocaInst *Alloca = nullptr;
    llvm::SmallVector<llvm::BasicBlock *, 8>
        defining_blocks;                                   // blocks with stores
    llvm::SmallVector<llvm::BasicBlock *, 8> using_blocks; // blocks with loads
    llvm::StoreInst *only_store = nullptr; // non-null iff exactly one store
    llvm::BasicBlock *only_block =
        nullptr;                        // all loads/stores in the same block
    bool only_used_in_one_block = true; // true if all uses are in one block
  };

  // Allocas that remain after fast-path promotion and require full SSA
  // construction.
  std::vector<llvm::AllocaInst *> allocas;
  // Maps an alloca to its index in |allocas|.
  llvm::DenseMap<llvm::AllocaInst *, unsigned> alloca_lookup;
  // (block, alloca_index) -> inserted phi node.
  llvm::DenseMap<std::pair<llvm::BasicBlock *, unsigned>, llvm::PHINode *>
      new_phi_nodes;
  // Per-alloca value stack used during dominator-tree renaming.
  llvm::DenseMap<llvm::AllocaInst *, std::vector<llvm::Value *>> value_stacks;
  // Instructions to erase after traversal (deferred to avoid iterator
  // invalidation).
  llvm::SmallPtrSet<llvm::Instruction *, 32> to_erase;

  // return true if the alloca can be promoted to SSA registers.
  bool is_promotable(llvm::AllocaInst *AI);

  // collect defining/using blocks and detect fast-path eligibility.
  void compute_alloca_info(llvm::AllocaInst *AI, AllocaInfo &Info);

  // exactly one store dominates all loads.
  bool try_promote_single_store(AllocaInfo &Info);

  // all loads/stores are within a single basic block.
  bool try_promote_single_block(AllocaInfo &Info);

  // insert phi nodes at iterated dominance frontiers.
  void insert_phi_nodes();

  // DFS over the dominator tree, replacing loads/stores with SSA
  // values.
  void rename_block(llvm::BasicBlock *BB);

  // remove unused phis and phis whose incoming values are identical.
  void cleanup_dead_phis();
};

} // namespace cat::opt
