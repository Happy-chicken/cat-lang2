#include "parser.h"
#include "expr.h"
#include "stmt.h"
#include "token.h"
#include <memory>

namespace cat {
  Program Parser::parse_program() {
    vector<ItemNode> items;
    while (!is_at_end()) {
      auto item = parse_item();
      if (item.has_value()) {
        items.push_back(std::move(item.value()));
      } else {
        synchronize();
      }
    }
    return Program{std::move(items)};
  }
  void Parser::advance() {
    auto tok = tokens.next_token();
    if (tok.kind != TokenKind::TokenEOF) {
      current_token = tok;
      last_span = tok.span;
    } else {
      current_token = std::nullopt;
    }
  }

  // std::optional<Token> Parser::peek() const { return current_token; }

  bool Parser::check(TokenKind kind) const {
    return current_token.has_value() && current_token->kind == kind;
  }

  bool Parser::check_any(std::initializer_list<TokenKind> kinds) const {
    if (!current_token.has_value())
      return false;
    for (auto kind: kinds) {
      if (current_token->kind == kind)
        return true;
    }
    return false;
  }

  bool Parser::is_at_end() const {
    return !current_token.has_value() ||
           current_token->kind == TokenKind::TokenEOF;
  }

  std::optional<Token> Parser::consume(TokenKind kind, std::string_view msg) {
    if (check(kind)) {
      auto tok = current_token;
      advance();
      return tok;
    }
    auto span = current_span();
    diag_ctxt.error(span, string(msg))
        .note("Expected " + string(tokenkind_to_string(kind)) + ", but found " + (current_token.has_value() ? string(tokenkind_to_string(current_token->kind)) : "EOF"));
    return std::nullopt;
  }

  Span Parser::current_span() const {
    return current_token.has_value() ? current_token->span : last_span;
  }

  template<typename T>
  error::ParseResult<T> Parser::err(Span span, std::string_view msg) {
    diag_ctxt.error(span, string(msg)).emit_to(diag_ctxt);
    return std::nullopt;
  }

  template<typename T>
  error::ParseResult<T> Parser::unexpected(std::string_view context) {
    auto span = current_span();
    string msg = "unexpected ";
    if (current_token.has_value()) {
      msg += tokenkind_to_string(current_token->kind);
      msg += " ";
    } else {
      msg += "end of file ";
    }
    msg += context;
    return err<T>(span, msg);
  }

  void Parser::synchronize() {
    while (!is_at_end()) {
      if (current_token.has_value() &&
          current_token->kind == TokenKind::Semicolon) {
        advance();
        return;
      }
      if (check_any({TokenKind::Let, TokenKind::Def, TokenKind::Class, TokenKind::If, TokenKind::While, TokenKind::Return, TokenKind::Break, TokenKind::Continue, TokenKind::TokenEOF})) {
        advance();// 跳过该关键字，视为同步点
        return;
      }
      advance();
    }
  }
}// namespace cat

// parsing
namespace cat {
  error::ParseResult<ItemNode> Parser::parse_item() {
    if (!current_token.has_value()) {
      return unexpected<ItemNode>("Unexpected at end of file");
    }
    auto span = current_span();
    switch (current_token->kind) {
      case TokenKind::Decl: {
        auto decl = parse_func_decl();
        if (decl.has_value()) {
          return ItemNode{span, std::move(*decl)};
        } else {
          return std::nullopt;
        }
      }
      case TokenKind::Def: {
        auto func = parse_function();
        if (func.has_value()) {
          return ItemNode{span, std::move(*func)};
        } else {
          return std::nullopt;
        }
      }
      case TokenKind::Trait: {
        auto trait = parse_trait();
        if (trait.has_value()) {
          return ItemNode{span, std::move(*trait)};
        } else {
          return std::nullopt;
        }
      }
      case TokenKind::Impl: {
        auto impl = parse_impl();
        if (impl.has_value()) {
          return ItemNode{span, std::move(*impl)};
        } else {
          return std::nullopt;
        }
      }
      case TokenKind::Class: {
        auto cls = parse_class();
        if (cls.has_value()) {
          return ItemNode{span, std::move(*cls)};
        } else {
          return std::nullopt;
        }
      }
      case TokenKind::Let: {
        auto global_var = parse_global_var();
        if (global_var.has_value()) {
          return ItemNode{span, std::move(*global_var)};
        } else {
          return std::nullopt;
        }
      }
      default:
        return unexpected<ItemNode>("at top-level");
    }
  }

  error::ParseResult<StmtNode> Parser::parse_stmt() {
    if (!current_token.has_value()) {
      return unexpected<StmtNode>("Unexpected at end of file");
    }
    auto span = current_span();
    switch (current_token->kind) {
      case TokenKind::If: {
        auto if_stmt = parse_if_stmt();
        if (!if_stmt.has_value()) {
          return std::nullopt;
        }
        return if_stmt;
      }
      case TokenKind::While: {
        auto loop_stmt = parse_loop_stmt();
        if (!loop_stmt.has_value()) {
          return std::nullopt;
        }
        return loop_stmt;
      }
      case TokenKind::Return: {
        auto return_stmt = parse_return_stmt();
        if (!return_stmt.has_value()) {
          return std::nullopt;
        }
        return return_stmt;
      }
      case TokenKind::Break: {
        auto break_stmt = parse_break_stmt();
        if (!break_stmt.has_value()) {
          return std::nullopt;
        }
        return break_stmt;
      }
      case TokenKind::Continue: {
        auto continue_stmt = parse_continue_stmt();
        if (!continue_stmt.has_value()) {
          return std::nullopt;
        }
        return continue_stmt;
      }
      case TokenKind::LeftBrace: {
        auto block_stmt = parse_block_stmt();
        if (!block_stmt.has_value()) {
          return std::nullopt;
        }
        return block_stmt;
      }
      case TokenKind::Let: {
        auto var_def_stmt = parse_var_def_stmt();
        if (!var_def_stmt.has_value()) {
          return std::nullopt;
        }
        return var_def_stmt;
      }
      default: {
        auto expr = parse_expr();
        if (!expr.has_value()) {
          return std::nullopt;
        }
        consume(TokenKind::Semicolon, "Expected ';' after expression statement.");
        return StmtNode{span, make_expr_stmt(std::move(*expr))};
      }
    }
  }

  error::ParseResult<StmtNode> Parser::parse_if_stmt() {
    consume(TokenKind::If, "Expected 'if' at the begining of is statement.");
    auto condition = parse_expr();
    if (!condition.has_value()) {
      return std::nullopt;
    }
    auto then_branch = parse_block();
    if (!then_branch.has_value()) {
      return std::nullopt;
    }
    auto elif_braches = vector<std::pair<ExprNode, uptr<Block>>>{};
    while (check(TokenKind::Elif)) {
      advance();// consume 'elif'
      auto elif_condition = parse_expr();
      if (!elif_condition.has_value()) {
        return std::nullopt;
      }
      auto elif_block = parse_block();
      if (!elif_block.has_value()) {
        return std::nullopt;
      }
      elif_braches.push_back(
          std::make_pair(std::move(*elif_condition), std::make_unique<Block>(std::move(*elif_block)))
      );
    }
    auto else_branch = uptr<Block>{nullptr};
    if (check(TokenKind::Else)) {
      advance();// consume 'else'
      auto else_block = parse_block();
      if (!else_block.has_value()) {
        return std::nullopt;
      }
      else_branch = std::make_unique<Block>(std::move(*else_block));
    }
    return StmtNode{current_span(), make_if(std::move(*condition), std::make_unique<Block>(std::move(*then_branch)), std::move(elif_braches), std::move(else_branch))};
  }

  error::ParseResult<StmtNode> Parser::parse_loop_stmt() {
    consume(TokenKind::While, "Expected 'while' at the begining of while statement.");
    auto condition = parse_expr();
    if (!condition.has_value()) {
      return std::nullopt;
    }
    auto body = parse_block();
    if (!body.has_value()) {
      return std::nullopt;
    }
    return StmtNode{current_span(), make_loop(std::move(*condition), std::make_unique<Block>(std::move(*body)))};
  }

  error::ParseResult<StmtNode> Parser::parse_break_stmt() {
    consume(TokenKind::Break, "Expected 'break' at the beginning of break statement.");
    consume(TokenKind::Semicolon, "Expected ';' after break statement.");
    return StmtNode{current_span(), make_break()};
  }

  error::ParseResult<StmtNode> Parser::parse_continue_stmt() {
    consume(TokenKind::Continue, "Expected 'continue' at the beginning of continue statement.");
    consume(TokenKind::Semicolon, "Expected ';' after continue statement.");
    return StmtNode{current_span(), make_continue()};
  }

  error::ParseResult<StmtNode> Parser::parse_return_stmt() {
    consume(TokenKind::Return, "Expected 'return' at the begining of return statement.");
    optional<ExprNode> expr = std::nullopt;
    if (!check(TokenKind::Semicolon)) {
      auto ret_expr = parse_expr();
      if (!ret_expr.has_value()) {
        return std::nullopt;
      }
      expr = std::move(*ret_expr);
    }
    consume(TokenKind::Semicolon, "Expected ';' after return statement.");
    return StmtNode{current_span(), make_return(std::move(expr))};
  }

  error::ParseResult<StmtNode> Parser::parse_block_stmt() {
    auto block = parse_block();
    if (!block.has_value()) {
      return std::nullopt;
    }
    return StmtNode{current_span(), make_block_stmt(std::make_unique<Block>(std::move(*block)))};
  }

  error::ParseResult<StmtNode> Parser::parse_var_def_stmt() {
    consume(TokenKind::Let, "Expected 'let' at the beginning of variable definition.");
    auto var_token =
        consume(TokenKind::Identifier, "Expected identifier after 'let'.");
    if (!var_token.has_value()) {
      return std::nullopt;
    }
    string var_name = var_token->lexeme;
    std::optional<ast::Type> ty = std::nullopt;
    if (check(TokenKind::Colon)) {
      advance();// consume ':'
      auto type = parse_type();
      if (!type.has_value()) {
        return std::nullopt;
      }
      ty = std::move(*type);
    }
    std::optional<ExprNode> init = std::nullopt;
    if (check(TokenKind::Equal)) {
      advance();// consume '='
      auto init_expr = parse_expr();
      if (!init_expr.has_value()) {
        return std::nullopt;
      }
      init = std::move(*init_expr);
    }
    consume(TokenKind::Semicolon, "Expected ';' after variable definition.");
    return StmtNode{current_span(), make_var_def(std::move(var_name), std::move(ty), std::move(init))};
  }

  error::ParseResult<ExprNode> Parser::parse_expr() { return parse_assignment(); }

  error::ParseResult<ExprNode> Parser::parse_assignment() {
    auto left = parse_logical_or();
    if (!left.has_value()) {
      return std::nullopt;
    }
    if (check(TokenKind::Equal)) {
      advance();
      auto right = parse_assignment();
      if (!right.has_value()) {
        return std::nullopt;
      }
      auto target = std::make_unique<ExprNode>(std::move(*left));
      auto value = std::make_unique<ExprNode>(std::move(*right));
      auto expr = make_assign(std::move(target), std::move(value));
      return ExprNode{left->span.merge(right->span), std::move(expr)};
    }
    return left;
  }

  error::ParseResult<ExprNode> Parser::parse_logical_or() {
    auto expr = parse_logical_and();
    if (!expr.has_value()) {
      return std::nullopt;
    }
    if (check(TokenKind::Or)) {
      auto op_span = current_span();
      advance();
      auto right = parse_logical_and();
      if (!right.has_value()) {
        return std::nullopt;
      }
      auto lhs_node = std::make_unique<ExprNode>(std::move(*expr));
      auto rhs_node = std::make_unique<ExprNode>(std::move(*right));
      auto binary_expr =
          make_binary(BinaryOp::Or, std::move(lhs_node), std::move(rhs_node));
      expr = ExprNode{op_span.merge(right->span), std::move(binary_expr)};
    }
    return expr;
  }

  error::ParseResult<ExprNode> Parser::parse_logical_and() {
    auto expr = parse_equality();
    if (!expr.has_value()) {
      return std::nullopt;
    }
    if (check(TokenKind::And)) {
      auto op_span = current_span();
      advance();
      auto right = parse_equality();
      if (!right.has_value()) {
        return std::nullopt;
      }
      auto lhs_node = std::make_unique<ExprNode>(std::move(*expr));
      auto rhs_node = std::make_unique<ExprNode>(std::move(*right));
      auto binary_expr =
          make_binary(BinaryOp::And, std::move(lhs_node), std::move(rhs_node));
      expr = ExprNode{op_span.merge(right->span), std::move(binary_expr)};
    }
    return expr;
  }

  error::ParseResult<ExprNode> Parser::parse_equality() {
    auto expr = parse_comparison();
    if (!expr.has_value()) {
      return std::nullopt;
    }
    while (check_any({TokenKind::EqualEqual, TokenKind::BangEqual})) {
      auto op_span = current_span();
      auto op_kind = current_token->kind;
      advance();
      auto right = parse_comparison();
      if (!right.has_value()) {
        return std::nullopt;
      }
      auto lhs_node = std::make_unique<ExprNode>(std::move(*expr));
      auto rhs_node = std::make_unique<ExprNode>(std::move(*right));
      BinaryOp op;
      if (op_kind == TokenKind::EqualEqual) {
        op = BinaryOp::Eq;
      } else {
        op = BinaryOp::NotEq;
      }
      auto binary_expr =
          make_binary(op, std::move(lhs_node), std::move(rhs_node));
      expr = ExprNode{op_span.merge(right->span), std::move(binary_expr)};
    }
    return expr;
  }

  error::ParseResult<ExprNode> Parser::parse_comparison() {
    auto expr = parse_term();
    if (!expr.has_value()) {
      return std::nullopt;
    }
    while (check_any({TokenKind::Less, TokenKind::LessEqual, TokenKind::Greater, TokenKind::GreaterEqual})) {
      auto op_span = current_span();
      auto op_kind = current_token->kind;
      advance();
      auto right = parse_term();
      if (!right.has_value()) {
        return std::nullopt;
      }
      auto lhs_node = std::make_unique<ExprNode>(std::move(*expr));
      auto rhs_node = std::make_unique<ExprNode>(std::move(*right));
      BinaryOp op;
      switch (op_kind) {
        case TokenKind::Less:
          op = BinaryOp::Lt;
          break;
        case TokenKind::LessEqual:
          op = BinaryOp::Le;
          break;
        case TokenKind::Greater:
          op = BinaryOp::Gt;
          break;
        case TokenKind::GreaterEqual:
          op = BinaryOp::Ge;
          break;
        default:
          return unexpected<ExprNode>("in comparison operator");
      }
      auto binary_expr =
          make_binary(op, std::move(lhs_node), std::move(rhs_node));
      expr = ExprNode{op_span.merge(right->span), std::move(binary_expr)};
    }
    return expr;
  }

  error::ParseResult<ExprNode> Parser::parse_term() {
    auto expr = parse_factor();
    if (!expr.has_value()) {
      return std::nullopt;
    }
    while (check_any({TokenKind::Plus, TokenKind::Minus})) {
      auto op_span = current_span();
      auto op_kind = current_token->kind;
      advance();
      auto right = parse_factor();
      if (!right.has_value()) {
        return std::nullopt;
      }
      auto lhs_node = std::make_unique<ExprNode>(std::move(*expr));
      auto rhs_node = std::make_unique<ExprNode>(std::move(*right));
      BinaryOp op;
      switch (op_kind) {
        case TokenKind::Plus:
          op = BinaryOp::Add;
          break;
        case TokenKind::Minus:
          op = BinaryOp::Sub;
          break;
        default:
          return unexpected<ExprNode>("Unexpected in term operator");
      }
      auto binary_expr =
          make_binary(op, std::move(lhs_node), std::move(rhs_node));
      expr = ExprNode{op_span.merge(right->span), std::move(binary_expr)};
    }
    return expr;
  }

  error::ParseResult<ExprNode> Parser::parse_factor() {
    auto expr = parse_unary();
    if (!expr.has_value()) {
      return std::nullopt;
    }
    while (check_any({TokenKind::Star, TokenKind::Slash})) {
      auto op_span = current_span();
      auto op_kind = current_token->kind;
      advance();
      auto right = parse_unary();
      if (!right.has_value()) {
        return std::nullopt;
      }
      auto lhs_node = std::make_unique<ExprNode>(std::move(*expr));
      auto rhs_node = std::make_unique<ExprNode>(std::move(*right));
      BinaryOp op;
      switch (op_kind) {
        case TokenKind::Star:
          op = BinaryOp::Mul;
          break;
        case TokenKind::Slash:
          op = BinaryOp::Div;
          break;
        default:
          return unexpected<ExprNode>("Unexpected in factor operator");
      }
      auto binary_expr =
          make_binary(op, std::move(lhs_node), std::move(rhs_node));
      expr = ExprNode{op_span.merge(right->span), std::move(binary_expr)};
    }
    return expr;
  }

  error::ParseResult<ExprNode> Parser::parse_unary() {
    if (check_any({TokenKind::Bang, TokenKind::Minus, TokenKind::BitwiseAnd, TokenKind::Star})) {
      auto op_span = current_span();
      auto op_kind = current_token->kind;
      advance();
      auto expr = parse_unary();
      if (!expr.has_value()) {
        return std::nullopt;
      }
      UnaryOp op;
      switch (op_kind) {
        case TokenKind::Bang:
          op = UnaryOp::Not;
          break;
        case TokenKind::Minus:
          op = UnaryOp::Neg;
          break;
        case TokenKind::BitwiseAnd:
          op = UnaryOp::AddrOf;
          break;
        case TokenKind::Star:
          op = UnaryOp::Deref;
          break;
        default:
          return unexpected<ExprNode>("Unexpected in unary operator");
      }
      auto expr_node = std::make_unique<ExprNode>(std::move(*expr));
      auto unary_expr = make_unary(op, std::move(expr_node));
      return ExprNode{op_span.merge(expr->span), std::move(unary_expr)};
    }
    return parse_postfix();
  }

  error::ParseResult<ExprNode> Parser::parse_postfix() {
    auto expr = parse_primary();
    if (!expr.has_value()) {
      return std::nullopt;
    }
    while (true) {
      if (!current_token.has_value())
        return expr;
      switch (current_token->kind) {
        case TokenKind::LeftParen: {
          auto call_result = finish_call(std::move(*expr));
          if (!call_result.has_value()) {
            return std::nullopt;
          }
          expr = std::move(*call_result);
          continue;
        }
        case TokenKind::Dot: {
          advance();// consume '.'
          auto field_token = consume(TokenKind::Identifier, "Expected field name.");
          if (!field_token.has_value()) {
            return std::nullopt;
          }
          auto object_node = std::make_unique<ExprNode>(std::move(*expr));
          auto member_expr =
              make_member(std::move(object_node), field_token->lexeme);
          expr = ExprNode{current_span(), std::move(member_expr)};
          continue;
        }
        case TokenKind::LeftBracket: {
          advance();// consume '['
          auto index_expr = parse_expr();
          if (!index_expr.has_value()) {
            return std::nullopt;
          }
          if (!consume(TokenKind::RightBracket, "Expected ']' after index.")) {
            return std::nullopt;
          }
          auto object_node = std::make_unique<ExprNode>(std::move(*expr));
          auto index_node = std::make_unique<ExprNode>(std::move(*index_expr));
          auto index_expr_node =
              make_index(std::move(object_node), std::move(index_node));
          expr = ExprNode{current_span(), std::move(index_expr_node)};
          continue;
        }
        case TokenKind::PlusPlus: {
          advance();// consume '++'
          auto object_node = std::make_unique<ExprNode>(std::move(*expr));
          auto inc_expr = make_unary(UnaryOp::Inc, std::move(object_node));
          expr = ExprNode{current_span(), std::move(inc_expr)};
          continue;
        }
        case TokenKind::MinusMinus: {
          advance();// consume '--'
          auto object_node = std::make_unique<ExprNode>(std::move(*expr));
          auto dec_expr = make_unary(UnaryOp::Dec, std::move(object_node));
          expr = ExprNode{current_span(), std::move(dec_expr)};
          continue;
        }
        default:
          return expr;
      }
    }
  }

  error::ParseResult<ExprNode> Parser::parse_primary() {
    if (!current_token.has_value()) {
      return unexpected<ExprNode>("Unexpected at end of file");
    }
    auto span = current_span();
    switch (current_token->kind) {
      case TokenKind::IntLiteral: {
        auto value = std::stoll(current_token->lexeme);
        auto expr = make_int_literal(value);
        advance();
        return ExprNode{span, std::move(expr)};
      }
      case TokenKind::FloatLiteral: {
        auto value = std::stod(current_token->lexeme);
        auto expr = make_float_literal(value);
        advance();
        return ExprNode{span, std::move(expr)};
      }
      case TokenKind::StringLiteral: {
        auto value = current_token->lexeme;
        auto expr = make_string_literal(value);
        advance();
        return ExprNode{span, std::move(expr)};
      }
      case TokenKind::CharLiteral: {
        auto value = current_token->lexeme[0];
        auto expr = make_char_literal(value);
        advance();
        return ExprNode{span, std::move(expr)};
      }
      case TokenKind::True: {
        auto expr = make_bool_literal(true);
        advance();
        return ExprNode{span, std::move(expr)};
      }
      case TokenKind::False: {
        auto expr = make_bool_literal(false);
        advance();
        return ExprNode{span, std::move(expr)};
      }
      case TokenKind::Identifier: {
        auto name = current_token->lexeme;
        auto expr = make_variable(name);
        advance();
        return ExprNode{span, std::move(expr)};
      }
      case TokenKind::LeftParen: {
        advance();// consume '('
        auto expr = parse_expr();
        if (!expr.has_value()) {
          return std::nullopt;
        }
        consume(TokenKind::RightParen, "Expected ')' after expression.");
        return expr;
      }
      case TokenKind::LeftBracket: {
        advance();// consume '['
        vector<uptr<ExprNode>> elements;
        while (!check(TokenKind::RightBracket)) {
          auto element = parse_expr();
          if (!element.has_value()) {
            return std::nullopt;
          }
          elements.push_back(std::make_unique<ExprNode>(std::move(*element)));
          if (check(TokenKind::Comma)) {
            advance();// consume ','
          } else {
            break;
          }
        }
        consume(TokenKind::RightBracket, "Expected ']' after list expression.");
        auto list_expr = make_list(std::move(elements));
        return ExprNode{span, std::move(list_expr)};
      }
      case TokenKind::Sself: {
        auto expr = make_variable("self");
        advance();
        return ExprNode{span, std::move(expr)};
      }
      default:
        return unexpected<ExprNode>("Unexpected in primary expression");
    }
  }

  error::ParseResult<Block> Parser::parse_block() {
    consume(TokenKind::LeftBrace, "Expected '{' at the beginning of block.");
    auto stmts = vector<StmtNode>{};
    while (!check(TokenKind::RightBrace) && !is_at_end()) {
      auto stmt = parse_stmt();
      if (stmt.has_value()) {
        stmts.push_back(std::move(*stmt));
      }
    }
    consume(TokenKind::RightBrace, "Expected '}' at the end of block.");
    return Block{std::move(stmts)};
  }

  error::ParseResult<ast::Type> Parser::parse_type() {
    switch (current_token->kind) {
      case TokenKind::Int: {
        advance();
        return ast::type_int();
      }
      case TokenKind::Float: {
        advance();
        return ast::type_float();
      }
      case TokenKind::Bool: {
        advance();
        return ast::type_bool();
      }
      case TokenKind::Char: {
        advance();
        return ast::type_char();
      }
      case TokenKind::Str: {
        advance();
        return ast::type_str();
      }
      case TokenKind::None: {
        advance();
        return ast::type_void();
      }
      case TokenKind::Identifier: {
        auto class_name = current_token->lexeme;
        advance();
        return ast::type_class(class_name);
      }
      case TokenKind::Ptr: {
        advance();
        consume(TokenKind::Less, "Expected '<' after 'ptr'.");
        auto pointee_type = parse_type();
        if (!pointee_type.has_value()) {
          return std::nullopt;
        }
        consume(TokenKind::Greater, "Expected '>' after pointer type.");
        return type_ptr(std::move(*pointee_type));
      }
      case TokenKind::List: {
        advance();
        consume(TokenKind::Less, "Expected '<' after 'list'.");
        auto element_type = parse_type();
        if (!element_type.has_value()) {
          return std::nullopt;
        }
        consume(TokenKind::Greater, "Expected '>' after list type.");
        return type_list(std::move(*element_type));
      }
      default:
        return unexpected<ast::Type>("Unexpected in type");
    }
  }

  error::ParseResult<Parameter> Parser::parse_parameter() {
    optional<Token> param_token = std::nullopt;
    if (check(TokenKind::Sself)) {
      advance();// consume 'self'
    } else {
      param_token = consume(TokenKind::Identifier, "Expected parameter name.");
      if (!param_token.has_value()) {
        return std::nullopt;
      }
    }
    string param_name = param_token.has_value() ? param_token->lexeme : "self";
    consume(TokenKind::Colon, "Expected ':' after parameter name.");
    bool is_ref = false, is_own = false;
    if (check(TokenKind::Ref)) {
      advance();
      is_ref = true;
    } else if (check(TokenKind::Own)) {
      advance();
      is_own = true;
    }
    auto param_type = parse_type();
    if (!param_type.has_value()) {
      return std::nullopt;
    }
    // TODO:
    return Parameter{std::move(param_name), std::move(*param_type), is_ref, is_own};
  }

  error::ParseResult<FunctionDecl> Parser::parse_header() {
    auto func_tok = consume(TokenKind::Identifier, "Expected function name.");
    if (!func_tok.has_value()) {
      return std::nullopt;
    }
    string func_name = func_tok->lexeme;
    consume(TokenKind::LeftParen, "Expected '(' after function name.");
    auto parameters = vector<Parameter>{};
    while (!check(TokenKind::RightParen)) {
      auto param = parse_parameter();
      if (!param.has_value()) {
        return std::nullopt;
      }
      parameters.push_back(std::move(*param));
      if (check(TokenKind::Comma)) {
        advance();// consume ','
      } else {
        break;
      }
    }
    consume(TokenKind::RightParen, "Expected ')' after parameters.");
    std::optional<ast::Type> return_type = std::nullopt;
    if (check(TokenKind::Arrow)) {
      advance();// consume '->'
      auto ret_type = parse_type();
      if (!ret_type.has_value()) {
        return std::nullopt;
      }
      return_type = std::move(*ret_type);
    }
    return FunctionDecl{func_name, std::move(parameters), std::move(return_type)};
  }

  error::ParseResult<FunctionDecl> Parser::parse_func_decl() {
    consume(TokenKind::Decl, "Expected 'decl'  before function declaration.");
    auto header = parse_header();
    if (!header.has_value()) {
      return std::nullopt;
    }
    consume(TokenKind::Semicolon, "Expected ';' after function declaration.");
    return header;
  }

  error::ParseResult<FunctionDef> Parser::parse_function() {
    consume(TokenKind::Def, "Expected 'def' before function definition.");
    auto header = parse_header();
    if (!header.has_value()) {
      return std::nullopt;
    }
    auto body = parse_block();
    if (!body.has_value()) {
      return std::nullopt;
    }
    return FunctionDef{std::move(*header), std::move(*body)};
  }

  error::ParseResult<Trait> Parser::parse_trait() {
    consume(TokenKind::Trait, "Expected 'trait' before trait definition.");
    auto trait_name_token =
        consume(TokenKind::Identifier, "Expected trait name after 'trait'.");
    if (!trait_name_token.has_value()) {
      return std::nullopt;
    }
    string trait_name = trait_name_token->lexeme;
    consume(TokenKind::LeftBrace, "Expected '{' after trait name.");
    auto methods = vector<FunctionDecl>{};
    while (!check(TokenKind::RightBrace) && !is_at_end()) {
      auto method = parse_func_decl();
      if (!method.has_value()) {
        return std::nullopt;
      }
      methods.push_back(std::move(*method));
    }
    consume(TokenKind::RightBrace, "Expected '}' after trait definition.");
    return Trait{std::move(trait_name), std::move(methods)};
  }

  error::ParseResult<Impl> Parser::parse_impl() {
    consume(TokenKind::Impl, "Expected 'impl' before impl block.");
    auto trait_or_class_token = consume(
        TokenKind::Identifier, "Expected trait or class name after 'impl'."
    );
    if (!trait_or_class_token.has_value()) {
      return std::nullopt;
    }
    string trait_or_class_name = trait_or_class_token->lexeme;
    optional<string> trait_name = std::nullopt;
    if (check(TokenKind::For)) {
      advance();// consume 'for'
      auto class_token =
          consume(TokenKind::Identifier, "Expected class name after 'for'.");
      if (!class_token.has_value()) {
        return std::nullopt;
      }
      trait_name = std::move(trait_or_class_name);
      trait_or_class_name = class_token->lexeme;
    }
    consume(TokenKind::LeftBrace, "Expected '{' after impl header.");
    auto methods = vector<FunctionDef>{};
    while (!check(TokenKind::RightBrace) && !is_at_end()) {
      auto method = parse_function();
      if (!method.has_value()) {
        return std::nullopt;
      }
      methods.push_back(std::move(*method));
    }
    consume(TokenKind::RightBrace, "Expected '}' after impl block.");
    return Impl{std::move(trait_name), std::move(trait_or_class_name), std::move(methods)};
  }

  error::ParseResult<Field> Parser::parse_field() {
    consume(TokenKind::Let, "Expected 'let' at the beginning of field definition.");
    auto field_token =
        consume(TokenKind::Identifier, "Expected identifier after 'let'.");
    if (!field_token.has_value()) {
      return std::nullopt;
    }
    string field_name = field_token->lexeme;
    std::optional<ast::Type> ty = std::nullopt;
    consume(TokenKind::Colon, "Expected ':' after field name.");
    auto field_type = parse_type();
    if (!field_type.has_value()) {
      return std::nullopt;
    }
    optional<ExprNode> init = std::nullopt;
    if (check(TokenKind::Equal)) {
      advance();// consume '='
      auto init_expr = parse_expr();
      if (!init_expr.has_value()) {
        return std::nullopt;
      }
      init = std::move(*init_expr);
    }
    consume(TokenKind::Semicolon, "Expected ';' after field definition.");
    return Field{std::move(field_name), std::move(*field_type), std::move(init)};
  }

  error::ParseResult<Class> Parser::parse_class() {
    consume(TokenKind::Class, "Expected 'class' before class definition.");
    auto class_name_token =
        consume(TokenKind::Identifier, "Expected class name after 'class'.");
    if (!class_name_token.has_value()) {
      return std::nullopt;
    }
    string class_name = class_name_token->lexeme;
    consume(TokenKind::LeftBrace, "Expected '{' after class name.");
    auto fields = vector<Field>{};
    while (!check(TokenKind::RightBrace) && !is_at_end()) {
      auto field = parse_field();
      if (!field.has_value()) {
        return std::nullopt;
      }
      fields.push_back(std::move(*field));
    }
    consume(TokenKind::RightBrace, "Expected '}' after class definition.");
    return Class{std::move(class_name), std::move(fields)};
  }

  error::ParseResult<GlobalVar> Parser::parse_global_var() {
    consume(TokenKind::Let, "Expected 'let' at the beginning of global variable definition.");
    auto var_token =
        consume(TokenKind::Identifier, "Expected identifier after 'let'.");
    if (!var_token.has_value()) {
      return std::nullopt;
    }
    string var_name = var_token->lexeme;
    std::optional<ast::Type> ty = std::nullopt;
    if (check(TokenKind::Colon)) {
      advance();// consume ':'
      auto type = parse_type();
      if (!type.has_value()) {
        return std::nullopt;
      }
      ty = std::move(*type);
    }
    std::optional<ExprNode> init = std::nullopt;
    if (check(TokenKind::Equal)) {
      advance();// consume '='
      auto init_expr = parse_expr();
      if (!init_expr.has_value()) {
        return std::nullopt;
      }
      init = std::move(*init_expr);
    }
    consume(TokenKind::Semicolon, "Expected ';' after global variable definition.");
    return GlobalVar{std::move(var_name), std::move(ty), std::move(init)};
  }

  error::ParseResult<std::vector<uptr<ExprNode>>> Parser::parse_arguments() {
    auto args = std::vector<uptr<ExprNode>>{};
    while (!check(TokenKind::RightParen)) {
      auto arg = parse_expr();
      if (!arg.has_value()) {
        return std::nullopt;
      }
      args.push_back(std::make_unique<ExprNode>(std::move(*arg)));
      if (check(TokenKind::Comma)) {
        advance();// consume ','
      } else {
        break;
      }
    }
    return args;
  }

  error::ParseResult<ExprNode> Parser::finish_call(ExprNode callee) {
    consume(TokenKind::LeftParen, "Expected '(' after callee.");
    auto args = parse_arguments();
    if (!args.has_value()) {
      return std::nullopt;
    }
    consume(TokenKind::RightParen, "Expected ')' after arguments.");
    auto callee_node = std::make_unique<ExprNode>(std::move(callee));
    auto call_expr = make_call(std::move(callee_node), std::move(*args));
    return ExprNode{current_span(), std::move(call_expr)};
  }
}// namespace cat
