#pragma once
#include "symbol_table.h"
#include "type_ctx.h"
#include "builtin_registry.h"
namespace cat::semantics {
  class SemaCtxt {
public:
    SemaCtxt() : symbol_table{}, type_ctxt{}, builtins{} {
      builtins.init_defaults();
    }

    SymbolTable &get_symbol_table() { return symbol_table; }
    TypeCtxt &get_type_ctxt() { return type_ctxt; }
    runtime::BuiltinRegistry &get_builtins() { return builtins; }

private:
    SymbolTable symbol_table;
    TypeCtxt type_ctxt;
    runtime::BuiltinRegistry builtins;
  };
}// namespace cat::semantics
