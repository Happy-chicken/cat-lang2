// #pragma once

// #include "common.h"
// #include "analysis_ctx.h"          
// #include "dataflow_slover.h"     

// namespace cat::opt::ana {

// using ExprSet = std::set<std::string>;

// // 辅助函数：检查表达式是否使用了 def 集合中的任何变量
// static bool expr_uses_any_def(const std::string& expr, const std::set<std::string>& def) {
//     auto pos = expr.find('(');
//     if (pos == std::string::npos) return false;
//     // 提取括号内的参数部分，例如 "op(a,b)" -> "a,b"
//     std::string inner = expr.substr(pos + 1, expr.size() - pos - 2); // 去掉括号
//     // 按逗号分割
//     size_t start = 0;
//     while (start < inner.size()) {
//         size_t end = inner.find(',', start);
//         std::string var = (end == std::string::npos) ? inner.substr(start) : inner.substr(start, end - start);
//         // 去除可能的空格
//         var.erase(0, var.find_first_not_of(" \t"));
//         var.erase(var.find_last_not_of(" \t") + 1);
//         if (def.count(var)) return true;
//         if (end == std::string::npos) break;
//         start = end + 1;
//     }
//     return false;
// }

// /// Very Busy Expressions Analysis (C++ version)
// ///
// /// An expression is "very busy" at a program point if it MUST be evaluated
// /// on EVERY path from that point to the exit, and none of its operands have
// /// been redefined since the last evaluation.
// ///
// /// Dataflow equations (backward, intersection):
// ///   out[n] = ⋂ in[s]     for all successors s
// ///   in[n]  = gen[n] ∪ (out[n] − kill[n])
// ///
// /// where:
// ///   gen[n]  = expressions evaluated in n whose operands survive the block
// ///   kill[n] = all expressions whose operands are redefined in n
// class VeryBusyExpressionAnalysis {
// public:
//     using State = ExprSet;

//     VeryBusyExpressionAnalysis(const CFG& cfg, const FunctionAnalysisData& fdata) {
//         size_t n = cfg.blocks.size();
//         gen_map.resize(n);
//         kill_map.resize(n);

//         for (const auto& block : cfg.blocks) {
//             size_t i = block.id;
//             const auto& def = block.def;   // 假设 block.def 是 std::set<std::string> 或类似

//             // 计算 gen：本块求值且操作数在进入块时存活的表达式
//             if (i < fdata.block_expressions.size()) {
//                 ExprSet gen_set;
//                 for (const auto& expr : fdata.block_expressions[i]) {
//                     if (!expr_uses_any_def(expr, def)) {
//                         gen_set.insert(expr);
//                     }
//                 }
//                 gen_map[i] = std::move(gen_set);
//             }

//             // 计算 kill：所有操作数在本块被重定义的表达式
//             ExprSet kill_set;
//             for (const auto& expr : fdata.all_expressions) {
//                 if (expr_uses_any_def(expr, def)) {
//                     kill_set.insert(expr);
//                 }
//             }
//             kill_map[i] = std::move(kill_set);
//         }
//     }

//     Direction direction() const { return Direction::Backward; }

//     State initial_state() const { return {}; }

//     State boundary_state() const { return {}; }

//     // transfer: 根据后继输出（output）计算块入口输入（input）
//     State transfer(const BlockInfo& block, const State& output) const {
//         State input = output;
//         // 先移出 kill
//         for (const auto& e : kill_map[block.id]) {
//             input.erase(e);
//         }
//         // 再并入 gen
//         for (const auto& e : gen_map[block.id]) {
//             input.insert(e);
//         }
//         return input;
//     }

//     // meet: 对后继状态取交集（因为是非常忙表达式，必须所有后继都有）
//     State meet(const std::vector<State>& states) const {
//         if (states.empty()) return {};
//         State result = states[0];
//         for (size_t i = 1; i < states.size(); ++i) {
//             // 原地交集
//             State intersection;
//             for (const auto& e : result) {
//                 if (states[i].count(e)) {
//                     intersection.insert(e);
//                 }
//             }
//             result.swap(intersection);
//             if (result.empty()) break; // 早期剪枝
//         }
//         return result;
//     }

// private:
//     std::vector<State> gen_map;
//     std::vector<State> kill_map;
// };

// /// 便捷函数：直接计算所有基本块的入口（in）非常忙表达式集合
// inline std::vector<ExprSet> compute_very_busy_expressions(
//     const CFG& cfg,
//     const FunctionAnalysisData& fdata) 
// {
//     VeryBusyExpressionAnalysis analysis(cfg, fdata);
//     DataflowSolver<VeryBusyExpressionAnalysis> solver(cfg, analysis);
//     return solver.solve();
// }

// } // namespace cat::opt::ana