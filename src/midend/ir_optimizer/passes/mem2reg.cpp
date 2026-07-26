#include "mem2reg.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>

#include <deque>
#include <set>

namespace cat::opt {

PromoteMem2Reg::PromoteMem2Reg(llvm::Function &F, llvm::DominatorTree &DT,
                               llvm::DominanceFrontier &DF)
    : F(F), DT(DT), DF(DF) {}

// ---------------------------------------------------------------------------
// Stage 1: Filter promotable allocas
// ---------------------------------------------------------------------------

bool PromoteMem2Reg::is_promotable(llvm::AllocaInst *AI) {
  llvm::Type *alloc_ty = AI->getAllocatedType();
  // Only scalar types are promotable; arrays/structs require SROA first.
  if (!alloc_ty->isSingleValueType())
    return false;

  for (auto *U : AI->users()) {
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(U)) {
      if (LI->isVolatile())
        return false;
      continue;
    }
    if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(U)) {
      if (SI->isVolatile())
        return false;
      // The alloca must be the store target, not the stored value.
      if (SI->getPointerOperand() != AI)
        return false;
      // The alloca address itself must not be stored elsewhere (address escape).
      if (SI->getValueOperand() == AI)
        return false;
      continue;
    }
    // Any other user (GEP, bitcast, call arg, etc.) means address escape.
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------
// Stage 2: Collect def/use info
// ---------------------------------------------------------------------------

void PromoteMem2Reg::compute_alloca_info(llvm::AllocaInst *AI,
                                         AllocaInfo &Info) {
  Info.Alloca = AI;
  Info.only_used_in_one_block = true;
  Info.only_block = nullptr;
  Info.only_store = nullptr;

  unsigned store_count = 0;
  for (auto *U : AI->users()) {
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(U)) {
      auto *BB = LI->getParent();
      Info.using_blocks.push_back(BB);
      if (Info.only_block == nullptr)
        Info.only_block = BB;
      else if (Info.only_block != BB)
        Info.only_used_in_one_block = false;
    } else if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(U)) {
      auto *BB = SI->getParent();
      Info.defining_blocks.push_back(BB);
      if (Info.only_block == nullptr)
        Info.only_block = BB;
      else if (Info.only_block != BB)
        Info.only_used_in_one_block = false;
      ++store_count;
      Info.only_store = SI;
    }
  }
  // only_store is meaningful only when there is exactly one store.
  if (store_count != 1)
    Info.only_store = nullptr;
}

// ---------------------------------------------------------------------------
// Stage 3: Fast-path promotion (no phi nodes needed)
// ---------------------------------------------------------------------------

bool PromoteMem2Reg::try_promote_single_store(AllocaInfo &Info) {
  if (!Info.only_store)
    return false;

  llvm::Value *stored_val = Info.only_store->getValueOperand();
  for (auto *U : Info.Alloca->users()) {
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(U)) {
      // If the store dominates the load, the loaded value is known.
      // Otherwise (e.g. loop back-edge before first iteration), use undef.
      if (DT.dominates(Info.only_store, LI))
        LI->replaceAllUsesWith(stored_val);
      else
        LI->replaceAllUsesWith(
            llvm::UndefValue::get(Info.Alloca->getAllocatedType()));
      to_erase.insert(LI);
    }
  }
  to_erase.insert(Info.only_store);
  to_erase.insert(Info.Alloca);
  return true;
}

bool PromoteMem2Reg::try_promote_single_block(AllocaInfo &Info) {
  if (!Info.only_used_in_one_block || !Info.only_block)
    return false;

  llvm::Type *alloc_ty = Info.Alloca->getAllocatedType();
  // Walk instructions in order, tracking the last stored value.
  llvm::Value *cur_val = llvm::UndefValue::get(alloc_ty);

  for (auto &Inst : *Info.only_block) {
    if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&Inst)) {
      if (SI->getPointerOperand() == Info.Alloca) {
        cur_val = SI->getValueOperand();
        to_erase.insert(SI);
        continue;
      }
    }
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&Inst)) {
      if (LI->getPointerOperand() == Info.Alloca) {
        LI->replaceAllUsesWith(cur_val);
        to_erase.insert(LI);
        continue;
      }
    }
  }
  to_erase.insert(Info.Alloca);
  return true;
}

// ---------------------------------------------------------------------------
// Stage 4: Insert phi nodes at iterated dominance frontiers
// ---------------------------------------------------------------------------

void PromoteMem2Reg::insert_phi_nodes() {
  for (unsigned alloca_idx = 0; alloca_idx < allocas.size(); ++alloca_idx) {
    auto *AI = allocas[alloca_idx];
    llvm::Type *alloc_ty = AI->getAllocatedType();

    // Collect blocks that contain a store to this alloca.
    std::set<llvm::BasicBlock *> defining_block_set;
    for (auto *U : AI->users()) {
      if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(U))
        defining_block_set.insert(SI->getParent());
    }

    std::set<llvm::BasicBlock *> visited_frontier;
    std::deque<llvm::BasicBlock *> worklist;
    for (auto *BB : defining_block_set)
      worklist.push_back(BB);

    // Iteratively walk dominance frontiers from each defining block.
    while (!worklist.empty()) {
      auto *B = worklist.front();
      worklist.pop_front();

      auto it = DF.find(B);
      if (it == DF.end())
        continue;

      for (auto *df_block : it->second) {
        if (!visited_frontier.insert(df_block).second)
          continue;

        // Create phi with undef placeholders for each predecessor.
        // Actual values are filled in during rename_block (Stage 5).
        auto *Phi = llvm::PHINode::Create(alloc_ty, 0, "",
                                          df_block->getFirstInsertionPt());
        for (auto *Pred : llvm::predecessors(df_block))
          Phi->addIncoming(llvm::UndefValue::get(alloc_ty), Pred);

        new_phi_nodes[{df_block, alloca_idx}] = Phi;

        // If this frontier block is not itself a defining block,
        // continue propagating through its dominance frontiers.
        if (defining_block_set.find(df_block) == defining_block_set.end())
          worklist.push_back(df_block);
      }
    }
  }
}

// Pop values from the stack back to the given target size.
static void clear_stack(std::vector<llvm::Value *> &Stack,
                        unsigned target_size) {
  while (Stack.size() > target_size)
    Stack.pop_back();
}

// ---------------------------------------------------------------------------
// Stage 5: Dominator-tree DFS renaming (core SSA construction)
// ---------------------------------------------------------------------------
//
// Traverses the dominator tree in preorder. For each basic block:
//   1. Push phis in this block onto their respective value stacks.
//   2. Replace loads with stack top; push stored values onto stacks.
//   3. Fill incoming values of phis in successor blocks (CFG children).
//   4. Recurse into dominator-tree children.
//   5. Pop all values pushed in this block so sibling subtrees see
//      the correct stack state.
//
// push/pop must be strictly symmetric per block scope.  Each call to
// rename_block tracks its own pushes in a local vector; this avoids
// leaking state between sibling subtrees.

void PromoteMem2Reg::rename_block(llvm::BasicBlock *BB) {
  std::vector<std::pair<llvm::AllocaInst *, unsigned>> push_count_tracker;

  // Push phis present at the start of this block onto their stacks.
  for (unsigned alloca_idx = 0; alloca_idx < allocas.size(); ++alloca_idx) {
    auto *AI = allocas[alloca_idx];
    auto it = new_phi_nodes.find({BB, alloca_idx});
    if (it != new_phi_nodes.end()) {
      value_stacks[AI].push_back(it->second);
      push_count_tracker.emplace_back(AI, 1u);
    }
  }

  // Scan instructions: skip phis we inserted, replace loads, record stores.
  for (auto &Inst : *BB) {
    if (auto *PN = llvm::dyn_cast<llvm::PHINode>(&Inst)) {
      bool is_our_phi = false;
      for (auto &Entry : new_phi_nodes) {
        if (Entry.second == PN) {
          is_our_phi = true;
          break;
        }
      }
      if (is_our_phi)
        continue;
    }

    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(&Inst)) {
      auto *Ptr = LI->getPointerOperand();
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(Ptr)) {
        auto lookup = alloca_lookup.find(AI);
        if (lookup != alloca_lookup.end()) {
          auto &Stack = value_stacks[AI];
          if (!Stack.empty()) {
            LI->replaceAllUsesWith(Stack.back());
          } else {
            // No definition reaches this load — replace with undef.
            LI->replaceAllUsesWith(
                llvm::UndefValue::get(AI->getAllocatedType()));
          }
          to_erase.insert(LI);
        }
      }
    }

    if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(&Inst)) {
      auto *Ptr = SI->getPointerOperand();
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(Ptr)) {
        auto lookup = alloca_lookup.find(AI);
        if (lookup != alloca_lookup.end()) {
          value_stacks[AI].push_back(SI->getValueOperand());
          bool found = false;
          for (auto &entry : push_count_tracker) {
            if (entry.first == AI) {
              entry.second++;
              found = true;
              break;
            }
          }
          if (!found)
            push_count_tracker.emplace_back(AI, 1u);
          to_erase.insert(SI);
        }
      }
    }
  }

  // Fill incoming values of phis in CFG successor blocks.
  for (auto *Succ : llvm::successors(BB)) {
    for (unsigned alloca_idx = 0; alloca_idx < allocas.size(); ++alloca_idx) {
      auto it = new_phi_nodes.find({Succ, alloca_idx});
      if (it == new_phi_nodes.end())
        continue;

      auto *Phi = it->second;
      auto *AI = allocas[alloca_idx];
      auto &Stack = value_stacks[AI];

      llvm::Value *incoming = Stack.empty()
                                  ? llvm::UndefValue::get(AI->getAllocatedType())
                                  : Stack.back();

      Phi->setIncomingValueForBlock(BB, incoming);
    }
  }

  // Recurse into dominator-tree children.
  for (auto *child_node : *DT.getNode(BB))
    rename_block(child_node->getBlock());

  // Pop all values pushed in this block scope.
  for (auto &entry : push_count_tracker)
    clear_stack(value_stacks[entry.first],
                value_stacks[entry.first].size() - entry.second);
}

// ---------------------------------------------------------------------------
// Stage 6: Simplify and remove redundant phi nodes
// ---------------------------------------------------------------------------

void PromoteMem2Reg::cleanup_dead_phis() {
  // Iterate to a fixed point because simplifying one phi may
  // make another phi dead or trivially constant.
  bool changed = true;
  while (changed) {
    changed = false;
    llvm::SmallVector<llvm::PHINode *, 8> phi_to_erase;

    for (auto &Entry : new_phi_nodes) {
      auto *Phi = Entry.second;
      if (!Phi)
        continue;

      // Remove phi with no users.
      if (Phi->use_empty()) {
        phi_to_erase.push_back(Phi);
        continue;
      }

      // If all non-self incoming values are identical, replace phi with
      // that common value (trivial phi).
      llvm::Value *common = nullptr;
      bool all_same = true;
      for (unsigned i = 0; i < Phi->getNumIncomingValues(); ++i) {
        auto *V = Phi->getIncomingValue(i);
        if (V == Phi)
          continue;
        if (!common)
          common = V;
        else if (common != V) {
          all_same = false;
          break;
        }
      }
      if (all_same && common) {
        Phi->replaceAllUsesWith(common);
        phi_to_erase.push_back(Phi);
      }
    }

    for (auto *Phi : phi_to_erase) {
      for (auto &E : new_phi_nodes) {
        if (E.second == Phi)
          E.second = nullptr;
      }
      Phi->eraseFromParent();
      changed = true;
    }
  }
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

void PromoteMem2Reg::run() {
  to_erase.clear();
  allocas.clear();
  alloca_lookup.clear();
  new_phi_nodes.clear();
  value_stacks.clear();

  // Stage 1: collect all promotable allocas.
  llvm::SmallVector<llvm::AllocaInst *, 16> candidate_allocas;
  for (auto &BB : F) {
    for (auto &Inst : BB) {
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(&Inst)) {
        if (is_promotable(AI))
          candidate_allocas.push_back(AI);
      }
    }
  }

  // Stage 2–3: try fast-path promotion; collect remaining allocas.
  std::vector<llvm::AllocaInst *> remaining_allocas;
  for (auto *AI : candidate_allocas) {
    AllocaInfo Info;
    compute_alloca_info(AI, Info);
    if (try_promote_single_store(Info) || try_promote_single_block(Info))
      continue;
    remaining_allocas.push_back(AI);
  }

  // Erase instructions from fast-path promotion before proceeding.
  for (auto *Inst : to_erase)
    Inst->eraseFromParent();
  to_erase.clear();

  if (remaining_allocas.empty())
    return;

  // Stage 4–6: full SSA construction for remaining allocas.
  allocas = std::move(remaining_allocas);
  for (unsigned i = 0; i < allocas.size(); ++i) {
    alloca_lookup[allocas[i]] = i;
    value_stacks[allocas[i]] = {};
  }

  insert_phi_nodes();

  rename_block(&F.getEntryBlock());

  cleanup_dead_phis();

  // Erase the original alloca instructions and all replaced loads/stores.
  for (auto *AI : allocas)
    AI->eraseFromParent();
  for (auto *Inst : to_erase)
    Inst->eraseFromParent();
}

}  // namespace cat::opt
