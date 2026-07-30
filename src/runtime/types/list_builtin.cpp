#include "list_builtin.h"
#include "../../frontend/type_checker/type.h"
#include "../../midend/ir_emitter/llvm_helpers.h"
#include <llvm-20/llvm/IR/Constants.h>
#include <llvm-20/llvm/IR/Intrinsics.h>

namespace cat::runtime {

namespace {

auto build_func_len(const semantics::Type &elem_ty) -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(
      std::make_unique<semantics::Type>(semantics::Type::list(elem_ty.clone())));
  return semantics::Type::func(
      std::move(params), semantics::Type::prim(semantics::PrimType::Int));
}

auto build_func_push(const semantics::Type &elem_ty) -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(
      std::make_unique<semantics::Type>(semantics::Type::list(elem_ty.clone())));
  params.push_back(std::make_unique<semantics::Type>(elem_ty.clone()));
  return semantics::Type::func(
      std::move(params), semantics::Type::prim(semantics::PrimType::Void));
}

auto build_func_pop(const semantics::Type &elem_ty) -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(
      std::make_unique<semantics::Type>(semantics::Type::list(elem_ty.clone())));
  return semantics::Type::func(std::move(params), elem_ty.clone());
}

void emit_list_grow(const IrGenCtxtRef &ctx, llvm::Value *list_ptr,
                    llvm::StructType *st, llvm::Type *elem_ty) {
  auto &c = ctx.ctx();
  auto *old_cap =
      ctx.builder.CreateLoad(cat::ir::i64(c),
                             ctx.builder.CreateStructGEP(st, list_ptr, 1));
  auto *two = llvm::ConstantInt::get(cat::ir::i64(c), 2);
  auto *min_cap = llvm::ConstantInt::get(cat::ir::i64(c), 8);
  auto *doubled = ctx.builder.CreateMul(old_cap, two);
  auto *new_cap = ctx.builder.CreateSelect(
      ctx.builder.CreateICmpUGT(doubled, old_cap), doubled, min_cap);
  ctx.builder.CreateStore(new_cap,
                           ctx.builder.CreateStructGEP(st, list_ptr, 1));

  auto *old_data =
      ctx.builder.CreateLoad(cat::ir::ptr_ty(c),
                             ctx.builder.CreateStructGEP(st, list_ptr, 2));

  auto *malloc_fn = ctx.declare_runtime("malloc", cat::ir::ptr_ty(c),
                                        {cat::ir::i64(c)}, false);
  auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(elem_ty), cat::ir::i64(c));
  auto *new_size = ctx.builder.CreateMul(new_cap, elem_sz);
  auto *new_data = ctx.builder.CreateCall(malloc_fn, {new_size}, "listgrow");

  auto *is_null = ctx.builder.CreateIsNull(old_data);
  auto *fn = ctx.builder.GetInsertBlock()->getParent();
  auto *copy_bb = llvm::BasicBlock::Create(c, "grow.copy", fn);
  auto *done_bb = llvm::BasicBlock::Create(c, "grow.done", fn);
  ctx.builder.CreateCondBr(is_null, done_bb, copy_bb);

  ctx.builder.SetInsertPoint(copy_bb);
  auto *old_size = ctx.builder.CreateMul(old_cap, elem_sz);
  auto *memcpy_fn = ctx.declare_runtime(
      "memcpy", cat::ir::ptr_ty(c),
      {cat::ir::ptr_ty(c), cat::ir::ptr_ty(c), cat::ir::i64(c)}, false);
  ctx.builder.CreateCall(memcpy_fn, {new_data, old_data, old_size});

  auto *free_fn = ctx.declare_runtime("free", cat::ir::void_ty(c),
                                      {cat::ir::ptr_ty(c)}, false);
  ctx.builder.CreateCall(free_fn, {old_data});
  ctx.builder.CreateBr(done_bb);

  ctx.builder.SetInsertPoint(done_bb);
  ctx.builder.CreateStore(new_data,
                           ctx.builder.CreateStructGEP(st, list_ptr, 2));
}

auto emit_list_len(const IrGenCtxtRef &ctx, llvm::Value *list_ptr,
                   llvm::StructType *st, llvm::Type *,
                   llvm::ArrayRef<llvm::Value *>, Span) -> llvm::Value * {
  auto *len = ctx.builder.CreateLoad(
      cat::ir::i64(ctx.ctx()),
      ctx.builder.CreateStructGEP(st, list_ptr, 0));
  return ctx.builder.CreateTrunc(len, cat::ir::i32(ctx.ctx()));
}

auto emit_list_push(const IrGenCtxtRef &ctx, llvm::Value *list_ptr,
                    llvm::StructType *st, llvm::Type *elem_ty,
                    llvm::ArrayRef<llvm::Value *> args, Span) -> llvm::Value * {
  auto &c = ctx.ctx();
  auto *fn = ctx.builder.GetInsertBlock()->getParent();

  auto *len_ptr = ctx.builder.CreateStructGEP(st, list_ptr, 0);
  auto *cap_ptr = ctx.builder.CreateStructGEP(st, list_ptr, 1);
  auto *data_ptr = ctx.builder.CreateStructGEP(st, list_ptr, 2);
  auto *len_val = ctx.builder.CreateLoad(cat::ir::i64(c), len_ptr);
  auto *cap_val = ctx.builder.CreateLoad(cat::ir::i64(c), cap_ptr);
  auto *data = ctx.builder.CreateLoad(cat::ir::ptr_ty(c), data_ptr);

  auto *cap_exhausted = ctx.builder.CreateICmpUGE(len_val, cap_val);
  auto *is_null = ctx.builder.CreateIsNull(data);
  auto *need_grow = ctx.builder.CreateOr(cap_exhausted, is_null);

  auto *grow_bb = llvm::BasicBlock::Create(c, "push.grow", fn);
  auto *store_bb = llvm::BasicBlock::Create(c, "push.store", fn);
  ctx.builder.CreateCondBr(need_grow, grow_bb, store_bb);

  ctx.builder.SetInsertPoint(grow_bb);
  emit_list_grow(ctx, list_ptr, st, elem_ty);
  ctx.builder.CreateBr(store_bb);

  ctx.builder.SetInsertPoint(store_bb);
  data = ctx.builder.CreateLoad(cat::ir::ptr_ty(c), data_ptr);
  len_val = ctx.builder.CreateLoad(cat::ir::i64(c), len_ptr);

  ctx.builder.CreateStore(args[0],
                          ctx.builder.CreateGEP(elem_ty, data, len_val));
  auto *new_len = ctx.builder.CreateAdd(
      len_val, llvm::ConstantInt::get(cat::ir::i64(c), 1));
  ctx.builder.CreateStore(new_len, len_ptr);

  return nullptr;
}

auto emit_list_pop(const IrGenCtxtRef &ctx, llvm::Value *list_ptr,
                   llvm::StructType *st, llvm::Type *elem_ty,
                   llvm::ArrayRef<llvm::Value *>, Span) -> llvm::Value * {
  auto &c = ctx.ctx();
  auto *len_ptr = ctx.builder.CreateStructGEP(st, list_ptr, 0);
  auto *len_val = ctx.builder.CreateLoad(cat::ir::i64(c), len_ptr);

  auto *zero = llvm::ConstantInt::get(cat::ir::i64(c), 0);
  auto *is_empty = ctx.builder.CreateICmpEQ(len_val, zero);

  auto *fn = ctx.builder.GetInsertBlock()->getParent();
  auto *ok_bb = llvm::BasicBlock::Create(c, "pop.ok", fn);
  auto *fail_bb = llvm::BasicBlock::Create(c, "pop.fail", fn);
  ctx.builder.CreateCondBr(is_empty, fail_bb, ok_bb);

  ctx.builder.SetInsertPoint(fail_bb);
  auto *trap = llvm::Intrinsic::getOrInsertDeclaration(&ctx.module,
                                                       llvm::Intrinsic::trap);
  ctx.builder.CreateCall(trap);
  ctx.builder.CreateUnreachable();

  ctx.builder.SetInsertPoint(ok_bb);
  auto *new_len = ctx.builder.CreateSub(
      len_val, llvm::ConstantInt::get(cat::ir::i64(c), 1));
  ctx.builder.CreateStore(new_len, len_ptr);

  auto *data =
      ctx.builder.CreateLoad(cat::ir::ptr_ty(c),
                             ctx.builder.CreateStructGEP(st, list_ptr, 2));
  auto *elem_ptr = ctx.builder.CreateGEP(elem_ty, data, new_len);
  return ctx.builder.CreateLoad(elem_ty, elem_ptr);
}

auto build_func_clone(const semantics::Type &elem_ty) -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(
      std::make_unique<semantics::Type>(semantics::Type::list(elem_ty.clone())));
  return semantics::Type::func(
      std::move(params), semantics::Type::list(elem_ty.clone()));
}

auto emit_list_clone(const IrGenCtxtRef &ctx, llvm::Value *list_ptr,
                     llvm::StructType *st, llvm::Type *elem_ty,
                     llvm::ArrayRef<llvm::Value *>, Span) -> llvm::Value * {
  auto &c = ctx.ctx();
  auto *len_val = ctx.builder.CreateLoad(cat::ir::i64(c),
                                         ctx.builder.CreateStructGEP(st, list_ptr, 0));
  auto *cap_val = ctx.builder.CreateLoad(cat::ir::i64(c),
                                         ctx.builder.CreateStructGEP(st, list_ptr, 1));
  auto *old_data = ctx.builder.CreateLoad(cat::ir::ptr_ty(c),
                                          ctx.builder.CreateStructGEP(st, list_ptr, 2));

  auto *malloc_fn = ctx.declare_runtime("malloc", cat::ir::ptr_ty(c),
                                        {cat::ir::i64(c)}, false);
  auto *memcpy_fn = ctx.declare_runtime("memcpy", cat::ir::ptr_ty(c),
                                        {cat::ir::ptr_ty(c), cat::ir::ptr_ty(c), cat::ir::i64(c)},
                                        false);
  auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(elem_ty), cat::ir::i64(c));
  auto *total = ctx.builder.CreateMul(len_val, elem_sz);
  auto *new_data = ctx.builder.CreateCall(malloc_fn, {total}, "clone.list.data");
  ctx.builder.CreateCall(memcpy_fn, {new_data, old_data, total});

  llvm::Value *result = llvm::UndefValue::get(st);
  result = ctx.builder.CreateInsertValue(result, len_val, {0u});
  result = ctx.builder.CreateInsertValue(result, len_val, {1u});
  result = ctx.builder.CreateInsertValue(result, new_data, {2u});
  return result;
}

} // namespace

void register_list_builtins(BuiltinRegistry &reg) {
  reg.register_type(LIST_TAG,
                    {
                        {{"len", 0, MethodEffect::PureRead}, build_func_len,
                         emit_list_len},
                        {{"push", 1, MethodEffect::Mutating},
                         build_func_push, emit_list_push},
                        {{"pop", 0, MethodEffect::Mutating}, build_func_pop,
                         emit_list_pop},
                        {{"clone", 0, MethodEffect::PureRead},
                         build_func_clone, emit_list_clone,
                         [](const IrGenCtxtRef &, llvm::Value *self,
                            llvm::ArrayRef<llvm::Value *>,
                            Span) -> llvm::Value * { return self; }},
                    });
}

} // namespace cat::runtime
