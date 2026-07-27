#include "mem2reg.h"
#include "cfg.h"

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

PromoteMem2Reg::PromoteMem2Reg(llvm::Function &F, ana::DomTree &DT,
                               ana::DomFrontier &DF)
    : F(F), DT(DT), DF(DF) {}

// ---------------------------------------------------------------------------
// Stage 1: Filter promotable allocas
// ---------------------------------------------------------------------------

bool PromoteMem2Reg::is_promotable(llvm::AllocaInst *AI) {
  llvm::Type *alloc_ty = AI->getAllocatedType();
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
      if (SI->getPointerOperand() != AI)
        return false;
      if (SI->getValueOperand() == AI)
        return false;
      continue;
    }
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
  Info.only_block = ana::kInvalidBlockId;
  Info.only_store = nullptr;

  unsigned store_count = 0;
  for (auto *U : AI->users()) {
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(U)) {
      uint32_t bid = DT.id(LI->getParent());
      Info.using_blocks.push_back(bid);
      if (Info.only_block == ana::kInvalidBlockId)
        Info.only_block = bid;
      else if (Info.only_block != bid)
        Info.only_used_in_one_block = false;
    } else if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(U)) {
      uint32_t bid = DT.id(SI->getParent());
      Info.defining_blocks.push_back(bid);
      if (Info.only_block == ana::kInvalidBlockId)
        Info.only_block = bid;
      else if (Info.only_block != bid)
        Info.only_used_in_one_block = false;
      ++store_count;
      Info.only_store = SI;
    }
  }
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
  if (!Info.only_used_in_one_block || Info.only_block == ana::kInvalidBlockId)
    return false;

  auto *BB = DT.block(Info.only_block);
  llvm::Type *alloc_ty = Info.Alloca->getAllocatedType();
  llvm::Value *cur_val = llvm::UndefValue::get(alloc_ty);

  for (auto &Inst : *BB) {
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
    std::set<uint32_t> defining_block_set;
    for (auto *U : AI->users()) {
      if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(U))
        defining_block_set.insert(DT.id(SI->getParent()));
    }

    std::set<uint32_t> visited_frontier;
    std::deque<uint32_t> worklist;
    for (uint32_t bid : defining_block_set)
      worklist.push_back(bid);

    while (!worklist.empty()) {
      uint32_t b = worklist.front();
      worklist.pop_front();

      const auto &fronts = DF.frontier(b);
      for (uint32_t df_id : fronts) {
        if (!visited_frontier.insert(df_id).second)
          continue;

        auto *DFBlock = DT.block(df_id);
        auto *Phi = llvm::PHINode::Create(alloc_ty, 0, "",
                                          DFBlock->getFirstInsertionPt());
        for (auto *Pred : llvm::predecessors(DFBlock))
          Phi->addIncoming(llvm::UndefValue::get(alloc_ty), Pred);

        new_phi_nodes[{df_id, alloca_idx}] = Phi;

        if (defining_block_set.find(df_id) == defining_block_set.end())
          worklist.push_back(df_id);
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

void PromoteMem2Reg::rename_block(uint32_t block_id) {
  std::vector<std::pair<llvm::AllocaInst *, unsigned>> push_count_tracker;

  auto *BB = DT.block(block_id);

  // Push phis present at the start of this block onto their stacks.
  for (unsigned alloca_idx = 0; alloca_idx < allocas.size(); ++alloca_idx) {
    auto *AI = allocas[alloca_idx];
    auto it = new_phi_nodes.find({block_id, alloca_idx});
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
    uint32_t succ_id = DT.id(Succ);
    for (unsigned alloca_idx = 0; alloca_idx < allocas.size(); ++alloca_idx) {
      auto it = new_phi_nodes.find({succ_id, alloca_idx});
      if (it == new_phi_nodes.end())
        continue;

      auto *Phi = it->second;
      auto *AI = allocas[alloca_idx];
      auto &Stack = value_stacks[AI];

      llvm::Value *incoming =
          Stack.empty() ? llvm::UndefValue::get(AI->getAllocatedType())
                        : Stack.back();
      Phi->setIncomingValueForBlock(BB, incoming);
    }
  }

  // Recurse into dominator-tree children.
  for (uint32_t child_id : DT.children(block_id))
    rename_block(child_id);

  // Pop all values pushed in this block scope.
  for (auto &entry : push_count_tracker)
    clear_stack(value_stacks[entry.first],
                value_stacks[entry.first].size() - entry.second);
}

// ---------------------------------------------------------------------------
// Stage 6: Simplify and remove redundant phi nodes
// ---------------------------------------------------------------------------

void PromoteMem2Reg::cleanup_dead_phis() {
  bool changed = true;
  while (changed) {
    changed = false;
    llvm::SmallVector<llvm::PHINode *, 8> phi_to_erase;

    for (auto &Entry : new_phi_nodes) {
      auto *Phi = Entry.second;
      if (!Phi)
        continue;

      if (Phi->use_empty()) {
        phi_to_erase.push_back(Phi);
        continue;
      }

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

  rename_block(DT.entry_id());

  cleanup_dead_phis();

  for (auto *AI : allocas)
    AI->eraseFromParent();
  for (auto *Inst : to_erase)
    Inst->eraseFromParent();
}

} // namespace cat::opt
