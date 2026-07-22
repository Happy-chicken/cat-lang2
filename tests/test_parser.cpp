#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "diag.h"
#include "file.h"

using namespace cat;

static auto parse_source(const string &source) {
    cat::File file("<test>", source);
    cat::error::DiagCtxt diag;
    diag.add_file(std::move(file));
    cat::Lexer lexer(source);
    cat::Parser parser(lexer, diag);
    return std::make_pair(parser.parse_program(), std::move(diag));
}

TEST(Parser, EmptyProgram) {
    auto [prog, diag] = parse_source("");
    EXPECT_TRUE(prog.items.empty());
}

TEST(Parser, SimpleFunction) {
    auto [prog, diag] = parse_source("def main()->int { return 42; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->function_header.name, "main");
    EXPECT_TRUE(func->function_header.return_type.has_value());
    EXPECT_EQ(func->function_header.return_type->to_string(), "int");
    EXPECT_EQ(func->body.stmts.size(), 1u);
}

TEST(Parser, FunctionWithParams) {
    auto [prog, diag] = parse_source("def add(x:int, y:int) -> int { return x + y; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->function_header.name, "add");
    EXPECT_EQ(func->function_header.params.size(), 2u);
    EXPECT_EQ(func->function_header.params[0].name, "x");
    EXPECT_EQ(func->function_header.params[1].name, "y");
    EXPECT_EQ(func->function_header.return_type->to_string(), "int");
}

TEST(Parser, VarDefWithInit) {
    auto [prog, diag] = parse_source("def main()->int { let x = 42; return x; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->body.stmts.size(), 2u);
}

TEST(Parser, RefParam) {
    auto [prog, diag] = parse_source("def inc(x: ref int) { x = x + 1; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    ASSERT_EQ(func->function_header.params.size(), 1u);
    auto &ty = func->function_header.params[0].ty;
    EXPECT_TRUE(std::get_if<ast::Type::Ref>(&ty.data) != nullptr);
}

TEST(Parser, OwnParam) {
    auto [prog, diag] = parse_source("def take(x: own int) {}");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    auto &ty = func->function_header.params[0].ty;
    EXPECT_TRUE(std::get_if<ast::Type::Own>(&ty.data) != nullptr);
}

TEST(Parser, ListLiteral) {
    auto [prog, diag] = parse_source("def main()->int { let x = [1, 2, 3]; return x[0]; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->body.stmts.size(), 2u);
}

TEST(Parser, IfElse) {
    auto [prog, diag] = parse_source(
        "def main()->int { if 1 > 0 { return 1; } else { return 0; } }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->body.stmts.size(), 1u);
}

TEST(Parser, WhileLoop) {
    auto [prog, diag] = parse_source(
        "def main()->int { let i = 0; while i < 10 { i = i + 1; } return i; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
}

TEST(Parser, BinaryOps) {
    auto [prog, diag] = parse_source("def main()->int { return 1 + 2 * 3; }");
    ASSERT_EQ(prog.items.size(), 1u);
    ASSERT_FALSE(diag.has_errors());
}

TEST(Parser, ComparisonOps) {
    auto [prog, diag] = parse_source("def main()->int { return 5 >= 3 and 2 <= 4; }");
    ASSERT_EQ(prog.items.size(), 1u);
    ASSERT_FALSE(diag.has_errors());
}

TEST(Parser, GlobalVar) {
    auto [prog, diag] = parse_source("let g: int = 42;");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *gv = std::get_if<GlobalVar>(&prog.items[0].item);
    ASSERT_NE(gv, nullptr);
    EXPECT_EQ(gv->name, "g");
    EXPECT_TRUE(gv->ty.has_value());
    EXPECT_EQ(gv->ty->to_string(), "int");
}

TEST(Parser, ClassDef) {
    auto [prog, diag] = parse_source("class Point { let x: int = 0; let y: int = 0; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *cls = std::get_if<Class>(&prog.items[0].item);
    ASSERT_NE(cls, nullptr);
    EXPECT_EQ(cls->name, "Point");
    EXPECT_EQ(cls->fields.size(), 2u);
}

TEST(Parser, ListTypeParam) {
    auto [prog, diag] = parse_source("def foo(x: list<int>) -> int { return x[0]; }");
    ASSERT_EQ(prog.items.size(), 1u);
    auto *func = std::get_if<FunctionDef>(&prog.items[0].item);
    ASSERT_NE(func, nullptr);
    EXPECT_EQ(func->function_header.params.size(), 1u);
    auto &ty = func->function_header.params[0].ty;
    EXPECT_TRUE(std::get_if<ast::Type::List>(&ty.data) != nullptr);
}
