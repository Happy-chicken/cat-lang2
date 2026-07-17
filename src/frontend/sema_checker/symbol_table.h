#pragma once

#include "common/common.h"
#include "scope.h"
#include "symbol.h"
#include <cassert>
#include <vector>

namespace cat {

class SymbolTable {
public:
  SymbolTable();

  SymbolTable(const SymbolTable &) = delete;
  SymbolTable &operator=(const SymbolTable &) = delete;
  SymbolTable(SymbolTable &&) = delete;
  SymbolTable &operator=(SymbolTable &&) = delete;

  void enter_scope(ScopeKind kind);
  void exit_scope();

  Symbol *declare(Symbol sym);

  Symbol *resolve(const string &name) const;
  Symbol *resolve_global(const string &name) const;

  bool nearest_of_kind(ScopeKind kind) const;
  size_t depth() const noexcept { return scopes.size(); }

private:
  vector<Scope> scopes;
};

} // namespace cat
