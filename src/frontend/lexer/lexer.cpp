#include "lexer.h"
#include "token.h"
#include <cctype>
#include <optional>
namespace cat {

  static const unordered_map<string, TokenKind> KEYWORDS = {
      {"and", TokenKind::And},
      {"or", TokenKind::Or},
      {"true", TokenKind::True},
      {"false", TokenKind::False},

      {"class", TokenKind::Class},
      {"super", TokenKind::Super},
      {"self", TokenKind::Sself},
      {"impl", TokenKind::Impl},
      {"trait", TokenKind::Trait},
      {"for", TokenKind::For},

      {"fn", TokenKind::Fn},
      {"let", TokenKind::Let},
      {"decl", TokenKind::Decl},

      {"if", TokenKind::If},
      {"else", TokenKind::Else},
      {"elif", TokenKind::Elif},
      {"while", TokenKind::While},
      {"break", TokenKind::Break},
      {"continue", TokenKind::Continue},
      {"return", TokenKind::Return},

      {"void", TokenKind::None},
      {"ref", TokenKind::Ref},
      {"own", TokenKind::Own},
      {"ptr", TokenKind::Ptr},

      {"int", TokenKind::Int},
      {"float", TokenKind::Float},
      {"bool", TokenKind::Bool},
      {"char", TokenKind::Char},
      {"str", TokenKind::Str},

      {"list", TokenKind::List},
  };

  Token Lexer::make_token(TokenKind kind, std::string lexeme) const {
    auto start = static_cast<size_t>(token_start - src.begin());
    auto end = static_cast<size_t>(it - src.begin());
    return Token{kind, std::move(lexeme), Span(start, end)};
  }

  optional<char> Lexer::peek() const {
    if (it == src.end())
      return std::nullopt;
    return *it;
  }

  optional<char> Lexer::peek_next() const {
    auto next = std::next(it);
    if (next == src.end())
      return std::nullopt;
    return *next;
  }

  optional<char> Lexer::advance() {
    if (it == src.end())
      return std::nullopt;
    char c = *it++;
    if (c == '\n') {
      loc.line++;
      loc.column = 1;
    } else {
      loc.column++;
    }
    return c;
  }

  bool Lexer::is_eof() const { return it == src.end(); }

  void Lexer::skip_whitespace() {
    while (!is_eof() && std::isspace(static_cast<unsigned char>(*it))) {
      advance();
    }
    token_start = it;
  }

  std::string_view Lexer::current_token() const { return {token_start, it}; }

  auto Lexer::chars() const { return std::views::all(src); }

  Token Lexer::next_token() {
    skip_whitespace();
    token_start = it;

    if (is_eof()) {
      current_ = make_token(TokenKind::TokenEOF, "");
      started_ = true;
      return current_;
    }

    char c = *it;

    if (std::isdigit(static_cast<unsigned char>(c))) {
      current_ = number();
    } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
      current_ = identifier_or_keyword();
    } else if (c == '"') {
      current_ = string();
    } else if (c == '\'') {
      current_ = character();
    } else if (c == '/') {
      auto next = peek_next();
      if (next.has_value() && (next.value() == '/' || next.value() == '*')) {
        current_ = comment();
      } else {
        current_ = operator_or_punctuation();
      }
    } else {
      current_ = operator_or_punctuation();
    }

    started_ = true;
    return current_;
  }

  Token Lexer::number() {
    bool is_float = false;

    while (!is_eof()) {
      char c = *it;
      if (std::isdigit(static_cast<unsigned char>(c))) {
        advance();
      } else if (c == '.') {
        auto next = peek_next();
        if (!next.has_value() ||
            !std::isdigit(static_cast<unsigned char>(next.value()))) {
          break;
        }
        if (is_float)
          break;
        is_float = true;
        advance();
      } else if (c == '_') {
        auto next = peek_next();
        if (!next.has_value() ||
            !std::isdigit(static_cast<unsigned char>(next.value()))) {
          break;
        }
        advance();
      } else {
        break;
      }
    }

    return make_token(is_float ? TokenKind::FloatLiteral : TokenKind::IntLiteral, std::string(current_token()));
  }

  Token Lexer::identifier_or_keyword() {
    while (!is_eof()) {
      char c = *it;
      if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
        advance();
      } else {
        break;
      }
    }

    std::string lexeme(current_token());
    if (auto kw = KEYWORDS.find(lexeme); kw != KEYWORDS.end()) {
      return make_token(kw->second, std::move(lexeme));
    }
    return make_token(TokenKind::Identifier, std::move(lexeme));
  }

  Token Lexer::string() {
    advance();

    auto content_start = it;

    while (!is_eof() && *it != '"') {
      if (*it == '\\' && peek_next().has_value()) {
        advance();
      }
      advance();
    }

    auto content = std::string(content_start, it);

    if (!is_eof())
      advance();

    return make_token(TokenKind::StringLiteral, std::move(content));
  }

  Token Lexer::character() {
    advance();

    auto content_start = it;

    if (!is_eof() && *it == '\\') {
      advance();
    }
    if (!is_eof())
      advance();

    auto content = std::string(content_start, it);

    if (!is_eof() && *it == '\'') {
      advance();
    }

    return make_token(TokenKind::CharLiteral, std::move(content));
  }

  Token Lexer::comment() {
    advance();

    if (!is_eof() && *it == '/') {
      advance();
      while (!is_eof() && *it != '\n') {
        advance();
      }
    } else if (!is_eof() && *it == '*') {
      advance();
      while (!is_eof()) {
        if (*it == '*' && peek_next().has_value() && peek_next().value() == '/') {
          advance();
          advance();
          break;
        }
        advance();
      }
    }

    return next_token();
  }

  Token Lexer::operator_or_punctuation() {
    char c = *it;
    advance();

    switch (c) {
      case '(':
        return make_token(TokenKind::LeftParen, "(");
      case ')':
        return make_token(TokenKind::RightParen, ")");
      case '{':
        return make_token(TokenKind::LeftBrace, "{");
      case '}':
        return make_token(TokenKind::RightBrace, "}");
      case ',':
        return make_token(TokenKind::Comma, ",");
      case '.':
        return make_token(TokenKind::Dot, ".");
      case '^':
        return make_token(TokenKind::Caret, "^");
      case ';':
        return make_token(TokenKind::Semicolon, ";");
      case '*':
        return make_token(TokenKind::Star, "*");
      case '\\':
        return make_token(TokenKind::Backslash, "\\");
      case '%':
        return make_token(TokenKind::Modulo, "%");
      case ':':
        return make_token(TokenKind::Colon, ":");
      case '&':
        return make_token(TokenKind::BitwiseAnd, "&");
      case ']':
        return make_token(TokenKind::RightBracket, "]");
      case '[':
        return make_token(TokenKind::LeftBracket, "[");

      case '!': {
        if (!is_eof() && *it == '=') {
          advance();
          return make_token(TokenKind::BangEqual, "!=");
        }
        return make_token(TokenKind::Bang, "!");
      }
      case '=': {
        if (!is_eof() && *it == '=') {
          advance();
          return make_token(TokenKind::EqualEqual, "==");
        }
        return make_token(TokenKind::Equal, "=");
      }
      case '>': {
        if (!is_eof() && *it == '=') {
          advance();
          return make_token(TokenKind::GreaterEqual, ">=");
        }
        return make_token(TokenKind::Greater, ">");
      }
      case '<': {
        if (!is_eof() && *it == '=') {
          advance();
          return make_token(TokenKind::LessEqual, "<=");
        }
        return make_token(TokenKind::Less, "<");
      }
      case '-': {
        if (!is_eof() && *it == '>') {
          advance();
          return make_token(TokenKind::Arrow, "->");
        }
        if (!is_eof() && *it == '-') {
          advance();
          return make_token(TokenKind::MinusMinus, "--");
        }
        return make_token(TokenKind::Minus, "-");
      }
      case '+': {
        if (!is_eof() && *it == '+') {
          advance();
          return make_token(TokenKind::PlusPlus, "++");
        }
        return make_token(TokenKind::Plus, "+");
      }
      case '/': {
        return make_token(TokenKind::Slash, "/");
      }

      default:
        return make_token(TokenKind::Error, std::string(1, c));
    }
  }

  Lexer &Lexer::operator++() {
    static_cast<void>(next_token());
    return *this;
  }

  vector<Token> Lexer::tokenize() {
    vector<Token> tokens;
    for (auto &tok: *this) {
      tokens.push_back(tok);
    }
    return tokens;
  }

}// namespace cat
