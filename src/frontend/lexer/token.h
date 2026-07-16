#pragma once
#include "../../common/common.h"
#include "../../common/span.h"
namespace cat {
enum class TokenKind {
  LeftParen,  // (
  RightParen, // )
  LeftBrace,  // {
  RightBrace, // }
  Comma,      // ,
  Dot,        // .
  Minus,      //-
  Plus,       //=
  Caret,      // ^
  Semicolon,  // ;
  Slash,      // /
  Star,       // *
  Backslash,  // (\)
  Modulo,     // %
  Colon,      // :
  BitwiseAnd, // &

  // One or two character tokens.
  RightBracket, // ]
  LeftBracket,  // [
  Bang,         // !
  BangEqual,    // !=
  Equal,        // =
  EqualEqual,   //==
  Greater,      // >
  GreaterEqual, //>=
  Less,         //<
  LessEqual,    //<=
  Arrow,        // ->
  MinusMinus,   //--
  PlusPlus,     //++

  // Literals.
  Identifier,
  CharLiteral,
  StringLiteral,
  IntLiteral,
  FloatLiteral,

  // Keywords.
  // logic operators
  And,
  Or,
  True,
  False,
  // branch
  If,
  Else,
  Elif,
  // declare
  Decl,
  Def,
  Let,
  // return
  Return,
  // none
  None,
  // refernce
  Ref,
  // pointer
  Ptr,
  // class
  Class,
  Super,
  Sself,
  // New,
  Trait,
  Impl,
  For,

  // loop
  While,
  // control flow
  Break,
  Continue,
  // type
  Int,
  Float,
  Bool,
  Char,
  Str,
  // list
  List,

  TokenEOF,
  Error,
};
struct Token {
  TokenKind kind;
  string lexeme;
  Span span;

  Token(TokenKind kind, string lexeme, Span span)
      : kind(kind), lexeme(std::move(lexeme)), span(span) {}
  Token() : kind(TokenKind::Error), lexeme(""), span(Span{0, 0}) {}
  void print(ostream &out) const;
};

[[nodiscard]] constexpr const char *
tokenkind_to_string(TokenKind kind) noexcept {
  using enum TokenKind;
  switch (kind) {
  case LeftParen:    return "LeftParen";
  case RightParen:   return "RightParen";
  case LeftBrace:    return "LeftBrace";
  case RightBrace:   return "RightBrace";
  case Comma:        return "Comma";
  case Dot:          return "Dot";
  case Minus:        return "Minus";
  case Plus:         return "Plus";
  case Caret:        return "Caret";
  case Semicolon:    return "Semicolon";
  case Slash:        return "Slash";
  case Star:         return "Star";
  case Backslash:    return "Backslash";
  case Modulo:       return "Modulo";
  case Colon:        return "Colon";
  case BitwiseAnd:   return "BitwiseAnd";
  case RightBracket: return "RightBracket";
  case LeftBracket:  return "LeftBracket";
  case Bang:         return "Bang";
  case BangEqual:    return "BangEqual";
  case Equal:        return "Equal";
  case EqualEqual:   return "EqualEqual";
  case Greater:      return "Greater";
  case GreaterEqual: return "GreaterEqual";
  case Less:         return "Less";
  case LessEqual:    return "LessEqual";
  case Arrow:        return "Arrow";
  case MinusMinus:   return "MinusMinus";
  case PlusPlus:     return "PlusPlus";
  case Identifier:   return "Identifier";
  case CharLiteral:  return "CharLiteral";
  case StringLiteral:return "StringLiteral";
  case IntLiteral:   return "IntLiteral";
  case FloatLiteral: return "FloatLiteral";
  case And:          return "And";
  case Or:           return "Or";
  case True:         return "True";
  case False:        return "False";
  case If:           return "If";
  case Else:         return "Else";
  case Elif:         return "Elif";
  case Decl:         return "Decl";
  case Def:          return "Def";
  case Let:          return "Let";
  case Return:       return "Return";
  case None:         return "None";
  case Ref:          return "Ref";
  case Ptr:          return "Ptr";
  case Class:        return "Class";
  case Super:        return "Super";
  case Sself:        return "Sself";
  case Trait:        return "Trait";
  case Impl:         return "Impl";
  case For:          return "For";
  case While:        return "While";
  case Break:        return "Break";
  case Continue:     return "Continue";
  case Int:          return "Int";
  case Float:        return "Float";
  case Bool:         return "Bool";
  case Char:         return "Char";
  case Str:          return "Str";
  case List:         return "List";
  case TokenEOF:     return "EOF";
  case Error:        return "Error";
  }
  return "Unknown";
}
} // namespace cat
