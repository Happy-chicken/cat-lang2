#pragma once
#include "common.h"
#include <llvm-20/llvm/ADT/DenseMap.h>
#include <llvm-20/llvm/ADT/DenseSet.h>
#include <llvm-20/llvm/IR/BasicBlock.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/Instruction.h>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/IR/Value.h>

namespace cat::opt::ana {

  using ValueSet = llvm::DenseSet<const llvm::Value *>;

  struct BlockInfo {
    uint32_t id;
    ValueSet def;
    ValueSet use;
    vector<uint32_t> succ;
  };

  struct CFG {
    vector<BlockInfo> blocks;
    uint32_t entry = 0;
    uint32_t exit = 0;

    uint32_t size() const { return static_cast<uint32_t>(blocks.size()); }
    vector<uint32_t> predecessors(uint32_t id) const {
      vector<uint32_t> preds;
      for (auto &b: blocks)
        for (auto s: b.succ)
          if (s == id) preds.push_back(b.id);
      return preds;
    }
  };

  struct FunctionAnalysisData {
    ValueSet alloca_names;
    llvm::DenseMap<const llvm::Value *, const llvm::Value *> load2alloca;
    llvm::DenseMap<const llvm::Value *, const llvm::Value *> def2alloca;
    vector<ValueSet> block_expressions;
    ValueSet all_expressions;
    vector<ValueSet> block_defs;
  };

  class AnalysisCtxt {
public:
    explicit AnalysisCtxt(const llvm::Module &module);

    const unordered_map<string, CFG> &get_cfgs() const { return cfgs; }
    const unordered_map<string, uptr<FunctionAnalysisData>> &get_func_data() const { return func_data; }

private:
    CFG build_cfg(const llvm::Function &func, FunctionAnalysisData &fdata);
    void extract_block_def_use(const llvm::BasicBlock &bb, unordered_map<const llvm::BasicBlock *, uint32_t> &bb2id, ValueSet &def, ValueSet &use);
    vector<uint32_t> get_successor_indices(const llvm::BasicBlock &bb, const unordered_map<const llvm::BasicBlock *, uint32_t> &bb2id);

    unordered_map<string, CFG> cfgs;
    unordered_map<string, uptr<FunctionAnalysisData>> func_data;
  };

}// namespace cat::opt::ana
