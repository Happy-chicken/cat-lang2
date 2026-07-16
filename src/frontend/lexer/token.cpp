#include "token.h"
namespace cat {

  void Token::print(ostream &out) const {
    out << tokenkind_to_string(kind);
    if (!lexeme.empty()) out << " '" << lexeme << "'";
    out << '\n';
  }

}// namespace cat
