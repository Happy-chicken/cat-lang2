#pragma once
#include "common.h"
#include <llvm-20/llvm/ADT/DenseMap.h>
#include <llvm-20/llvm/IR/BasicBlock.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/Instruction.h>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/IR/Value.h>
#include <set>
#include <unordered_set>

namespace cat::opt::ana {

  struct ConstValuePtrHash {
    size_t operator()(const llvm::Value *v) const noexcept {
      return reinterpret_cast<size_t>(v);
    }
  };

  using ValueSet = std::unordered_set<const llvm::Value *, ConstValuePtrHash>;
  using ExprSet = std::set<std::string>;

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
    std::set<string> alloca_names;
    unordered_map<string, string> load2alloca;
    vector<ExprSet> block_expressions;
    vector<ExprSet> all_expressions;
  };

  class AnalysisCtxt {
public:
    explicit AnalysisCtxt(const llvm::Module &module);

    const unordered_map<string, CFG> &get_cfgs() const { return cfgs; }

private:
    CFG build_cfg(const llvm::Function &func);
    void extract_block_def_use(const llvm::BasicBlock &bb, unordered_map<const llvm::BasicBlock *, uint32_t> &bb2id, ValueSet &def, ValueSet &use);
    vector<uint32_t> get_successor_indices(const llvm::BasicBlock &bb, const unordered_map<const llvm::BasicBlock *, uint32_t> &bb2id);

    unordered_map<string, CFG> cfgs;
    unordered_map<string, FunctionAnalysisData*> func_data;
  };

}// namespace cat::opt::ana
