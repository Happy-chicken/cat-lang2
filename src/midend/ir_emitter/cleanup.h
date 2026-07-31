#pragma once
#include "codegen_ctx.h"
#include "common.h"
#include <cstdint>
#include <llvm-20/llvm/IR/DerivedTypes.h>
#include <llvm-20/llvm/IR/Type.h>
#include <llvm-20/llvm/IR/Value.h>

namespace cat::ast {
struct Type;
} // namespace cat::ast

namespace cat::ir {

struct Env;

struct CleanupInfo {
  llvm::Value *alloca;
  llvm::Type *alloca_ty;
  bool is_class;
  llvm::StructType *list_st;
  bool cancelled = false;
  bool free_base_ptr = false;
  bool is_str = false;
};

enum class CleanupKind : uint8_t { None, ClassFree, ListDataFree, OwnListFree, StrDataFree };

class CleanupManager {
public:
  explicit CleanupManager(CodeGenCtxt &ctx);

  static CleanupKind classify_type(const ast::Type &ty);

  void register_class_cleanup(Env &env, llvm::Value *alloca,
                              llvm::Type *alloca_ty);
  void register_list_cleanup(Env &env, llvm::Value *alloca,
                             llvm::Type *alloca_ty,
                             llvm::StructType *list_st);
  void register_own_list_cleanup(Env &env, llvm::Value *alloca,
                                 llvm::Type *alloca_ty,
                                 llvm::StructType *list_st);
  void register_str_cleanup(Env &env, llvm::Value *alloca,
                            llvm::Type *alloca_ty,
                            llvm::StructType *str_st);

  void emit_scope_cleanup(Env &scope);
  void emit_all_cleanups(Env &env);
  void emit_until_loop(Env &env);
  void emit_var_free(Env &env, llvm::Value *alloca);

  bool cancel_cleanup(Env &env, llvm::Value *alloca);

private:
  llvm::Function *declare_runtime_func(const string &name, llvm::Type *ret,
                                       vector<llvm::Type *> param_tys,
                                       bool var_arg = false);

  CodeGenCtxt &ctx;
};

} // namespace cat::ir
