#pragma once

#include "analysis_ctx.h"
#include "dataflow_slover.h"

namespace cat::opt::ana {

class ReachingDefinitionAnalysis {
public:
  using State = ValueSet;

  ReachingDefinitionAnalysis(const CFG &cfg,
                             const FunctionAnalysisData &fdata) {
    size_t n = cfg.blocks.size();
    size_t ndef = fdata.block_defs.size();
    gen_map.resize(n);
    kill_map.resize(n);

    llvm::DenseMap<const llvm::Value *, ValueSet> alloca_to_defs;

    for (size_t i = 0; i < ndef; ++i) {
      for (auto *def : fdata.block_defs[i]) {
        auto it = fdata.def2alloca.find(def);
        if (it != fdata.def2alloca.end())
          alloca_to_defs[it->second].insert(def);
      }
    }

    for (size_t i = 0; i < ndef; ++i) {
      gen_map[i] = fdata.block_defs[i];

      for (auto *def : fdata.block_defs[i]) {
        auto it = fdata.def2alloca.find(def);
        if (it != fdata.def2alloca.end()) {
          const auto &all_defs = alloca_to_defs[it->second];
          kill_map[i].insert(all_defs.begin(), all_defs.end());
        }
      }
    }
  }

  Direction direction() const { return Direction::Forward; }

  State initial_state() const { return {}; }

  State boundary_state() const { return {}; }

  State transfer(const BlockInfo &block, const State &input) const {
    State output = input;
    for (auto *e : kill_map[block.id]) {
      output.erase(e);
    }
    for (auto *e : gen_map[block.id]) {
      output.insert(e);
    }
    return output;
  }

  State meet(const vector<State> &states) const {
    State result;
    for (auto &s : states)
      for (auto *v : s)
        result.insert(v);
    return result;
  }

private:
  vector<State> gen_map;
  vector<State> kill_map;
};

inline vector<ValueSet>
compute_reaching_definitions(const CFG &cfg,
                             const FunctionAnalysisData &fdata) {
  ReachingDefinitionAnalysis analysis(cfg, fdata);
  DataflowSolver<ReachingDefinitionAnalysis> solver(cfg, analysis);
  return solver.solve();
}

} // namespace cat::opt::ana
