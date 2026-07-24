#pragma once
#include "../ast/item.h"
#include "../common/diag.h"
#include "../common/error.h"
#include "../lexer/lexer.h"
#include "stmt.h"
namespace cat {
  class Parser {
public:
    Parser(Lexer &lexer, error::DiagCtxt &diag_ctxt)
        : tokens(lexer), diag_ctxt(diag_ctxt), current_token(std::nullopt),
          last_span(Span{0, 0}) {
      advance();
    }

    Program parse_program();

private:
    Lexer &tokens;
    optional<Token> current_token;
    error::DiagCtxt &diag_ctxt;
    Span last_span;

private:
    void advance();
    std::optional<Token> peek() const;
    bool check(TokenKind kind) const;
    bool check_any(std::initializer_list<TokenKind> kinds) const;
    bool is_at_end() const;

    std::optional<Token> consume(TokenKind kind, std::string_view msg);

    Span current_span() const;

    template<typename T>
    error::ParseResult<T> err(Span span, std::string_view msg);

    template<typename T>
    error::ParseResult<T> unexpected(std::string_view context);

    void synchronize();

    // parsing
    error::ParseResult<ItemNode> parse_item();
    error::ParseResult<StmtNode> parse_stmt();
    error::ParseResult<ExprNode> parse_expr();
    error::ParseResult<ExprNode> parse_assignment();
    error::ParseResult<ExprNode> parse_logical_or();
    error::ParseResult<ExprNode> parse_logical_and();
    error::ParseResult<ExprNode> parse_equality();
    error::ParseResult<ExprNode> parse_comparison();
    error::ParseResult<ExprNode> parse_term();
    error::ParseResult<ExprNode> parse_factor();
    error::ParseResult<ExprNode> parse_unary();
    error::ParseResult<ExprNode> parse_postfix();
    error::ParseResult<ExprNode> parse_primary();

    error::ParseResult<StmtNode> parse_if_stmt();
    error::ParseResult<StmtNode> parse_loop_stmt();
    error::ParseResult<StmtNode> parse_break_stmt();
    error::ParseResult<StmtNode> parse_continue_stmt();
    error::ParseResult<StmtNode> parse_return_stmt();
    error::ParseResult<StmtNode> parse_block_stmt();
    error::ParseResult<StmtNode> parse_var_def_stmt();

    error::ParseResult<Block> parse_block();
    error::ParseResult<ast::Type> parse_type();
    error::ParseResult<Parameter> parse_parameter();
    error::ParseResult<FunctionDecl> parse_header();
    error::ParseResult<FunctionDecl> parse_func_decl();
    error::ParseResult<FunctionDef> parse_function();
    error::ParseResult<Trait> parse_trait();
    error::ParseResult<Impl> parse_impl();
    error::ParseResult<Field> parse_field();
    error::ParseResult<Class> parse_class();
    error::ParseResult<GlobalVar> parse_global_var();

    error::ParseResult<std::vector<uptr<ExprNode>>> parse_arguments();
    error::ParseResult<ExprNode> finish_call(ExprNode callee);
    error::ParseResult<ExprNode> parse_lambda();
  };
}// namespace cat
