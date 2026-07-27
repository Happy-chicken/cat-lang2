#pragma once

#include <llvm/ADT/DenseMap.h>
#include <llvm/ADT/SmallPtrSet.h>
#include <llvm/ADT/SmallVector.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Value.h>
#include <llvm/Support/raw_ostream.h>

#include <map>
#include <string>
#include <vector>

namespace llvm {
class BasicBlock;
class Function;
class Module;
}  // namespace llvm

namespace cat::opt {

// Describes a function that allocates heap memory (e.g. malloc, calloc).
struct AllocSpec {
  std::string func_name;
  bool returns_new_pointer = true;
};

// Describes a function that deallocates heap memory (e.g. free).
struct DeallocSpec {
  std::string func_name;
  unsigned pointer_arg_index = 0;
};

// State lattice for tracking an allocation's lifetime.
enum class LeakState {
  Unknown,
  Allocated,
  Freed,
  Escaped,
  DoubleFreed,
  MaybeAllocated
};

// A call instruction that performs a heap allocation.
struct AllocSite {
  llvm::CallInst *alloc_call = nullptr;
  unsigned id = 0;
};

// A detected leak report.
struct LeakReport {
  AllocSite *site = nullptr;
  llvm::BasicBlock *exit_block = nullptr;
  bool is_definite = true;
};

// Per-block analysis result for one AllocSite.
struct PerBlockStates {
  LeakState must_state;
  LeakState may_state;
};

// Cross-function summary computed once per module.
struct FuncSummary {
  bool returns_alloc = false;             // return value is a newly allocated pointer
  std::map<unsigned, int> param_effects;  // 0=none, 1=frees, 2=escapes
};

// Compute per-function summaries across the module.
std::map<llvm::Function *, FuncSummary>
compute_func_summaries(llvm::Module &M);

// LeakChecker — detects heap memory leaks within a single function.
//
// Optionally uses cross-function summaries (computed by compute_func_summaries)
// to track heap allocations returned by internal functions (e.g. class
// constructors) and pointer frees performed inside callees.
class LeakChecker {
public:
  LeakChecker(llvm::Function &F, llvm::DominatorTree &DT,
              bool strict_mode = false,
              const std::map<llvm::Function *, FuncSummary> *summaries = nullptr);
  void run();
  const std::vector<LeakReport> &get_reports() const { return reports; }

  // Walk backward along def-use chain to find alloc site indices.
  static llvm::SmallVector<unsigned, 2>
  trace_to_alloc(llvm::Value *V,
                 const std::vector<AllocSite> &sites,
                 llvm::DenseMap<llvm::CallInst *, const DeallocSpec *>
                     *dealloc_calls_ptr = nullptr);

  void dump_reports(llvm::raw_ostream &OS) const;

private:
  llvm::Function &F;
  llvm::DominatorTree &DT;
  bool strict_mode;
  const std::map<llvm::Function *, FuncSummary> *summaries;

  std::vector<AllocSpec> alloc_specs;
  std::vector<DeallocSpec> dealloc_specs;

  std::vector<AllocSite> alloc_sites; // alloc sites found in this function
  llvm::DenseMap<llvm::CallInst *, const DeallocSpec *> dealloc_calls; // a call releases some pointer

  std::vector<llvm::DenseMap<llvm::BasicBlock *, PerBlockStates>> block_results;
  std::vector<LeakReport> reports;

  void collect_alloc_dealloc();
  llvm::SmallVector<AllocSite *, 2> resolve_alloc_sites(llvm::Value *V);
  bool is_escaping_use(llvm::Instruction *User, AllocSite *AS);
  static LeakState meet_must(LeakState a, LeakState b);
  static LeakState meet_may(LeakState a, LeakState b);
  void run_dataflow(AllocSite *AS);
  void transfer_block(llvm::BasicBlock *BB, AllocSite *AS,
                      LeakState &must, LeakState &may);
  void collect_reports();
  static std::vector<llvm::BasicBlock *> find_path(llvm::BasicBlock *from,
                                                    llvm::BasicBlock *to);
};

}  // namespace cat::opt
