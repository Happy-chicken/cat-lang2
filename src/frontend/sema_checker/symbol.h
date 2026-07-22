#pragma once

#include "common/common.h"
#include "common/span.h"
#include "frontend/ast/type.h"

namespace cat {

  // ---------- SymbolKind 各个变体 ----------
  struct VariableData {
    bool is_mutable;
    bool is_initialized;
    optional<size_t> known_list_len;
  };

  struct FunctionData {
    vector<ast::Type> params;
    ast::Type return_type;
  };

  struct TypeData {};// 仅标记类型

  struct ParameterData {
    bool is_ref;
    bool is_own;
  };

  struct ClassData {
    vector<std::pair<string, ast::Type>> fields;
    vector<bool> has_default;
  };

  struct TraitData {
    vector<string> methods;
  };

  using SymbolKind = std::variant<VariableData, FunctionData, TypeData, ParameterData, ClassData, TraitData>;

  struct Symbol {
public:
    // 禁止拷贝
    Symbol(const Symbol &) = delete;
    Symbol &operator=(const Symbol &) = delete;

    // 允许移动
    Symbol(Symbol &&) = default;
    Symbol &operator=(Symbol &&) = default;

    static Symbol new_variable(string name, optional<ast::Type> ty, bool is_mutable, Span span, optional<size_t> list_len = std::nullopt);

    static Symbol new_function(string name, vector<ast::Type> params, ast::Type return_type, Span span);

    static Symbol new_parameter(string name, ast::Type ty, bool is_ref, bool is_own, Span span);

    static Symbol new_type(string name, Span span);

    static Symbol new_class(string name, vector<std::pair<string, ast::Type>> fields, vector<bool> has_default, Span span);

    static Symbol new_trait(string name, vector<string> methods, Span span);

    bool is_type() const { return std::holds_alternative<TypeData>(kind); }

    bool is_class() const { return std::holds_alternative<ClassData>(kind); }

    bool is_trait() const { return std::holds_alternative<TraitData>(kind); }

    bool is_variable() const { return std::holds_alternative<VariableData>(kind); }

    bool is_function() const { return std::holds_alternative<FunctionData>(kind); }

    bool is_ref() const {
      if (auto *param = std::get_if<ParameterData>(&kind)) {
        return param->is_ref;
      }
      return false;
    }

    bool is_own() const {
      if (auto *param = std::get_if<ParameterData>(&kind)) {
        return param->is_own;
      }
      return false;
    }

    bool is_callable() const;

    const char *kindname() const;

    const SymbolKind &get_kind() const noexcept { return kind; }
    const string &get_name() const noexcept { return name; }
    const optional<ast::Type> &get_type() const noexcept { return type; }
    size_t get_scope_depth() const noexcept { return scope_depth; }
    void set_scope_depth(size_t depth) noexcept { scope_depth = depth; }
    const Span &get_span() const noexcept { return span_; }

private:
    SymbolKind kind;
    string name;
    optional<ast::Type> type;
    size_t scope_depth;
    Span span_;

    Symbol(SymbolKind kind, string name, optional<ast::Type> ty, size_t scope_depth, Span span);
  };

}// namespace cat
