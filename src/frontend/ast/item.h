#pragma once
#include "../common/common.h"
#include "expr.h"
#include "stmt.h"
#include "type.h"
namespace cat {

  struct FunctionDecl {
    std::string name;
    std::vector<Parameter> params;
    std::optional<ast::Type> return_type;
  };

  struct Field {
    std::string name;
    ast::Type ty;
    std::optional<ExprNode> init;
  };

  struct GlobalVar {
    std::string name;
    std::optional<ast::Type> ty;
    std::optional<ExprNode> init;
  };

  struct FunctionDef {
    FunctionDecl function_header;
    Block body;
  };

  struct Class {
    std::string name;
    std::vector<Field> fields;
  };

  struct Trait {
    std::string name;
    std::vector<FunctionDecl> methods;
  };

  struct Impl {
    std::optional<std::string> trait_name;
    std::string class_name;
    std::vector<FunctionDef> methods;
  };

  using Item =
      std::variant<FunctionDef, FunctionDecl, Class, Trait, Impl, GlobalVar>;

  struct ItemNode {
    Span span;
    Item item;
  };

  struct Program {
    std::vector<ItemNode> items;
  };

  inline Item make_function_def(FunctionDecl header, Block body) {
    return Item{FunctionDef{std::move(header), std::move(body)}};
  }

  inline Item make_function_decl(FunctionDecl header) {
    return Item{std::move(header)};
  }

  inline Item make_class(std::string name, std::vector<Field> fields) {
    return Item{Class{std::move(name), std::move(fields)}};
  }

  inline Item make_trait(std::string name, std::vector<FunctionDecl> methods) {
    return Item{Trait{std::move(name), std::move(methods)}};
  }

  inline Item make_impl(std::optional<std::string> trait_name, std::string class_name, std::vector<FunctionDef> methods) {
    return Item{
        Impl{std::move(trait_name), std::move(class_name), std::move(methods)}
    };
  }

  inline Item make_global_var(std::string name, std::optional<ast::Type> ty = std::nullopt, std::optional<ExprNode> init = std::nullopt) {
    return Item{GlobalVar{std::move(name), std::move(ty), std::move(init)}};
  }

}// namespace cat
