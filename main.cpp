#include "./src/common/diag.h"
#include "./src/common/file.h"
#include "./src/frontend/ast/printer.h"
#include "./src/frontend/lexer/lexer.h"
#include "./src/frontend/parser/parser.h"
#include <iostream>

int main() {
  std::string source = R"cat(
let pi = -3.14;

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
  def print(self: ptr<str>) -> str {
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

  if (diag_ctxt.has_errors()) {
    std::cerr << "\n--- Parse Errors ---\n";
    diag_ctxt.print_all(std::cerr);
  }

  return 0;
}
