#pragma once
#include "common.h"
#include "expr.h"
#include "item.h"
#include <cstdint>
#include <llvm-20/llvm/ADT/StringMap.h>
#include <llvm-20/llvm/IR/BasicBlock.h>
#include <llvm-20/llvm/IR/DerivedTypes.h>
#include <llvm-20/llvm/IR/IRBuilder.h>
#include <llvm-20/llvm/IR/LLVMContext.h>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/IR/Type.h>
#include <llvm-20/llvm/IR/Value.h>
namespace cat::ir {

struct VarInfo {
  llvm::Value *ptr;
  llvm::Type *alloca_ty;
  llvm::Type *value_ty;
  bool is_ref = false;
  bool is_own = false;
  vector<llvm::Type *> deref_chain;
  llvm::FunctionType *func_ty = nullptr;
};

struct LoopInfo {
  llvm::BasicBlock *cond_bb;
  llvm::BasicBlock *exit_bb;
};

struct ClassInfo {
  llvm::StructType *struct_ty = nullptr;
  vector<string> field_names;
  vector<ast::Type> field_types;
  llvm::StringMap<uint32_t> field_indices;
  vector<optional<const ExprNode *>> field_defaults;
  llvm::StringMap<string> methods;

  ClassInfo() = default;
  ClassInfo(const ClassInfo &) = delete;
  ClassInfo &operator=(const ClassInfo &) = delete;
  ClassInfo(ClassInfo &&) = default;
  ClassInfo &operator=(ClassInfo &&) = default;
};

struct ListType {
  llvm::StructType *struct_ty;
  llvm::Type *elem_ty;
};

struct CodeGenCtxt {
  uptr<llvm::LLVMContext> llvm_ctx;
  uptr<llvm::IRBuilder<>> builder;
  uptr<llvm::Module> module;
  llvm::StringMap<uptr<ClassInfo>> class_registry;
  llvm::StringMap<uptr<ListType>> list_types;
  uint32_t str_counter = 0;
  uint32_t lambda_counter = 0;

  CodeGenCtxt(const string &module_name);
};
} // namespace cat::ir
