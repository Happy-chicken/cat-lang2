#include "diag.h"
#include "file.h"
#include "printer.h"
#include "lexer.h"
#include "parser.h"
#include "./src/frontend/sema_checker/pass_manager.h"
#include "sema_checker.h"
#include "flow_checker.h"
#include "resolver.h"
#include "type_checker.h"
#include "algebraic_simplifier.h"
#include "block_simplifier.h"
#include "boolean_simplifier.h"
#include "canonicalization.h"
#include "constant_folder.h"
#include "dead_brach.h"
#include "./src/midend/ast_optimizer/pass_manager.h"
#include <iostream>

int main() {
  std::string source = R"cat(
let pi:str = -3.14;

def add(a: int, b: int) -> int {
  pi = 3.14;
  return a + b;
}

def test(x: int) -> bool {
  let result = x > 0 and x < 10;
  if result {
    return true;
  } else {
    return false;
  }
}

class Point {
  let x: int;
  let y: int = 0;
}

trait Printable {
  decl print(self: ptr<str>);
}

impl Printable for Point {
  def print(self: Point) -> str {
    return "Point";
  }
}
)cat";

  cat::File file("example.cat", source);
  cat::error::DiagCtxt diag_ctxt;
  diag_ctxt.add_file(std::move(file));

  cat::Lexer lexer(source);
  cat::Parser parser(lexer, diag_ctxt);
  auto program = parser.parse_program();
  cat::ast::print(std::cout, program);

  cat::semantics::PassManager sema_pm;
  sema_pm.add_pass(std::make_unique<cat::Resolver>());
  sema_pm.add_pass(std::make_unique<cat::SemaChecker>());
  sema_pm.add_pass(std::make_unique<cat::FlowChecker>());
  sema_pm.add_pass(std::make_unique<cat::semantics::TypeChecker>());
  sema_pm.run(program, diag_ctxt);

  if (diag_ctxt.has_errors()) {
    std::cerr << "\n--- Errors ---\n";
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

  std::cout << "\n--- Optimized AST ---\n";
  cat::ast::print(std::cout, program);

  return 0;
}
