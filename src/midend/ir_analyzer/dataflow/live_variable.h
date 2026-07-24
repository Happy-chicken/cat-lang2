#pragma once
#include "analysis_ctx.h"
#include "dataflow_slover.h"

namespace cat::opt::ana {

class LiveVariableAnalysis {
public:
  using State = ValueSet;

  Direction direction() const { return Direction::Backward; }

  State initial_state() const { return {}; }

  State boundary_state() const { return {}; }

  State transfer(const BlockInfo &block, const State &output) const {
    State input = output;
    for (auto *v : block.def)
      input.erase(v);
    for (auto *v : block.use)
      input.insert(v);
    return input;
  }

  State meet(const vector<State> &states) const {
    State result;
    for (auto &s : states)
      for (auto *v : s)
        result.insert(v);
    return result;
  }
};

inline vector<ValueSet> compute_live_variables(const CFG &cfg) {
  LiveVariableAnalysis lv;
  DataflowSolver<LiveVariableAnalysis> solver(cfg, lv);
  return solver.solve();
}

} // namespace cat::opt::ana
