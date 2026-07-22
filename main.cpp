#include "./src/frontend/sema_checker/pass_manager.h"
#include "./src/midend/ast_optimizer/pass_manager.h"
#include "algebraic_simplifier.h"
#include "analysis_ctx.h"
#include "andersen_solver.h"
#include "block_simplifier.h"
#include "boolean_simplifier.h"
#include "canonicalization.h"
#include "constant_folder.h"
#include "dead_brach.h"
#include "diag.h"
#include "file.h"
#include "flow_checker.h"
#include "ir_emitter.h"
#include "jit.h"
#include "lexer.h"
#include "live_variable.h"
#include "mlir_emitter.h"
#include "parser.h"
#include "printer.h"
#include "reaching_definition.h"
#include "resolver.h"
#include "sema_checker.h"
#include "type_checker.h"
#include "very_busy_expression.h"
#include <cstdlib>
#include <iostream>

static void run() {
  std::string source = R"(
    class Node {
      let val: int = 0;
      let next: int = 0;
    }

    def inc(x: ref<int>) {
      x = x + 1;
    }

    def swap(a: ref<int>, b: ref<int>) {
      let tmp = a;
      a = b;
      b = tmp;
    }

    def main()->int {
      let a = 10;
      let b = 20;
      inc(a);
      swap(a, b);
      let n = Node(42, 7);
      return a + n.val;
    }
  )";

  cat::File file("foo.cat", source);
  cat::error::DiagCtxt diag_ctxt;
  diag_ctxt.add_file(std::move(file));

  cat::Lexer lexer(source);
  cat::Parser parser(lexer, diag_ctxt);
  auto program = parser.parse_program();

  cat::semantics::PassManager sema_pm;
  sema_pm.add_pass(std::make_unique<cat::Resolver>());
  sema_pm.add_pass(std::make_unique<cat::SemaChecker>());
  sema_pm.add_pass(std::make_unique<cat::FlowChecker>());
  sema_pm.add_pass(std::make_unique<cat::semantics::TypeChecker>());
  sema_pm.run(program, diag_ctxt);

  if (diag_ctxt.has_errors()) {
    diag_ctxt.print_all(std::cerr);
  }

  cat::opt::ast::PassManager ast_opt_pm;
  ast_opt_pm.add_pass<cat::opt::ast::Canonicalization>();
  ast_opt_pm.add_pass<cat::opt::ast::AlgebraicSimplifier>();
  ast_opt_pm.add_pass<cat::opt::ast::ConstantFolder>();
  ast_opt_pm.add_pass<cat::opt::ast::BooleanSimplifier>();
  ast_opt_pm.add_pass<cat::opt::ast::DeadBranch>();
  ast_opt_pm.add_pass<cat::opt::ast::BlockSimplifier>();
  ast_opt_pm.run(program);

  cat::ir::IrEmitter emitter(file.name(), diag_ctxt, sema_pm.get_sema_ctxt());
  emitter.compile(program);
  emitter.dump_module(std::cout);

  // cat::mmlir::MlirEmitter mlir_emitter(file.name(), diag_ctxt, sema_pm.get_sema_ctxt());
  // mlir_emitter.compile(program);
  // mlir_emitter.dump_module(std::cout);

  cat::opt::ana::AnalysisCtxt analysis_ctx(emitter.get_module());
  const auto &cfgs = analysis_ctx.get_cfgs();

  std::cout << "\n--- Live variables ---\n";
  for (const auto &[fn_name, cfg]: cfgs) {
    if (cfg.blocks.empty()) continue;
    auto live_in = cat::opt::ana::compute_live_variables(cfg);
    for (auto &b: cfg.blocks) {
      for (auto *v: b.def) {
        bool ever_live = false;
        for (auto &li: live_in)
          if (li.count(v)) {
            ever_live = true;
            break;
          }
        if (!ever_live && v->hasName())
          std::cout << "  unused `" << v->getName().str() << "` in " << fn_name << "\n";
      }
    }
  }

  std::cout << "\n--- Very busy expressions ---\n";
  for (const auto &[fn_name, cfg]: cfgs) {
    if (cfg.blocks.empty()) continue;
    auto very_busy = cat::opt::ana::compute_very_busy_expressions(cfg, *analysis_ctx.get_func_data().at(fn_name));
    std::cout << fn_name << ":\n";
    for (auto &b: cfg.blocks) {
      if (b.id == cfg.exit) continue;
      const auto &vbe = very_busy[b.id];
      bool first = true;
      std::string str;
      for (auto *e: vbe) {
        if (!e->hasName()) continue;
        if (!first) str += ", ";
        str += e->getName().str();
        first = false;
      }
      if (!str.empty())
        std::cout << "  block[" << b.id << "] very busy: {" << str << "}\n";
    }
  }

  std::cout << "\n--- Reaching definitions ---\n";
  for (const auto &[fn_name, cfg]: cfgs) {
    if (cfg.blocks.empty()) continue;
    auto reaching_defs = cat::opt::ana::compute_reaching_definitions(cfg, *analysis_ctx.get_func_data().at(fn_name));
    std::cout << fn_name << ":\n";
    for (auto &b: cfg.blocks) {
      if (b.id == cfg.exit) continue;
      const auto &rd = reaching_defs[b.id];
      bool first = true;
      std::string str;
      for (auto *d: rd) {
        if (!d->hasName()) continue;
        if (!first) str += ", ";
        str += d->getName().str();
        first = false;
      }
      if (!str.empty())
        std::cout << "  block[" << b.id << "] reaching defs: {" << str << "}\n";
    }
  }

  std::cout << "\n--- Andersen points-to ---\n";
  for (const auto &func: emitter.get_module()) {
    if (func.getName().starts_with("llvm.")) continue;
    auto andersen = cat::opt::ana::compute_andersen(func);
    std::cout << func.getName().str() << ":\n";
    std::string s;
    llvm::raw_string_ostream os(s);
    andersen->dump(os);
    std::cout << s;
  }

  cat::jit::JIT jit(diag_ctxt);
  jit.add_symbol("malloc", reinterpret_cast<void *>(&malloc));
  jit.add_module(emitter);

  int result = jit.run();
  std::cout << "result -> " << result << std::endl;
}

int main(int argc, char **argv) {
  run();
  return EXIT_SUCCESS;
}
