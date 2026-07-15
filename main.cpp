#include "./src/frontend/lexer/lexer.h"
#include <iostream>

int main() {
  std::string source = R"cat(
let x = 42 + 3.14;
def foo(a: int) -> int { return a * 2; }
"hello" 'c' true false
<= >= == != -> ++ --
)cat";

  std::cout << "=== range-for ===\n";
  cat::Lexer lexer(source);
  for (const auto &tok: lexer) {
    tok.print(std::cout);
  }

  std::cout << "\n=== streaming next_token() ===\n";
  cat::Lexer lexer2(source);
  while (true) {
    auto token = lexer2.next_token();
    token.print(std::cout);
    if (token.kind == cat::TokenKind::TokenEOF) break;
  }

  std::cout << "\n=== tokenize() debug ===\n";
  cat::Lexer lexer3(source);
  auto tokens = lexer3.tokenize();
  std::cout << "collected " << tokens.size() << " tokens\n";

  return 0;
}
