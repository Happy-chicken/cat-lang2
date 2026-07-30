#pragma once
#include "../../frontend/ast/type.h"
#include "builtin_registry.h"
#include "cleanup.h"
#include "codegen_ctx.h"
#include "common.h"
#include "diag.h"
#include "env.h"
#include "expr.h"
#include "item.h"
#include "sema_ctx.h"
#include "stmt.h"
#include <unordered_set>
namespace cat::ir {

class IrEmitter {
public:
  IrEmitter(const string &name, error::DiagCtxt &diag,
            semantics::SemaCtxt &sema_ctx);
  llvm::Type *ast_type_to_llvm_type(const ast::Type &ast_type);

  struct ModuleHandle {
    uptr<llvm::LLVMContext> ctx;
    uptr<llvm::Module> mod;
  };
  ModuleHandle release_module();
  const llvm::Module &get_module() const { return *ctx->module; }

  void dump_module(std::ostream &os);
  llvm::Type *infer_lit_type(const Expr &expr);
  void compile(const Program &program);
  void build_classes(const Program &program);
  void build_class_constructors();
  void compile_class_constructor(const string &name,
                                 llvm::StructType *struct_ty,
                                 const ClassInfo &cls_info);
  void compile_class_clone(const string &name, llvm::StructType *struct_ty,
                           const ClassInfo &cls_info);
  void compile_global_var(const GlobalVar &gv, Span span);
  void compile_method(const string &cls_name, const FunctionDef &func,
                      Span span);
  void compile_function(const FunctionDef &func, Span span);
  void compile_named_function(const FunctionDef &func, const string &name,
                              Span span);
  void compile_block(const Block &block);
  void compile_stmt(const StmtNode &stmt_node);
  llvm::Value *compile_expr(const ExprNode &expr);
  llvm::Value *compile_call(const CallExpr &call, Span span);
  llvm::Value *emit_list_literal(const vector<uptr<ExprNode>> &elements,
                                 Span span);
  void emit_list_with_init(llvm::Value *list_alloca,
                           const vector<uptr<ExprNode>> &elements, Span span);
  void emit_list_with_init_fields(llvm::Value *list_alloca,
                                  const vector<uptr<ExprNode>> &elements,
                                  llvm::Type *elem_ty, Span span);
  void compile_if(const IfStmt &if_stmt);
  void compile_while(const LoopStmt &loop_stmt);
  llvm::Value *compile_index(const ExprNode &object, const ExprNode &index);
  llvm::Value *compile_index_ptr(const ExprNode &expr_node);
  void emit_bounds_check(llvm::Value *index, llvm::Value *len, Span span);
  llvm::Value *compile_assignment(const AssignExpr &assign_expr);

  ListType *lookup_or_create_list_type(llvm::Type *elem_ty);
  ListType *lookup_list_type_by_struct(llvm::StructType *st);
  StrType *get_str_type();

  llvm::Value *emit_builtin_method(const runtime::BuiltinMethod &desc,
                                   llvm::StructType *st, llvm::Type *elem_ty,
                                   const ExprNode &obj_expr,
                                   const vector<uptr<ExprNode>> &args,
                                   Span span);

  llvm::Value *compile_member_access(const ExprNode &object,
                                     const string &name);
  llvm::Value *compile_member_ptr(const ExprNode &expr_node);
  llvm::Value *compile_literal(const LiteralExpr &lit);
  llvm::Value *emit_string_literal(const string &s);
  llvm::Value *compile_binary(const BinaryExpr &binary_expr);
  llvm::Value *compile_unary(const UnaryExpr &unary_expr);
  llvm::Value *compile_lambda(const LambdaExpr &lambda);
  void collect_free_vars(const StmtNode &stmt,
                         const unordered_set<string> &params,
                         unordered_set<string> &captured);
  void collect_free_vars_expr(const ExprNode &expr,
                              const unordered_set<string> &params,
                              unordered_set<string> &captured);

public:
  class EnvGuard {
  public:
    EnvGuard(IrEmitter &ir_emitter, sptr<Env> enclosing_env)
        : ir_emitter{ir_emitter}, previous_env{ir_emitter.env} {
      ir_emitter.env = std::move(enclosing_env);
    }

    ~EnvGuard() { ir_emitter.env = std::move(previous_env); }

  private:
    IrEmitter &ir_emitter;
    sptr<Env> previous_env;
  };

private:
  llvm::Type *llvm_type(const ast::Type &ast_type);
  llvm::FunctionType *llvm_func_type(const ast::Type &ast_type);
  llvm::Type *ptr_pointee_llvm_type(const ast::Type &ast_type);
  vector<llvm::Type *> ptr_deref_chain(const ast::Type &ast_type);
  llvm::Function *declare_runtime_func(const string &name, llvm::Type *ret_ty,
                                       vector<llvm::Type *> param_tys,
                                       bool is_var_arg = false);

  vector<llvm::Value *> compile_args(llvm::Function *fn,
                                     const vector<uptr<ExprNode>> &args,
                                     size_t param_offset,
                                     const ClassInfo *ctor_info = nullptr);
  void invalidate_owned_args(const string &fn_name,
                             const vector<uptr<ExprNode>> &args,
                             size_t param_offset);

  struct ParamTypeInfo {
    llvm::Type *param_ty = nullptr;
    llvm::Type *value_ty = nullptr;
    BorrowKind borrow_kind = BorrowKind::None;
  };
  ParamTypeInfo resolve_param_type(const ast::Type &ty);

  llvm::Value *load_variable(const string &name, Span span);
  llvm::Value *variable_ptr(const string &name, Span span);

  void invalidate_source(const ExprNode &src_expr);
  void compile_var_def(const VarDefStmt &s);

  runtime::IrGenCtxtRef make_ir_gen_context();

  llvm::Value *compile_direct_or_ctor_call(const Variable &callee,
                                           const CallExpr &call, Span span);
  llvm::Value *compile_indirect_call(const Variable &callee,
                                     const CallExpr &call, Span span);
  llvm::Value *compile_method_call(const MemberExpr &callee,
                                   const CallExpr &call, Span span);

private:
  uptr<CodeGenCtxt> ctx;
  sptr<Env> env;
  CleanupManager cleanup_mgr;
  error::DiagCtxt &diag;
  llvm::Function *current_function;
  semantics::SemaCtxt &sema;
};

} // namespace cat::ir
