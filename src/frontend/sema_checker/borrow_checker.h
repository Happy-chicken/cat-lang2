#pragma once
#include "item.h"
#include "pass_interface.h"
namespace cat {
class SymbolTable;
namespace runtime {
class BuiltinRegistry;
}

class BorrowChecker : public semantics::Pass {
public:
  const char *name() const noexcept override { return "BorrowChecker"; }

  bool run(Program &program, semantics::SemaCtxt &ctx,
           error::DiagCtxt &diag) override;

private:
  enum class VarState { Free, MutBorrowed, ImmutBorrowed, Moved };
  enum class ParamClass { Copy, Move, BorrowMut, BorrowImmut };

  struct VarInfo {
    VarState state = VarState::Free;
    string class_name;
    bool is_struct = false;
    bool is_cref = false;
  };

  void check_function(const FunctionDef &func, error::DiagCtxt &diag);
  void check_impl_methods(const Impl &imp, error::DiagCtxt &diag);

  void analyze_block(const Block &block, error::DiagCtxt &diag);
  void analyze_stmt(const StmtNode &stmt, error::DiagCtxt &diag);
  void analyze_if_stmt(const IfStmt &if_stmt, error::DiagCtxt &diag);
  void check_expr(const ExprNode &expr, error::DiagCtxt &diag,
                  bool is_write_target = false);
  void check_var_write(const ExprNode &expr, error::DiagCtxt &diag);

  void push_scope();
  void pop_scope();
  void release_borrow(const string &borrower);

  ParamClass classify_param(const ast::Type &ty) const;

  void mark_moved(const string &name);
  void mark_mut_borrowed(const string &name, const string &borrower);
  void mark_immut_borrowed(const string &name, const string &borrower);
  void mark_free(const string &name);

  VarState lookup_state(const string &name) const;

  void resolve_call_args(const CallExpr &call, error::DiagCtxt &diag);

  bool is_struct_var(const string &name) const;
  bool is_cref_var(const string &name) const;
  string get_class_name(const string &name) const;

  SymbolTable *sym_table = nullptr;
  runtime::BuiltinRegistry *builtins = nullptr;

  unordered_map<string, VarInfo> states;
  unordered_map<string, int> immut_count;
  unordered_map<string, string> borrow_map;
  vector<vector<string>> scopes;
};
} // namespace cat
