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
#include "common/borrow_kind.h"

namespace cat::ir {

struct VarInfo {
  llvm::Value *ptr;
  llvm::Type *alloca_ty;
  llvm::Type *value_ty;
  BorrowKind borrow_kind = BorrowKind::None;
  vector<llvm::Type *> deref_chain;
  llvm::FunctionType *func_ty = nullptr;
};

struct LoopInfo {
  llvm::BasicBlock *cond_bb;
  llvm::BasicBlock *exit_bb;
};

struct StructInfo {
  llvm::StructType *struct_ty = nullptr;
  vector<string> field_names;
  vector<ast::Type> field_types;
  llvm::StringMap<uint32_t> field_indices;
  vector<optional<const ExprNode *>> field_defaults;
  llvm::StringMap<string> methods;

  StructInfo() = default;
  StructInfo(const StructInfo &) = delete;
  StructInfo &operator=(const StructInfo &) = delete;
  StructInfo(StructInfo &&) = default;
  StructInfo &operator=(StructInfo &&) = default;
};

struct ListType {
  llvm::StructType *struct_ty;
  llvm::Type *elem_ty;
};

struct StrType {
  llvm::StructType *struct_ty;
};

struct CodeGenCtxt {
  uptr<llvm::LLVMContext> llvm_ctx;
  uptr<llvm::IRBuilder<>> builder;
  uptr<llvm::Module> module;
  llvm::StringMap<uptr<StructInfo>> struct_registry;
  llvm::StringMap<uptr<ListType>> list_types;
  uptr<StrType> str_type;
  uint32_t str_counter = 0;
  uint32_t lambda_counter = 0;

  CodeGenCtxt(const string &module_name);
};

} // namespace cat::ir
