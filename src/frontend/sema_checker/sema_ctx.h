#pragma once
#include "symbol_table.h"
#include "type_ctx.h"
namespace cat::semantics {
  class SemaCtxt {
public:
    SemaCtxt() : symbol_table{}, type_ctxt{} {}

    SymbolTable &get_symbol_table() { return symbol_table; }
    TypeCtxt &get_type_ctxt() { return type_ctxt; }

private:
    SymbolTable symbol_table;
    TypeCtxt type_ctxt;
  };
}// namespace cat::semantics
