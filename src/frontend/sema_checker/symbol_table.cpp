#include "symbol_table.h"
#include <cassert>

namespace cat {

SymbolTable::SymbolTable() {
  scopes.emplace_back(ScopeKind::Global);
}

void SymbolTable::enter_scope(ScopeKind kind) {
  scopes.emplace_back(kind);
}

void SymbolTable::exit_scope() {
  assert(scopes.size() >= 2 && "Cannot pop the global scope");
  scopes.pop_back();
}

Symbol *SymbolTable::declare(Symbol sym) {
  auto sptr = std::make_shared<Symbol>(std::move(sym));
  auto existing = scopes.back().insert(std::move(sptr));
  return existing ? existing.get() : nullptr;
}

Symbol *SymbolTable::resolve(const string &name) const {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->get(name);
    if (found) {
      return found.get();
    }
  }
  return nullptr;
}

Symbol *SymbolTable::resolve_global(const string &name) const {
  if (scopes.empty()) return nullptr;
  return scopes.front().get(name).get();
}

bool SymbolTable::nearest_of_kind(ScopeKind kind) const {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    if (it->kind == kind) return true;
  }
  return false;
}

} // namespace cat
