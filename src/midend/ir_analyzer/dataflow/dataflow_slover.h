#pragma once
#include "analysis_ctx.h"
#include <deque>
namespace cat::opt::ana {

enum class Direction { Forward, Backward };

template <typename Analysis> class DataflowSolver {
public:
  using State = typename Analysis::State;

  DataflowSolver(const CFG &cfg, const Analysis &analysis)
      : cfg(cfg), analysis(analysis) {}

  vector<State> solve() {
    auto n = cfg.size();
    auto entry = cfg.entry;
    auto exit = cfg.exit;

    vector<State> in_states(n, analysis.initial_state());
    vector<State> out_states(n, analysis.initial_state());
    auto boundary = analysis.boundary_state();

    if (analysis.direction() == Direction::Forward) {
      in_states[entry] = boundary;
    } else {
      out_states[exit] = boundary;
    }

    std::deque<uint32_t> worklist;
    for (uint32_t i = 0; i < n; ++i)
      worklist.push_back(i);

    while (!worklist.empty()) {
      auto id = worklist.front();
      worklist.pop_front();
      auto &block = cfg.blocks[id];

      switch (analysis.direction()) {
      case Direction::Forward: {
        auto preds = cfg.predecessors(id);
        State new_in =
            (id == entry) ? boundary : initial_or_meet(preds, out_states);
        State new_out = analysis.transfer(block, new_in);
        if (new_out != out_states[id]) {
          out_states[id] = std::move(new_out);
          for (auto s : block.succ)
            push_unique(worklist, s);
        }
        break;
      }
      case Direction::Backward: {
        const auto &succs = block.succ;
        State new_out =
            (id == exit) ? boundary : initial_or_meet(succs, in_states);
        State new_in = analysis.transfer(block, new_out);
        if (new_in != in_states[id]) {
          in_states[id] = std::move(new_in);
          for (auto p : cfg.predecessors(id))
            push_unique(worklist, p);
        }
        break;
      }
      }
    }

    return analysis.direction() == Direction::Forward ? out_states : in_states;
  }

private:
  State initial_or_meet(const vector<uint32_t> &indices,
                        const vector<State> &states) const {
    if (indices.empty())
      return analysis.initial_state();
    vector<State> to_meet;
    to_meet.reserve(indices.size());
    for (auto i : indices)
      to_meet.push_back(states[i]);
    return analysis.meet(to_meet);
  }

  void push_unique(std::deque<uint32_t> &q, uint32_t id) {
    for (auto x : q)
      if (x == id)
        return;
    q.push_back(id);
  }

  const CFG &cfg;
  const Analysis &analysis;
};

} // namespace cat::opt::ana
