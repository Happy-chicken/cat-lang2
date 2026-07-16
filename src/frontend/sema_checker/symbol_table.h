#pragma once

#include "common/common.h"
#include "symbol.h"
#include <cassert>
#include <vector>

namespace cat {

  class SymbolTable {
public:
    SymbolTable();

    SymbolTable(const SymbolTable &) = delete;
    SymbolTable &operator=(const SymbolTable &) = delete;
    SymbolTable(SymbolTable &&) = default;
    SymbolTable &operator=(SymbolTable &&) = default;

    void enter_scope();
    void exit_scope();

    Symbol *declare(Symbol sym);

    Symbol *resolve(const string &name);
    Symbol *resolve_global(const string &name) const;

    size_t depth() const noexcept { return scopes.size(); }

private:
    using Scope = unordered_map<string, Symbol>;
    vector<Scope> scopes;
  };

}// namespace cat
