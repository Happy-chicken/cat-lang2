#include "symbol.h"

namespace cat {

Symbol::Symbol(SymbolKind kind, string name, optional<ast::Type> ty,
               size_t scope_depth, Span span)
    : kind(std::move(kind)), name(std::move(name)), type(std::move(ty)),
      scope_depth(scope_depth), span_(span) {}

Symbol Symbol::new_variable(string name, optional<ast::Type> ty,
                            bool is_mutable, Span span) {
  VariableData data{is_mutable, false};
  SymbolKind kind = std::move(data);
  return Symbol(std::move(kind), std::move(name), std::move(ty), 0, span);
}

Symbol Symbol::new_function(string name, vector<ast::Type> params,
                            ast::Type return_type, Span span) {
  FunctionData data{std::move(params), std::move(return_type)};
  SymbolKind kind = std::move(data);
  return Symbol(std::move(kind), std::move(name), std::nullopt, 0, span);
}

Symbol Symbol::new_parameter(string name, ast::Type ty, bool is_ref,
                             Span span) {
  ParameterData data{is_ref};
  SymbolKind kind = std::move(data);
  return Symbol(std::move(kind), std::move(name), std::move(ty), 0, span);
}

Symbol Symbol::new_type(string name, Span span) {
  SymbolKind kind = TypeData{};
  return Symbol(std::move(kind), std::move(name), std::nullopt, 0, span);
}

Symbol Symbol::new_class(string name,
                         vector<std::pair<string, ast::Type>> fields,
                         Span span) {
  ClassData data{std::move(fields)};
  SymbolKind kind = std::move(data);
  return Symbol(std::move(kind), std::move(name), std::nullopt, 0, span);
}

Symbol Symbol::new_trait(string name, vector<string> methods, Span span) {
  TraitData data{std::move(methods)};
  SymbolKind kind = std::move(data);
  return Symbol(std::move(kind), std::move(name), std::nullopt, 0, span);
}

bool Symbol::is_callable() const {
  return std::visit(
      [](const auto &v) -> bool {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, FunctionData> ||
                      std::is_same_v<T, ClassData>) {
          return true;
        }
        return false;
      },
      kind);
}

const char *Symbol::kindname() const {
  return std::visit(
      [](const auto &v) -> const char * {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, VariableData>)
          return "variable";
        if constexpr (std::is_same_v<T, FunctionData>)
          return "function";
        if constexpr (std::is_same_v<T, TypeData>)
          return "type";
        if constexpr (std::is_same_v<T, ParameterData>)
          return "parameter";
        if constexpr (std::is_same_v<T, ClassData>)
          return "class";
        if constexpr (std::is_same_v<T, TraitData>)
          return "trait";
        return "unknown";
      },
      kind);
}

} // namespace cat
