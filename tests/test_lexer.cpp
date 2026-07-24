#include <gtest/gtest.h>
#include "lexer.h"
#include "token.h"

using namespace cat;

static vector<TokenKind> tokenize_kinds(const string &source) {
    Lexer lexer(source);
    vector<TokenKind> kinds;
    for (auto &tok : lexer) {
        kinds.push_back(tok.kind);
    }
    return kinds;
}

static vector<string> tokenize_lexemes(const string &source) {
    Lexer lexer(source);
    vector<string> lexemes;
    for (auto &tok : lexer) {
        lexemes.push_back(tok.lexeme);
    }
    return lexemes;
}

TEST(Lexer, Empty) {
    auto kinds = tokenize_kinds("");
    EXPECT_EQ(kinds.size(), 0u);
}

TEST(Lexer, Keywords) {
    auto kinds = tokenize_kinds("fn let return if else elif while break continue");
    ASSERT_GE(kinds.size(), 9u);
    EXPECT_EQ(kinds[0], TokenKind::Fn);
    EXPECT_EQ(kinds[1], TokenKind::Let);
    EXPECT_EQ(kinds[2], TokenKind::Return);
    EXPECT_EQ(kinds[3], TokenKind::If);
    EXPECT_EQ(kinds[4], TokenKind::Else);
    EXPECT_EQ(kinds[5], TokenKind::Elif);
    EXPECT_EQ(kinds[6], TokenKind::While);
    EXPECT_EQ(kinds[7], TokenKind::Break);
    EXPECT_EQ(kinds[8], TokenKind::Continue);
}

TEST(Lexer, Types) {
    auto kinds = tokenize_kinds("int float bool char str void");
    ASSERT_GE(kinds.size(), 6u);
    EXPECT_EQ(kinds[0], TokenKind::Int);
    EXPECT_EQ(kinds[1], TokenKind::Float);
    EXPECT_EQ(kinds[2], TokenKind::Bool);
    EXPECT_EQ(kinds[3], TokenKind::Char);
    EXPECT_EQ(kinds[4], TokenKind::Str);
    EXPECT_EQ(kinds[5], TokenKind::None);
}

TEST(Lexer, Identifiers) {
    auto kinds = tokenize_kinds("foo bar123 _under main x");
    ASSERT_GE(kinds.size(), 5u);
    for (int i = 0; i < 5; i++)
        EXPECT_EQ(kinds[i], TokenKind::Identifier);
}

TEST(Lexer, IntegerLiterals) {
    auto kinds = tokenize_kinds("0 42 100 9999");
    ASSERT_GE(kinds.size(), 4u);
    EXPECT_EQ(kinds[0], TokenKind::IntLiteral);
    EXPECT_EQ(kinds[1], TokenKind::IntLiteral);
    EXPECT_EQ(kinds[2], TokenKind::IntLiteral);
    EXPECT_EQ(kinds[3], TokenKind::IntLiteral);
}

TEST(Lexer, FloatLiterals) {
    auto kinds = tokenize_kinds("3.14 0.5 10.0");
    ASSERT_GE(kinds.size(), 3u);
    EXPECT_EQ(kinds[0], TokenKind::FloatLiteral);
    EXPECT_EQ(kinds[1], TokenKind::FloatLiteral);
    EXPECT_EQ(kinds[2], TokenKind::FloatLiteral);
}

TEST(Lexer, StringLiterals) {
    auto lexemes = tokenize_lexemes("\"hello\" \"world\"");
    ASSERT_GE(lexemes.size(), 2u);
    EXPECT_EQ(lexemes[0], "hello");
    EXPECT_EQ(lexemes[1], "world");
}

TEST(Lexer, CharLiterals) {
    auto kinds = tokenize_kinds("'a' 'b' 'Z'");
    ASSERT_GE(kinds.size(), 3u);
    EXPECT_EQ(kinds[0], TokenKind::CharLiteral);
    EXPECT_EQ(kinds[1], TokenKind::CharLiteral);
    EXPECT_EQ(kinds[2], TokenKind::CharLiteral);
}

TEST(Lexer, Operators) {
    auto kinds = tokenize_kinds("+ - * / % = == != < > >= <= &");
    ASSERT_GE(kinds.size(), 13u);
    EXPECT_EQ(kinds[0], TokenKind::Plus);
    EXPECT_EQ(kinds[1], TokenKind::Minus);
    EXPECT_EQ(kinds[2], TokenKind::Star);
    EXPECT_EQ(kinds[3], TokenKind::Slash);
    EXPECT_EQ(kinds[4], TokenKind::Modulo);
    EXPECT_EQ(kinds[5], TokenKind::Equal);
    EXPECT_EQ(kinds[6], TokenKind::EqualEqual);
    EXPECT_EQ(kinds[7], TokenKind::BangEqual);
    EXPECT_EQ(kinds[8], TokenKind::Less);
    EXPECT_EQ(kinds[9], TokenKind::Greater);
    EXPECT_EQ(kinds[10], TokenKind::GreaterEqual);
    EXPECT_EQ(kinds[11], TokenKind::LessEqual);
    EXPECT_EQ(kinds[12], TokenKind::BitwiseAnd);
}

TEST(Lexer, Brackets) {
    auto kinds = tokenize_kinds("(){}[]");
    ASSERT_GE(kinds.size(), 6u);
    EXPECT_EQ(kinds[0], TokenKind::LeftParen);
    EXPECT_EQ(kinds[1], TokenKind::RightParen);
    EXPECT_EQ(kinds[2], TokenKind::LeftBrace);
    EXPECT_EQ(kinds[3], TokenKind::RightBrace);
    EXPECT_EQ(kinds[4], TokenKind::LeftBracket);
    EXPECT_EQ(kinds[5], TokenKind::RightBracket);
}

TEST(Lexer, Separators) {
    auto kinds = tokenize_kinds(", ; : . ->");
    ASSERT_GE(kinds.size(), 5u);
    EXPECT_EQ(kinds[0], TokenKind::Comma);
    EXPECT_EQ(kinds[1], TokenKind::Semicolon);
    EXPECT_EQ(kinds[2], TokenKind::Colon);
    EXPECT_EQ(kinds[3], TokenKind::Dot);
    EXPECT_EQ(kinds[4], TokenKind::Arrow);
}

TEST(Lexer, BoolLiterals) {
    auto kinds = tokenize_kinds("true false");
    ASSERT_GE(kinds.size(), 2u);
    EXPECT_EQ(kinds[0], TokenKind::True);
    EXPECT_EQ(kinds[1], TokenKind::False);
}

TEST(Lexer, LogicOperators) {
    auto kinds = tokenize_kinds("and or");
    ASSERT_GE(kinds.size(), 2u);
    EXPECT_EQ(kinds[0], TokenKind::And);
    EXPECT_EQ(kinds[1], TokenKind::Or);
}

TEST(Lexer, ClassKeywords) {
    auto kinds = tokenize_kinds("class trait impl self super ref own ptr list");
    ASSERT_GE(kinds.size(), 9u);
    EXPECT_EQ(kinds[0], TokenKind::Class);
    EXPECT_EQ(kinds[1], TokenKind::Trait);
    EXPECT_EQ(kinds[2], TokenKind::Impl);
    EXPECT_EQ(kinds[3], TokenKind::Sself);
    EXPECT_EQ(kinds[4], TokenKind::Super);
    EXPECT_EQ(kinds[5], TokenKind::Ref);
    EXPECT_EQ(kinds[6], TokenKind::Own);
    EXPECT_EQ(kinds[7], TokenKind::Ptr);
    EXPECT_EQ(kinds[8], TokenKind::List);
}

TEST(Lexer, IncrementDecrement) {
    auto kinds = tokenize_kinds("++ --");
    ASSERT_GE(kinds.size(), 2u);
    EXPECT_EQ(kinds[0], TokenKind::PlusPlus);
    EXPECT_EQ(kinds[1], TokenKind::MinusMinus);
}

TEST(Lexer, Comments) {
    auto kinds = tokenize_kinds("// this is a comment\nlet x = 1;");
    ASSERT_GE(kinds.size(), 4u);
    EXPECT_EQ(kinds[0], TokenKind::Let);
    EXPECT_EQ(kinds[1], TokenKind::Identifier);
    EXPECT_EQ(kinds[2], TokenKind::Equal);
    EXPECT_EQ(kinds[3], TokenKind::IntLiteral);
}
