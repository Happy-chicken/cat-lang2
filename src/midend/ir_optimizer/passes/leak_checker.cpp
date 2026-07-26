#include "leak_checker.h"

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/CFG.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/InstrTypes.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include <algorithm>
#include <deque>
#include <set>

namespace cat::opt {

// ---------------------------------------------------------------------------
// Phase 0: Built-in alloc / dealloc specs
// ---------------------------------------------------------------------------

static std::vector<AllocSpec> default_alloc_specs() {
  return {{"malloc", true}, {"calloc", true}, {"realloc", true}};
}

static std::vector<DeallocSpec> default_dealloc_specs() {
  return {{"free", 0}};
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static bool is_pointer_type(llvm::Type *Ty) { return Ty->isPointerTy(); }

static bool stores_to_global(llvm::StoreInst *SI) {
  auto *Ptr = SI->getPointerOperand();
  return llvm::isa<llvm::GlobalVariable>(Ptr) ||
         llvm::isa<llvm::GlobalVariable>(Ptr->stripPointerCasts());
}

// Walk backward along def-use chains (phi / select / bitcast / GEP / load)
// to check if a Value resolves to any AllocSite in |sites|.
llvm::SmallVector<unsigned, 2> LeakChecker::trace_to_alloc(
    llvm::Value *V, const std::vector<AllocSite> &sites,
    llvm::DenseMap<llvm::CallInst *, const DeallocSpec *> *dealloc_calls_ptr) {
  llvm::SmallVector<unsigned, 2> result;
  if (!is_pointer_type(V->getType()))
    return result;

  std::deque<llvm::Value *> worklist;
  llvm::SmallPtrSet<llvm::Value *, 16> visited;
  worklist.push_back(V);

  while (!worklist.empty()) {
    auto *Val = worklist.front();
    worklist.pop_front();
    if (!visited.insert(Val).second)
      continue;

    for (unsigned i = 0; i < sites.size(); ++i) {
      if (Val == sites[i].alloc_call) {
        result.push_back(i);
        break;
      }
    }

    if (auto *PN = llvm::dyn_cast<llvm::PHINode>(Val)) {
      for (auto &Incoming : PN->incoming_values())
        worklist.push_back(Incoming.get());
      continue;
    }
    if (auto *SI = llvm::dyn_cast<llvm::SelectInst>(Val)) {
      worklist.push_back(SI->getTrueValue());
      worklist.push_back(SI->getFalseValue());
      continue;
    }
    if (auto *BC = llvm::dyn_cast<llvm::BitCastInst>(Val)) {
      worklist.push_back(BC->getOperand(0));
      continue;
    }
    if (auto *GEP = llvm::dyn_cast<llvm::GetElementPtrInst>(Val)) {
      worklist.push_back(GEP->getPointerOperand());
      continue;
    }
    if (auto *LI = llvm::dyn_cast<llvm::LoadInst>(Val)) {
      auto *Ptr = LI->getPointerOperand();
      if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(Ptr)) {
        bool cross_block = false;
        for (auto *U : AI->users()) {
          if (auto *S = llvm::dyn_cast<llvm::StoreInst>(U)) {
            if (S->getParent() != AI->getParent()) {
              cross_block = true;
              break;
            }
          }
        }
        if (!cross_block) {
          llvm::Value *cur = nullptr;
          for (auto &Inst : *AI->getParent()) {
            if (auto *S = llvm::dyn_cast<llvm::StoreInst>(&Inst)) {
              if (S->getPointerOperand() == AI)
                cur = S->getValueOperand();
            }
            if (&Inst == LI)
              break;
          }
          if (cur)
            worklist.push_back(cur);
        }
      }
      continue;
    }
  }
  return result;
}

// ---------------------------------------------------------------------------
// Cross-function summary computation
// ---------------------------------------------------------------------------

std::map<llvm::Function *, FuncSummary>
compute_func_summaries(llvm::Module &M) {
  std::map<llvm::Function *, FuncSummary> result;

  // Fixed-point iteration: summaries for functions that call each other
  // need to converge.  Since our lattice is simple (returns_alloc is a
  // monotonic bool, param_effects grow monotonically), a few rounds suffice.
  bool changed = true;
  for (int round = 0; round < 10 && changed; ++round) {
    changed = false;

    for (auto &F : M) {
      if (F.isDeclaration())
        continue;
      if (F.getName().starts_with("llvm."))
        continue;

      auto &sum = result[&F];
      bool prev_returns = sum.returns_alloc;

      // Collect alloc sites inside this function.
      std::vector<AllocSite> local_sites;
      llvm::DenseMap<llvm::CallInst *, const DeallocSpec *> local_dealloc;
      for (auto &BB : F) {
        for (auto &Inst : BB) {
          auto *CI = llvm::dyn_cast<llvm::CallInst>(&Inst);
          if (!CI)
            continue;
          auto *Callee = CI->getCalledFunction();
          if (!Callee)
            continue;
          auto name = Callee->getName().str();
          if (name == "malloc" || name == "calloc" || name == "realloc") {
            AllocSite site;
            site.alloc_call = CI;
            site.id = static_cast<unsigned>(local_sites.size());
            local_sites.push_back(site);
          }
          if (name == "free") {
            static const DeallocSpec free_spec{"free", 0};
            local_dealloc[CI] = &free_spec;
          }
          // If Callee is an internal function whose summary says returns_alloc,
          // treat this call as a local alloc site.
          auto it = result.find(Callee);
          if (it != result.end() && it->second.returns_alloc) {
            AllocSite site;
            site.alloc_call = CI;
            site.id = static_cast<unsigned>(local_sites.size());
            local_sites.push_back(site);
          }
        }
      }

      // Check if return value resolves to a local alloc site.
      sum.returns_alloc = false;
      for (auto &BB : F) {
        for (auto &Inst : BB) {
          auto *RI = llvm::dyn_cast<llvm::ReturnInst>(&Inst);
          if (!RI || !RI->getReturnValue())
            continue;
          auto idxs = LeakChecker::trace_to_alloc(
              RI->getReturnValue(), local_sites, &local_dealloc);
          if (!idxs.empty()) {
            sum.returns_alloc = true;
            break;
          }
        }
        if (sum.returns_alloc)
          break;
      }

      // Check parameter effects: does this function free or escape any
      // of its pointer arguments?
      for (unsigned ai = 0; ai < F.arg_size(); ++ai) {
        auto *Arg = F.getArg(ai);
        if (!Arg->getType()->isPointerTy())
          continue;

        int prev_effect = sum.param_effects.count(ai) ? sum.param_effects[ai] : 0;

        // Check if Arg is passed to free (directly or via internal call).
        for (auto *U : Arg->users()) {
          if (auto *CI = llvm::dyn_cast<llvm::CallInst>(U)) {
            if (local_dealloc.count(CI)) {
              sum.param_effects[ai] = 1; // Frees
              goto next_arg;
            }
            auto *Callee = CI->getCalledFunction();
            if (Callee) {
              auto sit = result.find(Callee);
              if (sit != result.end()) {
                // Find which arg position our Arg is at.
                for (unsigned ci = 0; ci < CI->arg_size(); ++ci) {
                  if (CI->getArgOperand(ci) == Arg ||
                      CI->getArgOperand(ci)->stripPointerCasts() == Arg) {
                    auto pit = sit->second.param_effects.find(ci);
                    if (pit != sit->second.param_effects.end()) {
                      if (pit->second == 1) // Frees
                        sum.param_effects[ai] = 1;
                    }
                  }
                }
              }
            }
          }
        }

        next_arg:
        if (sum.param_effects.count(ai) && sum.param_effects[ai] != prev_effect)
          changed = true;
      }

      if (prev_returns != sum.returns_alloc)
        changed = true;
    }
  }

  return result;
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LeakChecker::LeakChecker(llvm::Function &F, llvm::DominatorTree &DT,
                         bool strict_mode,
                         const std::map<llvm::Function *, FuncSummary> *summaries)
    : F(F), DT(DT), strict_mode(strict_mode), summaries(summaries),
      alloc_specs(default_alloc_specs()),
      dealloc_specs(default_dealloc_specs()) {}

// ---------------------------------------------------------------------------
// Phase 1: Collect alloc sites and dealloc calls
// ---------------------------------------------------------------------------

void LeakChecker::collect_alloc_dealloc() {
  alloc_sites.clear();
  dealloc_calls.clear();

  // Direct alloc calls (malloc / calloc / realloc).
  for (auto &BB : F) {
    for (auto &Inst : BB) {
      auto *CI = llvm::dyn_cast<llvm::CallInst>(&Inst);
      if (!CI)
        continue;
      auto *Callee = CI->getCalledFunction();
      if (!Callee)
        continue;
      auto callee_name = Callee->getName().str();

      for (const auto &spec : dealloc_specs) {
        if (callee_name == spec.func_name) {
          dealloc_calls[CI] = &spec;
          break;
        }
      }
      for (const auto &spec : alloc_specs) {
        if (callee_name != spec.func_name)
          continue;
        AllocSite site;
        site.alloc_call = CI;
        site.id = static_cast<unsigned>(alloc_sites.size());
        alloc_sites.push_back(site);
        break;
      }
    }
  }

  // Indirect alloc calls: internal functions that return a heap allocation.
  if (summaries) {
    for (auto &BB : F) {
      for (auto &Inst : BB) {
        auto *CI = llvm::dyn_cast<llvm::CallInst>(&Inst);
        if (!CI)
          continue;
        auto *Callee = CI->getCalledFunction();
        if (!Callee || Callee->isDeclaration())
          continue;
        auto it = summaries->find(Callee);
        if (it != summaries->end() && it->second.returns_alloc) {
          AllocSite site;
          site.alloc_call = CI;
          site.id = static_cast<unsigned>(alloc_sites.size());
          alloc_sites.push_back(site);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Phase 2: Resolve which AllocSites a Value may originate from
// ---------------------------------------------------------------------------

llvm::SmallVector<AllocSite *, 2> LeakChecker::resolve_alloc_sites(
    llvm::Value *V) {
  llvm::SmallVector<AllocSite *, 2> result;
  auto idxs = trace_to_alloc(V, alloc_sites, &dealloc_calls);
  for (unsigned idx : idxs)
    result.push_back(&alloc_sites[idx]);
  return result;
}

// ---------------------------------------------------------------------------
// Phase 3: Check whether an instruction causes an escape
// ---------------------------------------------------------------------------

bool LeakChecker::is_escaping_use(llvm::Instruction *User, AllocSite *AS) {
  if (auto *RI = llvm::dyn_cast<llvm::ReturnInst>(User)) {
    if (RI->getReturnValue()) {
      auto resolved = resolve_alloc_sites(RI->getReturnValue());
      for (auto *rs : resolved)
        if (rs == AS)
          return true;
    }
    return false;
  }

  if (auto *SI = llvm::dyn_cast<llvm::StoreInst>(User)) {
    if (stores_to_global(SI)) {
      auto resolved = resolve_alloc_sites(SI->getValueOperand());
      for (auto *rs : resolved)
        if (rs == AS)
          return true;
      return false;
    }
    if (auto *AI = llvm::dyn_cast<llvm::AllocaInst>(
            SI->getPointerOperand()->stripPointerCasts())) {
      bool cross_block = false;
      for (auto *U : AI->users()) {
        if (auto *L = llvm::dyn_cast<llvm::LoadInst>(U)) {
          if (L->getParent() != AI->getParent()) { cross_block = true; break; }
        }
        if (auto *S = llvm::dyn_cast<llvm::StoreInst>(U)) {
          if (S != SI && S->getParent() != AI->getParent()) {
            cross_block = true;
            break;
          }
        }
      }
      if (cross_block) {
        auto resolved = resolve_alloc_sites(SI->getValueOperand());
        for (auto *rs : resolved)
          if (rs == AS)
            return true;
      }
    }
    return false;
  }

  if (auto *CI = llvm::dyn_cast<llvm::CallInst>(User)) {
    auto *Callee = CI->getCalledFunction();
    if (!Callee)
      return false;

    // Known dealloc call matching this AS → it's a free, not an escape.
    auto dealloc_it = dealloc_calls.find(CI);
    if (dealloc_it != dealloc_calls.end()) {
      unsigned arg_idx = dealloc_it->second->pointer_arg_index;
      if (arg_idx < CI->arg_size()) {
        auto resolved = resolve_alloc_sites(CI->getArgOperand(arg_idx));
        for (auto *rs : resolved)
          if (rs == AS)
            return false;
      }
    }

    bool arg_matches = false;
    for (unsigned i = 0; i < CI->arg_size(); ++i) {
      auto resolved = resolve_alloc_sites(CI->getArgOperand(i));
      for (auto *rs : resolved)
        if (rs == AS) { arg_matches = true; break; }
      if (arg_matches)
        break;
    }
    if (!arg_matches)
      return false;

    if (strict_mode)
      return false;

    // Internal function: consult summary.
    if (!Callee->isDeclaration() && summaries) {
      auto it = summaries->find(Callee);
      if (it != summaries->end()) {
        // If the callee frees this argument, it's NOT an escape.
        for (unsigned i = 0; i < CI->arg_size(); ++i) {
          auto resolved = resolve_alloc_sites(CI->getArgOperand(i));
          for (auto *rs : resolved) {
            if (rs != AS) continue;
            auto pit = it->second.param_effects.find(i);
            if (pit != it->second.param_effects.end() && pit->second == 1)
              return false; // callee frees → not an escape
          }
        }
      }
    }

    // External function in conservative mode → escape.
    if (Callee->isDeclaration())
      return true;
  }

  return false;
}

// ---------------------------------------------------------------------------
// Phase 4: State-lattice meet functions
// ---------------------------------------------------------------------------

LeakState LeakChecker::meet_must(LeakState a, LeakState b) {
  if (a == LeakState::Unknown) return b;
  if (b == LeakState::Unknown) return a;
  if (a == LeakState::Escaped || b == LeakState::Escaped) return LeakState::Escaped;
  if (a == LeakState::DoubleFreed || b == LeakState::DoubleFreed) return LeakState::DoubleFreed;
  if (a == LeakState::Freed || b == LeakState::Freed) return LeakState::Freed;
  if (a == LeakState::MaybeAllocated || b == LeakState::MaybeAllocated) return LeakState::MaybeAllocated;
  if (a == LeakState::Allocated && b == LeakState::Allocated) return LeakState::Allocated;
  return LeakState::Unknown;
}

LeakState LeakChecker::meet_may(LeakState a, LeakState b) {
  if (a == LeakState::Unknown) return b;
  if (b == LeakState::Unknown) return a;
  if (a == LeakState::Escaped || b == LeakState::Escaped) return LeakState::Escaped;
  if (a == LeakState::DoubleFreed || b == LeakState::DoubleFreed) return LeakState::DoubleFreed;
  if (a == LeakState::Allocated && b == LeakState::Allocated) return LeakState::Allocated;
  if (a == LeakState::Allocated || b == LeakState::Allocated) return LeakState::MaybeAllocated;
  if (a == LeakState::Freed || b == LeakState::Freed) {
    if (a == LeakState::MaybeAllocated || b == LeakState::MaybeAllocated) return LeakState::MaybeAllocated;
    return LeakState::Freed;
  }
  if (a == LeakState::MaybeAllocated || b == LeakState::MaybeAllocated) return LeakState::MaybeAllocated;
  return LeakState::Unknown;
}

// ---------------------------------------------------------------------------
// Phase 5: Forward worklist dataflow per AllocSite
// ---------------------------------------------------------------------------

void LeakChecker::transfer_block(llvm::BasicBlock *BB, AllocSite *AS,
                                  LeakState &must, LeakState &may) {
  for (auto &Inst : *BB) {
    if (&Inst == AS->alloc_call) {
      must = LeakState::Allocated;
      may = LeakState::Allocated;
      continue;
    }

    // realloc: free old pointer.
    if (auto *CI = llvm::dyn_cast<llvm::CallInst>(&Inst)) {
      auto *Callee = CI->getCalledFunction();
      if (Callee && Callee->getName() == "realloc" && CI->arg_size() >= 1) {
        auto resolved = resolve_alloc_sites(CI->getArgOperand(0));
        for (auto *rs : resolved) {
          if (rs != AS) continue;
          if (must == LeakState::Allocated) must = LeakState::Freed;
          else if (must == LeakState::Freed) must = LeakState::DoubleFreed;
          if (may == LeakState::Allocated || may == LeakState::MaybeAllocated) may = LeakState::Freed;
          else if (may == LeakState::Freed) may = LeakState::DoubleFreed;
          break;
        }
      }
    }

    // Dealloc call.
    if (auto *CI = llvm::dyn_cast<llvm::CallInst>(&Inst)) {
      auto it = dealloc_calls.find(CI);
      if (it != dealloc_calls.end()) {
        unsigned arg_idx = it->second->pointer_arg_index;
        if (arg_idx < CI->arg_size()) {
          auto resolved = resolve_alloc_sites(CI->getArgOperand(arg_idx));
          for (auto *rs : resolved) {
            if (rs != AS) continue;
            if (must == LeakState::Allocated) must = LeakState::Freed;
            else if (must == LeakState::Freed) must = LeakState::DoubleFreed;
            if (may == LeakState::Allocated || may == LeakState::MaybeAllocated) may = LeakState::Freed;
            else if (may == LeakState::Freed) may = LeakState::DoubleFreed;
            break;
          }
        }
        continue;
      }
    }

    // Cross-function: internal call that frees one of our tracked pointers.
    if (auto *CI = llvm::dyn_cast<llvm::CallInst>(&Inst)) {
      auto *Callee = CI->getCalledFunction();
      if (Callee && !Callee->isDeclaration() && summaries) {
        auto sit = summaries->find(Callee);
        if (sit != summaries->end()) {
          for (unsigned ai = 0; ai < CI->arg_size(); ++ai) {
            auto pit = sit->second.param_effects.find(ai);
            if (pit == sit->second.param_effects.end() || pit->second != 1)
              continue;
            auto resolved = resolve_alloc_sites(CI->getArgOperand(ai));
            for (auto *rs : resolved) {
              if (rs != AS) continue;
              if (must == LeakState::Allocated) must = LeakState::Freed;
              else if (must == LeakState::Freed) must = LeakState::DoubleFreed;
              if (may == LeakState::Allocated || may == LeakState::MaybeAllocated) may = LeakState::Freed;
              else if (may == LeakState::Freed) may = LeakState::DoubleFreed;
              break;
            }
          }
        }
      }
    }

    // Escape check (only when state is live).
    if (must == LeakState::Allocated || may == LeakState::Allocated ||
        may == LeakState::MaybeAllocated) {
      if (is_escaping_use(&Inst, AS)) {
        if (must == LeakState::Allocated) must = LeakState::Escaped;
        if (may == LeakState::Allocated || may == LeakState::MaybeAllocated)
          may = LeakState::Escaped;
      }
    }
  }
}

void LeakChecker::run_dataflow(AllocSite *AS) {
  llvm::DenseMap<llvm::BasicBlock *, LeakState> block_must;
  llvm::DenseMap<llvm::BasicBlock *, LeakState> block_may;

  for (auto &BB : F) {
    block_must[&BB] = LeakState::Unknown;
    block_may[&BB] = LeakState::Unknown;
  }

  std::deque<llvm::BasicBlock *> worklist;
  llvm::SmallPtrSet<llvm::BasicBlock *, 32> in_worklist;
  for (auto &BB : F) {
    worklist.push_back(&BB);
    in_worklist.insert(&BB);
  }

  unsigned iter = 0;
  while (!worklist.empty()) {
    if (++iter > 10000) {
      llvm::errs() << "LeakChecker: WARNING — worklist did not converge for "
                      "AS " << AS->id << " in " << F.getName() << "\n";
      break;
    }
    auto *BB = worklist.front();
    worklist.pop_front();
    in_worklist.erase(BB);

    LeakState in_must = LeakState::Unknown;
    LeakState in_may = LeakState::Unknown;
    bool has_pred = false;
    for (auto *Pred : llvm::predecessors(BB)) {
      has_pred = true;
      in_must = meet_must(in_must, block_must[Pred]);
      in_may = meet_may(in_may, block_may[Pred]);
    }
    if (!has_pred) {
      in_must = LeakState::Unknown;
      in_may = LeakState::Unknown;
    }

    LeakState out_must = in_must;
    LeakState out_may = in_may;
    transfer_block(BB, AS, out_must, out_may);

    if (block_must[BB] != out_must || block_may[BB] != out_may) {
      block_must[BB] = out_must;
      block_may[BB] = out_may;
      for (auto *Succ : llvm::successors(BB)) {
        if (in_worklist.insert(Succ).second)
          worklist.push_back(Succ);
      }
    }
  }

  auto &result = block_results[AS->id];
  for (auto &BB : F) {
    PerBlockStates st;
    st.must_state = block_must[&BB];
    st.may_state = block_may[&BB];
    result[&BB] = st;
  }
}

// ---------------------------------------------------------------------------
// Phase 6: Generate reports from final per-block states
// ---------------------------------------------------------------------------

void LeakChecker::collect_reports() {
  for (auto &AS : alloc_sites) {
    auto &result = block_results[AS.id];
    for (auto &BB : F) {
      bool has_ret = false;
      for (auto &Inst : BB) {
        if (llvm::isa<llvm::ReturnInst>(&Inst)) { has_ret = true; break; }
      }
      if (!has_ret) continue;

      auto it = result.find(&BB);
      if (it == result.end()) continue;
      auto &st = it->second;

      if (st.must_state == LeakState::Allocated) {
        LeakReport r;
        r.site = &AS;
        r.exit_block = &BB;
        r.is_definite = true;
        reports.push_back(r);
      }

      if (st.may_state == LeakState::Allocated ||
          st.may_state == LeakState::MaybeAllocated) {
        if (st.must_state != LeakState::Allocated) {
          LeakReport r;
          r.site = &AS;
          r.exit_block = &BB;
          r.is_definite = false;
          reports.push_back(r);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Main entry point
// ---------------------------------------------------------------------------

void LeakChecker::run() {
  reports.clear();
  alloc_sites.clear();
  dealloc_calls.clear();

  collect_alloc_dealloc();

  if (alloc_sites.empty())
    return;

  block_results.clear();
  block_results.resize(alloc_sites.size());

  for (auto &AS : alloc_sites)
    run_dataflow(&AS);

  collect_reports();
}

// ---------------------------------------------------------------------------
// Debug / output helpers
// ---------------------------------------------------------------------------

std::vector<llvm::BasicBlock *> LeakChecker::find_path(llvm::BasicBlock *from,
                                                        llvm::BasicBlock *to) {
  std::vector<llvm::BasicBlock *> result;
  if (from == to) { result.push_back(from); return result; }

  llvm::SmallPtrSet<llvm::BasicBlock *, 32> visited;
  llvm::DenseMap<llvm::BasicBlock *, llvm::BasicBlock *> parent;
  std::deque<llvm::BasicBlock *> worklist;
  worklist.push_back(from);
  visited.insert(from);

  while (!worklist.empty()) {
    auto *BB = worklist.front();
    worklist.pop_front();
    if (BB == to) {
      for (auto *cur = to; cur != from; cur = parent[cur])
        result.push_back(cur);
      result.push_back(from);
      std::reverse(result.begin(), result.end());
      return result;
    }
    for (auto *Succ : llvm::successors(BB)) {
      if (visited.insert(Succ).second) {
        parent[Succ] = BB;
        worklist.push_back(Succ);
      }
    }
  }
  return result;
}

void LeakChecker::dump_reports(llvm::raw_ostream &OS) const {
  if (reports.empty()) {
    OS << "=== Leak Analysis for function '" << F.getName()
       << "': no leaks detected ===\n";
    return;
  }

  OS << "=== Leak Analysis for function '" << F.getName() << "' ===\n";
  OS << "  Found " << reports.size() << " issue(s)\n\n";

  for (unsigned i = 0; i < reports.size(); ++i) {
    auto &r = reports[i];
    auto *CI = r.site->alloc_call;
    auto *Callee = CI->getCalledFunction();
    auto alloc_fn_name = Callee ? Callee->getName().str() : "<unknown>";

    unsigned alloc_idx = 0;
    for (auto &Inst : *CI->getParent()) { if (&Inst == CI) break; ++alloc_idx; }
    unsigned exit_idx = 0;
    for (auto &Inst : *r.exit_block) {
      if (llvm::isa<llvm::ReturnInst>(&Inst)) break;
      ++exit_idx;
    }

    OS << "[" << (i + 1) << "] "
       << (r.is_definite ? "DEFINITE LEAK" : "POTENTIAL LEAK") << "\n";
    OS << "    Alloc : " << alloc_fn_name << " @ " << F.getName() << ":"
       << CI->getParent()->getName() << ":" << alloc_idx << "\n";
    OS << "    Exit  : " << F.getName() << ":" << r.exit_block->getName()
       << ":" << exit_idx << "\n";

    auto path = find_path(CI->getParent(), r.exit_block);
    if (!path.empty()) {
      OS << "    Path  : ";
      for (unsigned j = 0; j < path.size(); ++j) {
        if (j > 0) OS << " -> ";
        OS << path[j]->getName();
      }
      OS << "\n";
    }

    if (!r.is_definite)
      OS << "    Note  : only some control-flow paths freed; "
            "please check remaining branches\n";
    OS << "\n";
  }
}

}  // namespace cat::opt
