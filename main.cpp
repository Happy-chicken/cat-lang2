#include "./src/frontend/sema_checker/pass_manager.h"
#include "./src/midend/ast_optimizer/pass_manager.h"
#include "algebraic_simplifier.h"
#include "analysis_ctx.h"
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
#include "parser.h"
#include "printer.h"
#include "resolver.h"
#include "sema_checker.h"
#include "type_checker.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

namespace fs = std::filesystem;

static std::string read_file(const std::string &path) {
  std::ifstream f(path);
  if (!f) return "";
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}

static int run_file(const std::string &path) {
  std::string source = read_file(path);
  if (source.empty()) {
    std::cerr << "Cannot read file: " << path << std::endl;
    return 1;
  }

  cat::File file(path, source);
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
    std::cerr << "\n--- Errors in " << path << " ---\n";
    diag_ctxt.print_all(std::cerr);
    return 1;
  }

  cat::opt::ast::PassManager ast_opt_pm;
  ast_opt_pm.add_pass<cat::opt::ast::Canonicalization>();
  ast_opt_pm.add_pass<cat::opt::ast::AlgebraicSimplifier>();
  ast_opt_pm.add_pass<cat::opt::ast::ConstantFolder>();
  ast_opt_pm.add_pass<cat::opt::ast::BooleanSimplifier>();
  ast_opt_pm.add_pass<cat::opt::ast::DeadBranch>();
  ast_opt_pm.add_pass<cat::opt::ast::BlockSimplifier>();
  ast_opt_pm.run(program);

  cat::ir::IrEmitter emitter(path, diag_ctxt, sema_pm.get_sema_ctxt());
  emitter.compile(program);

  cat::jit::JIT jit(diag_ctxt);
  jit.add_symbol("malloc", reinterpret_cast<void *>(&malloc));
  jit.add_module(emitter);

  int result = jit.run();
  std::cout << path << " -> " << result << std::endl;
  return 0;
}

static void run() {
  std::string source = R"(
    def main()->int {
      let a = 1;
      let b = 2;
      return a;
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

  cat::opt::ana::AnalysisCtxt analysis_ctx(emitter.get_module());
  const auto &cfgs = analysis_ctx.get_cfgs();
  for (const auto &[fn_name, cfg]: cfgs) {
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

  cat::jit::JIT jit(diag_ctxt);
  jit.add_symbol("malloc", reinterpret_cast<void *>(&malloc));
  jit.add_module(emitter);

  int result = jit.run();
  std::cout << " -> " << result << std::endl;
}

int main(int argc, char **argv) {
  if (argc > 1) {
    for (int i = 1; i < argc; ++i) {
      std::string arg = argv[i];
      if (arg.ends_with(".cat")) {
        int rc = run_file(arg);
        if (rc != 0) return rc;
      }
    }
    return 0;
  }

  std::string test_dir = "test";
  bool found = false;
  std::vector<std::string> files;
  if (fs::exists(test_dir) && fs::is_directory(test_dir)) {
    for (const auto &entry : fs::directory_iterator(test_dir)) {
      if (entry.path().extension() == ".cat")
        files.push_back(entry.path().string());
    }
  }
  std::sort(files.begin(), files.end());

  int passed = 0, failed = 0;
  for (const auto &f : files) {
    found = true;
    int rc = run_file(f);
    if (rc == 0) ++passed;
    else ++failed;
    std::cout << std::endl;
  }

  if (!found) {
    run();
    return 0;
  }

  std::cout << "--- " << passed << " passed, " << failed << " failed ---" << std::endl;
  return failed > 0 ? 1 : 0;
}
