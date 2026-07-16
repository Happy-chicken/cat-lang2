#pragma once
#include "../common/common.h"
#include "symbol.h"
namespace cat {
enum class ScopeKind { Global, Function, Block, Loop };

struct Scope {
  ScopeKind kind;
  unordered_map<string, sptr<Symbol>> symbols;

  Scope(ScopeKind kind) : kind(kind), symbols{} {}

  sptr<Symbol> get(const string &name) const {
    auto it = symbols.find(name);
    if (it != symbols.end()) {
      return it->second;
    }
    return nullptr;
  }

  sptr<Symbol> insert(sptr<Symbol> symbol) {
    auto it = symbols.find(symbol->get_name());
    if (it != symbols.end()) {
      return it->second;
    }
    symbols[symbol->get_name()] = std::move(symbol);
    return nullptr;
  }
};
} // namespace cat