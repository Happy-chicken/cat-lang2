#pragma once
#include "cfg.h"
#include <deque>
#include <llvm-20/llvm/ADT/DenseMap.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/Instruction.h>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/IR/User.h>
#include <set>

namespace cat::opt::ana {

enum class Direction { Forward, Backward };

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
  explicit AnalysisCtxt(const llvm::Function &func);

  const CFG &get_cfg() const { return cfg; }
  const uptr<FunctionAnalysisData> &get_func_data() const { return func_data; }

private:
  CFG build_cfg(const llvm::Function &func, FunctionAnalysisData &fdata);
  void extract_block_def_use(const llvm::BasicBlock &bb, ValueSet &def,
                             ValueSet &use);
  vector<uint32_t> get_successor_indices(const llvm::BasicBlock &bb,
                                         const vector<BlockInfo> &blocks);

  CFG cfg;
  uptr<FunctionAnalysisData> func_data;
};

// Generic monotone dataflow framework.
//
// The solver solves the following fixed-point equation system:
//
//   Forward:
//     in[n]  = ∧ { out[p] | p ∈ pred(n) }    (or  boundary_state  for entry)
//     out[n] = f_n(in[n])
//
//   Backward:
//     out[n] = ∧ { in[s]  | s ∈ succ(n) }    (or  boundary_state  for exit)
//     in[n]  = f_n(out[n])
//
// where f_n is the transfer function for block n and ∧ is the meet operator.
// The worklist algorithm guarantees termination for finite-height lattices.
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

// ── Live Variable Analysis ───────────────────────────────────────────
//
// A variable v is LIVE at program point p if there exists a path from p to
// exit along which v is used before being redefined.
//
// Direction:  Backward
// Meet:       ∪ (union) — "may be live"
// Lattice:    P(Var), ⊆, lowered by union (larger sets = less precise)
//
//   out[n] = ∪ { in[s]  | s ∈ succ(n) }
//   in[n]  = (out[n] − def[n]) ∪ use[n]
//
//   boundary: out[exit] = ∅
//   initial:  in[n] = out[n] = ∅  (⊥ = empty set)
//
// def[n] — variables defined (killed) in block n
// use[n] — variables used before any definition in block n
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

// ── Reaching Definitions ─────────────────────────────────────────────
//
// A definition d reaches program point p if there exists a path from d to p
// along which the variable is not redefined (killed).
//
// Direction:  Forward
// Meet:       ∪ (union) — "may reach"
// Lattice:    P(Def), ⊆, lowered by union (larger sets = less precise)
//
//   out[n] = gen[n] ∪ (in[n] − kill[n])
//   in[n]  = ∪ { out[p] | p ∈ pred(n) }
//
//   boundary: in[entry] = ∅
//   initial:  in[n] = out[n] = ∅  (⊥ = empty set)
//
// gen[n]  — definitions in block n that reach the end of n
// kill[n] — all definitions (anywhere in the function) of variables defined in
// n:
//           kill[n] = ∪ { defs(v) | v defined in n }
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

static bool expr_uses_any_def(const llvm::Value *expr, const ValueSet &def) {
  const auto *user = llvm::dyn_cast<llvm::User>(expr);
  if (!user)
    return false;
  for (auto &op : user->operands()) {
    if (def.count(op.get()))
      return true;
  }
  return false;
}

// ── Very Busy Expressions ────────────────────────────────────────────
//
// An expression e is VERY BUSY at program point p if e MUST be evaluated on
// EVERY path from p to exit before any of its operands are redefined.
// This is the backward dual of Available Expressions.
//
// Direction:  Backward
// Meet:       ∩ (intersection) — "must be busy"
// Lattice:    P(Expr), ⊇, lowered by intersection (smaller sets = less precise)
//
//   out[n] = ∩ { in[s]  | s ∈ succ(n) }
//   in[n]  = gen[n] ∪ (out[n] − kill[n])
//
//   boundary: out[exit] = ∅
//   initial:  in[n] = out[n] = ∅  (⊤ = empty set, 注意交集格方向反转)
//
// gen[n]  — expressions evaluated in n whose operands survive n (not killed in
// n) kill[n] — expressions whose operands are defined in n
class VeryBusyExpressionAnalysis {
public:
  using State = ValueSet;

  VeryBusyExpressionAnalysis(const CFG &cfg,
                             const FunctionAnalysisData &fdata) {
    size_t n = cfg.blocks.size();
    gen_map.resize(n);
    kill_map.resize(n);

    for (const auto &block : cfg.blocks) {
      size_t i = block.id;
      const auto &def = block.def;

      if (i < fdata.block_expressions.size()) {
        ValueSet gen_set;
        for (const auto *expr : fdata.block_expressions[i]) {
          if (!expr_uses_any_def(expr, def)) {
            gen_set.insert(expr);
          }
        }
        gen_map[i] = std::move(gen_set);
      }

      ValueSet kill_set;
      for (const auto *expr : fdata.all_expressions) {
        if (expr_uses_any_def(expr, def)) {
          kill_set.insert(expr);
        }
      }
      kill_map[i] = std::move(kill_set);
    }
  }

  Direction direction() const { return Direction::Backward; }

  State initial_state() const { return {}; }

  State boundary_state() const { return {}; }

  State transfer(const BlockInfo &block, const State &output) const {
    State input = output;
    for (const auto *e : kill_map[block.id]) {
      input.erase(e);
    }
    for (const auto *e : gen_map[block.id]) {
      input.insert(e);
    }
    return input;
  }

  State meet(const vector<State> &states) const {
    if (states.empty())
      return {};

    const State *smallest = &states[0];
    for (size_t i = 1; i < states.size(); ++i) {
      if (states[i].size() < smallest->size())
        smallest = &states[i];
    }

    State result;
    for (const auto *v : *smallest) {
      bool in_all = true;
      for (const auto &s : states) {
        if (&s != smallest && !s.count(v)) {
          in_all = false;
          break;
        }
      }
      if (in_all)
        result.insert(v);
    }
    return result;
  }

private:
  vector<State> gen_map;
  vector<State> kill_map;
};

inline vector<ValueSet>
compute_very_busy_expressions(const CFG &cfg,
                              const FunctionAnalysisData &fdata) {
  VeryBusyExpressionAnalysis analysis(cfg, fdata);
  DataflowSolver<VeryBusyExpressionAnalysis> solver(cfg, analysis);
  return solver.solve();
}

// ── Dominance: dataflow-based (educational) ──────────────────────────
//
// Dominator set equation (forward, ∩):
//   in[n]  = ∩ { out[p] | p ∈ pred(n) }
//   out[n] = in[n] ∪ {n}
//   boundary: in[entry] = {entry}
//
// Uses the generic DataflowSolver.  For production use, prefer
// compute_idoms_fast() (Cooper-Harvey-Kennedy algorithm, O(N) in practice).

struct DomState {
  std::set<uint32_t> doms;
  bool operator==(const DomState &o) const { return doms == o.doms; }
  bool operator!=(const DomState &o) const { return !(*this == o); }
};

class DominatorAnalysis {
public:
  using State = DomState;

  explicit DominatorAnalysis(const CFG &cfg) : cfg(cfg) {}

  Direction direction() const { return Direction::Forward; }

  State initial_state() const {
    State s;
    for (uint32_t i = 0; i < cfg.size(); ++i)
      s.doms.insert(i);
    return s;
  }

  State boundary_state() const {
    State s;
    s.doms.insert(cfg.entry);
    return s;
  }

  State transfer(const BlockInfo &block, const State &input) const {
    State out = input;
    out.doms.insert(block.id);
    return out;
  }

  State meet(const vector<State> &states) const {
    if (states.empty())
      return initial_state();
    State result = states[0];
    for (size_t i = 1; i < states.size(); ++i) {
      State inter;
      for (auto d : result.doms)
        if (states[i].doms.count(d))
          inter.doms.insert(d);
      result = std::move(inter);
    }
    return result;
  }

private:
  const CFG &cfg;
};

inline vector<DomState> compute_dominators(const CFG &cfg) {
  DominatorAnalysis dom_analysis(cfg);
  DataflowSolver<DominatorAnalysis> solver(cfg, dom_analysis);
  return solver.solve();
}

} // namespace cat::opt::ana
