#pragma once

#include <llvm-20/llvm/IR/User.h>
#include "analysis_ctx.h"
#include "dataflow_slover.h"

namespace cat::opt::ana {

static bool expr_uses_any_def(const llvm::Value *expr, const ValueSet &def) {
    const auto *user = llvm::dyn_cast<llvm::User>(expr);
    if (!user) return false;
    for (auto &op : user->operands()) {
        if (def.count(op.get())) return true;
    }
    return false;
}

/// Very Busy Expressions Analysis
///
/// An expression is "very busy" at a program point if it MUST be evaluated
/// on EVERY path from that point to the exit, and none of its operands have
/// been redefined since the last evaluation.
///
/// This is the backward dual of Available Expressions.
///
/// Dataflow equations (backward, intersection):
///   out[n] = ⋂ in[s]     for all successors s
///   in[n]  = gen[n] ∪ (out[n] − kill[n])
///
/// where:
///   gen[n]  = expressions evaluated in n whose operands survive the block
///   kill[n] = all expressions whose operands are redefined in n
///
/// gen/kill are pre-computed from the shared AnalysisContext (one LLVM IR scan).
class VeryBusyExpressionAnalysis {
public:
    using State = ValueSet;

    VeryBusyExpressionAnalysis(const CFG &cfg, const FunctionAnalysisData &fdata) {
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
        if (states.empty()) return {};

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
            if (in_all) result.insert(v);
        }
        return result;
    }

private:
    vector<State> gen_map;
    vector<State> kill_map;
};

inline vector<ValueSet> compute_very_busy_expressions(
    const CFG &cfg,
    const FunctionAnalysisData &fdata)
{
    VeryBusyExpressionAnalysis analysis(cfg, fdata);
    DataflowSolver<VeryBusyExpressionAnalysis> solver(cfg, analysis);
    return solver.solve();
}

} // namespace cat::opt::ana
