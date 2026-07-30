#include "str_builtin.h"
#include "../../frontend/type_checker/type.h"
#include "../../midend/ir_emitter/llvm_helpers.h"
#include <llvm-20/llvm/IR/Constants.h>

namespace cat::runtime {

namespace {

auto build_func_len(const semantics::Type &) -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(semantics::Type::str()));
  return semantics::Type::func(
      std::move(params), semantics::Type::prim(semantics::PrimType::Int));
}

auto emit_str_len(const IrGenCtxtRef &ctx, llvm::Value *str_ptr,
                  llvm::StructType *st, llvm::Type *,
                  llvm::ArrayRef<llvm::Value *>, Span) -> llvm::Value * {
  auto *len = ctx.builder.CreateLoad(
      cat::ir::i64(ctx.ctx()),
      ctx.builder.CreateStructGEP(st, str_ptr, 0));
  return ctx.builder.CreateTrunc(len, cat::ir::i32(ctx.ctx()));
}

auto build_func_clone(const semantics::Type &) -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(semantics::Type::str()));
  return semantics::Type::func(
      std::move(params), semantics::Type::str());
}

auto emit_str_clone(const IrGenCtxtRef &ctx, llvm::Value *str_ptr,
                    llvm::StructType *st, llvm::Type *,
                    llvm::ArrayRef<llvm::Value *>, Span) -> llvm::Value * {
  auto &c = ctx.ctx();
  auto *len_val = ctx.builder.CreateLoad(
      cat::ir::i64(c), ctx.builder.CreateStructGEP(st, str_ptr, 0));
  auto *old_data = ctx.builder.CreateLoad(
      cat::ir::ptr_ty(c), ctx.builder.CreateStructGEP(st, str_ptr, 1));

  auto *malloc_fn =
      ctx.declare_runtime("malloc", cat::ir::ptr_ty(c), {cat::ir::i64(c)}, false);
  auto *memcpy_fn = ctx.declare_runtime(
      "memcpy", cat::ir::ptr_ty(c),
      {cat::ir::ptr_ty(c), cat::ir::ptr_ty(c), cat::ir::i64(c)}, false);
  auto *new_data = ctx.builder.CreateCall(malloc_fn, {len_val}, "clone.str.data");

  auto *zero = llvm::ConstantInt::get(cat::ir::i64(c), 0);
  auto *is_empty = ctx.builder.CreateICmpEQ(len_val, zero);
  auto *fn = ctx.builder.GetInsertBlock()->getParent();
  auto *copy_bb = llvm::BasicBlock::Create(c, "clone.str.copy", fn);
  auto *done_bb = llvm::BasicBlock::Create(c, "clone.str.done", fn);
  ctx.builder.CreateCondBr(is_empty, done_bb, copy_bb);

  ctx.builder.SetInsertPoint(copy_bb);
  ctx.builder.CreateCall(memcpy_fn, {new_data, old_data, len_val});
  ctx.builder.CreateBr(done_bb);

  ctx.builder.SetInsertPoint(done_bb);
  llvm::Value *result = llvm::UndefValue::get(st);
  result = ctx.builder.CreateInsertValue(result, len_val, {0u});
  result = ctx.builder.CreateInsertValue(result, new_data, {1u});
  return result;
}

} // namespace

void register_str_builtins(BuiltinRegistry &reg) {
  reg.register_type(STR_TAG,
                    {
                        {{"len", 0, MethodEffect::PureRead}, build_func_len,
                         emit_str_len},
                        {{"clone", 0, MethodEffect::PureRead},
                         build_func_clone, emit_str_clone,
                         [](const IrGenCtxtRef &, llvm::Value *self,
                            llvm::ArrayRef<llvm::Value *>,
                            Span) -> llvm::Value * { return self; }},
                    });
}

} // namespace cat::runtime
