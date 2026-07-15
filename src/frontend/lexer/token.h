#pragma once
#include "../../common/common.h"
#include "../../common/span.h"
namespace cat {
  enum class TokenKind {
    LeftParen, // (
    RightParen,// )
    LeftBrace, // {
    RightBrace,// }
    Comma,     // ,
    Dot,       // .
    Minus,     //-
    Plus,      //=
    Caret,     // ^
    Semicolon, // ;
    Slash,     // /
    Star,      // *
    Backslash, // (\)
    Modulo,    // %
    Colon,     // :
    BitwiseAnd,// &

    // One or two character tokens.
    RightBracket,// ]
    LeftBracket, // [
    Bang,        // !
    BangEqual,   // !=
    Equal,       // =
    EqualEqual,  //==
    Greater,     // >
    GreaterEqual,//>=
    Less,        //<
    LessEqual,   //<=
    Arrow,       // ->
    MinusMinus,  //--
    PlusPlus,    //++

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

    void print(ostream& out) const;
  };

  [[nodiscard]] constexpr const char* tokenkind_to_string(TokenKind kind) noexcept;
}// namespace cat
