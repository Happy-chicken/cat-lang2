#include "symbol_table.h"
#include <cassert>

namespace cat {

SymbolTable::SymbolTable() {
  scopes.emplace_back();
}

void SymbolTable::enter_scope() {
  scopes.emplace_back();
}

void SymbolTable::exit_scope() {
  assert(scopes.size() >= 2 && "Cannot pop the global scope");
  scopes.pop_back();
}

Symbol *SymbolTable::declare(Symbol sym) {
  auto &current = scopes.back();
  auto [it, inserted] =
      current.try_emplace(sym.get_name(), std::move(sym));
  return inserted ? &it->second : nullptr;
}

Symbol *SymbolTable::resolve(const string &name) {
  for (auto it = scopes.rbegin(); it != scopes.rend(); ++it) {
    auto found = it->find(name);
    if (found != it->end()) {
      return &found->second;
    }
  }
  return nullptr;
}

Symbol *SymbolTable::resolve_global(const string &name) const {
  if (scopes.empty()) return nullptr;
  auto found = scopes.front().find(name);
  if (found != scopes.front().end()) {
    return const_cast<Symbol *>(&found->second);
  }
  return nullptr;
}

} // namespace cat
