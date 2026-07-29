#include "./src/frontend/sema_checker/pass_manager.h"
#include "./src/midend/ast_optimizer/pass_manager.h"
#include "algebraic_simplifier.h"
#include "aot.h"
#include "block_simplifier.h"
#include "boolean_simplifier.h"
#include "borrow_checker.h"
#include "canonicalization.h"
#include "cat_coptimizer.h"
#include "constant_folder.h"
#include "dead_brach.h"
#include "diag.h"
#include "file.h"
#include "flow_checker.h"
#include "ir_emitter.h"
#include "jit.h"
#include "lexer.h"
#include "llvm_optimizer.h"
#include "parser.h"
#include "resolver.h"
#include "sema_checker.h"
#include "type_checker.h"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm/Support/CommandLine.h>

namespace cl = llvm::cl;

static cl::OptionCategory catlang_opts("catlang");

static cl::SubCommand run_cmd("run", "JIT compile and execute a .cat file");
static cl::SubCommand build_cmd("build",
                               "AOT compile a .cat file to native executable");

static cl::opt<std::string> run_input(cl::Positional, cl::init("main.cat"),
                                     cl::desc("<input.cat>"),
                                     cl::cat(catlang_opts), cl::sub(run_cmd));

static cl::opt<std::string> build_input(cl::Positional, cl::init("main.cat"),
                                       cl::desc("<input.cat>"),
                                       cl::cat(catlang_opts), cl::sub(build_cmd));

static cl::opt<std::string>
    build_output("o", cl::desc("Output executable path (default: ./a.out)"),
                cl::init("./a.out"), cl::cat(catlang_opts), cl::sub(build_cmd));

static cl::opt<bool> dump_ir("ir", cl::desc("Dump LLVM IR to out/ir.ll"),
                            cl::init(false), cl::cat(catlang_opts),
                            cl::sub(run_cmd), cl::sub(build_cmd));

static std::string read_file(const std::string &path) {
  std::ifstream ifs(path);
  if (!ifs)
    return {};
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

static void dump_module_to_file(cat::ir::IrEmitter &emitter) {
  std::filesystem::create_directories("out");
  std::ofstream ofs("out/ir.ll");
  if (ofs)
    emitter.dump_module(ofs);
  else
    std::cerr << "Failed to write out/ir.ll" << std::endl;
}

// Run lex→parse→sema→optimize→IR emit on source.
// Returns true on success.  After this call `emitter` is ready.
static bool compile_pipeline(const std::string &source,
                             const std::string &filename,
                             cat::ir::IrEmitter &emitter,
                             cat::semantics::PassManager &sema_pm,
                             cat::error::DiagCtxt &diag_ctxt) {
  cat::File file(filename, source);
  diag_ctxt.add_file(std::move(file));

  cat::Lexer lexer(source);
  cat::Parser parser(lexer, diag_ctxt);
  auto program = parser.parse_program();

  if (diag_ctxt.has_errors()) {
    diag_ctxt.print_all(std::cerr);
    return false;
  }

  sema_pm.add_pass(std::make_unique<cat::Resolver>());
  sema_pm.add_pass(std::make_unique<cat::SemaChecker>());
  sema_pm.add_pass(std::make_unique<cat::FlowChecker>());
  sema_pm.add_pass(std::make_unique<cat::BorrowChecker>());
  sema_pm.add_pass(std::make_unique<cat::semantics::TypeChecker>());
  sema_pm.run(program, diag_ctxt);

  if (diag_ctxt.has_errors()) {
    diag_ctxt.print_all(std::cerr);
    return false;
  }

  cat::opt::ast::PassManager ast_opt_pm;
  ast_opt_pm.add_pass<cat::opt::ast::Canonicalization>();
  ast_opt_pm.add_pass<cat::opt::ast::AlgebraicSimplifier>();
  ast_opt_pm.add_pass<cat::opt::ast::ConstantFolder>();
  ast_opt_pm.add_pass<cat::opt::ast::BooleanSimplifier>();
  ast_opt_pm.add_pass<cat::opt::ast::DeadBranch>();
  ast_opt_pm.add_pass<cat::opt::ast::BlockSimplifier>();
  ast_opt_pm.run(program);

  emitter.compile(program);

  // cat::opt::LLVMOptimizer llvm_opt;
  // llvm_opt.optimize(const_cast<llvm::Module &>(emitter.get_module()));
  cat::opt::CatOptimizer cat_opt;
  cat_opt.optimize(const_cast<llvm::Module &>(emitter.get_module()));

  if (dump_ir)
    dump_module_to_file(emitter);

  return true;
}

static int run(const std::string &input) {
  std::string source = read_file(input);
  if (source.empty()) {
    std::cerr << "Error: cannot read " << input << std::endl;
    return 1;
  }

  cat::error::DiagCtxt diag_ctxt;
  cat::semantics::PassManager sema_pm;
  cat::ir::IrEmitter emitter(input, diag_ctxt, sema_pm.get_sema_ctxt());

  if (!compile_pipeline(source, input, emitter, sema_pm, diag_ctxt))
    return 1;

  cat::jit::JIT jit(diag_ctxt);
  jit.add_module(emitter);
  if (diag_ctxt.has_errors()) {
    diag_ctxt.print_all(std::cerr);
    return 1;
  }
  int rc = jit.run();
  fflush(stdout);
  return rc;
}

static int build(const std::string &input, const std::string &output) {
  std::string source = read_file(input);
  if (source.empty()) {
    std::cerr << "Error: cannot read " << input << std::endl;
    return 1;
  }

  cat::error::DiagCtxt diag_ctxt;
  cat::semantics::PassManager sema_pm;
  cat::ir::IrEmitter emitter(input, diag_ctxt, sema_pm.get_sema_ctxt());

  if (!compile_pipeline(source, input, emitter, sema_pm, diag_ctxt))
    return 1;

  std::string obj = output + ".o";
  if (!cat::aot::AOTCompiler::compile(
          const_cast<llvm::Module &>(emitter.get_module()), obj, output))
    return 1;

  std::cout << "Built: " << output << std::endl;
  return 0;
}

int main(int argc, char **argv) {
  cl::HideUnrelatedOptions(catlang_opts);
  cl::ParseCommandLineOptions(argc, argv, "cat-lang compiler\n");

  if (run_cmd) {
    int rc = run(run_input);
    fflush(stdout);
    _Exit(rc >= 0 ? rc : EXIT_FAILURE);
  }

  if (build_cmd) {
    int rc = build(build_input, build_output);
    _Exit(rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE);
  }

  cl::PrintHelpMessage();
  _Exit(EXIT_SUCCESS);
}
