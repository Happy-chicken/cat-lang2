#include "list_builtin.h"
#include "../../frontend/type_checker/type.h"
#include <llvm-20/llvm/IR/Constants.h>
#include <llvm-20/llvm/IR/Intrinsics.h>

namespace cat::runtime {

namespace {

semantics::Type build_func_len(const semantics::Type &elem_ty) {
  auto self = semantics::Type::list(elem_ty.clone());
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(std::move(self)));
  return semantics::Type::func(std::move(params),
                               semantics::Type::prim(semantics::PrimType::Int));
}

semantics::Type build_func_push(const semantics::Type &elem_ty) {
  auto self = semantics::Type::list(elem_ty.clone());
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(std::move(self)));
  params.push_back(std::make_unique<semantics::Type>(elem_ty.clone()));
  return semantics::Type::func(
      std::move(params), semantics::Type::prim(semantics::PrimType::Void));
}

semantics::Type build_func_pop(const semantics::Type &elem_ty) {
  auto self = semantics::Type::list(elem_ty.clone());
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(std::move(self)));
  return semantics::Type::func(std::move(params), elem_ty.clone());
}

llvm::IntegerType *i64(llvm::LLVMContext &c) {
  return llvm::IntegerType::getInt64Ty(c);
}
llvm::PointerType *ptr_ty(llvm::LLVMContext &c) {
  return llvm::PointerType::get(c, 0);
}
llvm::Type *void_ty(llvm::LLVMContext &c) { return llvm::Type::getVoidTy(c); }

void grow_list(const IrGenParams &p, llvm::Value *list_ptr,
               llvm::StructType *st, llvm::Type *elem_ty) {
  auto *old_cap = p.builder.CreateLoad(
      i64(p.llvm_ctx), p.builder.CreateStructGEP(st, list_ptr, 1));
  auto *new_cap =
      p.builder.CreateMul(old_cap, llvm::ConstantInt::get(i64(p.llvm_ctx), 2));
  p.builder.CreateStore(new_cap, p.builder.CreateStructGEP(st, list_ptr, 1));

  auto *old_data = p.builder.CreateLoad(
      ptr_ty(p.llvm_ctx), p.builder.CreateStructGEP(st, list_ptr, 2));

  auto *malloc_fn =
      p.declare_runtime("malloc", ptr_ty(p.llvm_ctx), {i64(p.llvm_ctx)}, false);
  auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(elem_ty), i64(p.llvm_ctx));
  auto *new_size = p.builder.CreateMul(new_cap, elem_sz);
  auto *new_data = p.builder.CreateCall(malloc_fn, {new_size}, "listgrow");

  auto *is_null = p.builder.CreateIsNull(old_data);
  auto *fn = p.builder.GetInsertBlock()->getParent();
  auto *copy_bb = llvm::BasicBlock::Create(p.llvm_ctx, "grow.copy", fn);
  auto *done_bb = llvm::BasicBlock::Create(p.llvm_ctx, "grow.done", fn);
  p.builder.CreateCondBr(is_null, done_bb, copy_bb);

  p.builder.SetInsertPoint(copy_bb);
  auto *old_size = p.builder.CreateMul(old_cap, elem_sz);
  auto *memcpy_fn = p.declare_runtime(
      "memcpy", ptr_ty(p.llvm_ctx),
      {ptr_ty(p.llvm_ctx), ptr_ty(p.llvm_ctx), i64(p.llvm_ctx)}, false);
  p.builder.CreateCall(memcpy_fn, {new_data, old_data, old_size});

  auto *free_fn = p.declare_runtime("free", void_ty(p.llvm_ctx),
                                    {ptr_ty(p.llvm_ctx)}, false);
  p.builder.CreateCall(free_fn, {old_data});
  p.builder.CreateBr(done_bb);

  p.builder.SetInsertPoint(done_bb);
  p.builder.CreateStore(new_data, p.builder.CreateStructGEP(st, list_ptr, 2));
}

llvm::Value *ir_len(const IrGenParams &p, llvm::Value *list_ptr,
                    llvm::StructType *st, llvm::Type *,
                    const std::vector<llvm::Value *> &, Span) {
  return p.builder.CreateLoad(i64(p.llvm_ctx),
                              p.builder.CreateStructGEP(st, list_ptr, 0));
}

llvm::Value *ir_push(const IrGenParams &p, llvm::Value *list_ptr,
                     llvm::StructType *st, llvm::Type *elem_ty,
                     const std::vector<llvm::Value *> &args, Span) {
  auto *fn = p.builder.GetInsertBlock()->getParent();

  auto *len_ptr = p.builder.CreateStructGEP(st, list_ptr, 0);
  auto *cap_ptr = p.builder.CreateStructGEP(st, list_ptr, 1);
  auto *data_ptr = p.builder.CreateStructGEP(st, list_ptr, 2);
  auto *len_val = p.builder.CreateLoad(i64(p.llvm_ctx), len_ptr);
  auto *cap_val = p.builder.CreateLoad(i64(p.llvm_ctx), cap_ptr);
  auto *data = p.builder.CreateLoad(ptr_ty(p.llvm_ctx), data_ptr);

  auto *cap_exhausted = p.builder.CreateICmpUGE(len_val, cap_val);
  auto *is_null = p.builder.CreateIsNull(data);
  auto *need_grow = p.builder.CreateOr(cap_exhausted, is_null);

  auto *grow_bb = llvm::BasicBlock::Create(p.llvm_ctx, "push.grow", fn);
  auto *store_bb = llvm::BasicBlock::Create(p.llvm_ctx, "push.store", fn);

  p.builder.CreateCondBr(need_grow, grow_bb, store_bb);

  p.builder.SetInsertPoint(grow_bb);
  grow_list(p, list_ptr, st, elem_ty);
  p.builder.CreateBr(store_bb);

  p.builder.SetInsertPoint(store_bb);
  data = p.builder.CreateLoad(ptr_ty(p.llvm_ctx), data_ptr);
  len_val = p.builder.CreateLoad(i64(p.llvm_ctx), len_ptr);

  p.builder.CreateStore(args[0], p.builder.CreateGEP(elem_ty, data, len_val));
  auto *new_len =
      p.builder.CreateAdd(len_val, llvm::ConstantInt::get(i64(p.llvm_ctx), 1));
  p.builder.CreateStore(new_len, len_ptr);

  return nullptr;
}

llvm::Value *ir_pop(const IrGenParams &p, llvm::Value *list_ptr,
                    llvm::StructType *st, llvm::Type *elem_ty,
                    const std::vector<llvm::Value *> &, Span) {
  auto *len_ptr = p.builder.CreateStructGEP(st, list_ptr, 0);
  auto *len_val = p.builder.CreateLoad(i64(p.llvm_ctx), len_ptr);

  auto *zero = llvm::ConstantInt::get(i64(p.llvm_ctx), 0);
  auto *is_empty = p.builder.CreateICmpEQ(len_val, zero);

  auto *fn = p.builder.GetInsertBlock()->getParent();
  auto *ok_bb = llvm::BasicBlock::Create(p.llvm_ctx, "pop.ok", fn);
  auto *fail_bb = llvm::BasicBlock::Create(p.llvm_ctx, "pop.fail", fn);
  p.builder.CreateCondBr(is_empty, fail_bb, ok_bb);

  p.builder.SetInsertPoint(fail_bb);
  auto *trap =
      llvm::Intrinsic::getOrInsertDeclaration(&p.module, llvm::Intrinsic::trap);
  p.builder.CreateCall(trap);
  p.builder.CreateUnreachable();

  p.builder.SetInsertPoint(ok_bb);
  auto *new_len =
      p.builder.CreateSub(len_val, llvm::ConstantInt::get(i64(p.llvm_ctx), 1));
  p.builder.CreateStore(new_len, len_ptr);

  auto *data = p.builder.CreateLoad(ptr_ty(p.llvm_ctx),
                                    p.builder.CreateStructGEP(st, list_ptr, 2));
  auto *elem_ptr = p.builder.CreateGEP(elem_ty, data, new_len);
  return p.builder.CreateLoad(elem_ty, elem_ptr);
}

} // namespace

void register_list_builtins(BuiltinRegistry &reg) {
  reg.register_type("list", {
                                {"len", 0, build_func_len, ir_len},
                                {"push", 1, build_func_push, ir_push},
                                {"pop", 0, build_func_pop, ir_pop},
                            });
}

} // namespace cat::runtime
