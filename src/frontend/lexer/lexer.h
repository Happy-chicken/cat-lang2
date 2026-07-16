#pragma once
#include "../../common/common.h"
#include "../../common/file.h"
#include "token.h"
#include <iterator>
#include <ranges>
#include <string_view>


namespace cat {
  class Lexer {
private:
    std::string_view src;
    std::string_view::const_iterator it;
    std::string_view::const_iterator token_start;
    Location loc{1, 0};
    Token current_{TokenKind::TokenEOF, "", Span{0, 0}};
    bool started_ = false;

    [[nodiscard]] Token make_token(TokenKind kind, std::string lexeme) const;
    [[nodiscard]] Token number();
    [[nodiscard]] Token identifier_or_keyword();
    [[nodiscard]] Token string();
    [[nodiscard]] Token character();
    [[nodiscard]] Token comment();
    [[nodiscard]] Token operator_or_punctuation();

public:
    explicit Lexer(std::string_view source)
        : src(source), it(src.begin()), token_start(src.begin()) {}

    [[nodiscard]] optional<char> peek() const;
    [[nodiscard]] optional<char> peek_next() const;
    optional<char> advance();
    [[nodiscard]] bool is_eof() const;
    void skip_whitespace();
    [[nodiscard]] std::string_view current_token() const;

    auto chars() const;
    [[nodiscard]] Token next_token();
    [[nodiscard]] vector<Token> tokenize();

    [[nodiscard]] const Token &operator*() const { return current_; }
    Lexer &operator++();
    [[nodiscard]] bool operator==(std::default_sentinel_t) const { return started_ && current_.kind == TokenKind::TokenEOF; }

    Lexer &begin() {
      if (!started_) static_cast<void>(next_token());
      return *this;
    }
    [[nodiscard]] std::default_sentinel_t end() const { return std::default_sentinel; }
  };
}// namespace cat
