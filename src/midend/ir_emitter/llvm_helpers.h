#pragma once
#include <llvm-20/llvm/IR/Constants.h>
#include <llvm-20/llvm/IR/DerivedTypes.h>
#include <llvm-20/llvm/IR/LLVMContext.h>

namespace cat::ir {

inline llvm::IntegerType *i32(llvm::LLVMContext &c) {
  return llvm::IntegerType::getInt32Ty(c);
}
inline llvm::IntegerType *i64(llvm::LLVMContext &c) {
  return llvm::IntegerType::getInt64Ty(c);
}
inline llvm::PointerType *ptr_ty(llvm::LLVMContext &c) {
  return llvm::PointerType::get(c, 0);
}
inline llvm::Type *void_ty(llvm::LLVMContext &c) {
  return llvm::Type::getVoidTy(c);
}

inline llvm::Constant *zero_const(llvm::Type *ty) {
  if (ty->isIntegerTy())
    return llvm::ConstantInt::get(ty, 0);
  if (ty->isFloatTy())
    return llvm::ConstantFP::get(ty, 0.0);
  if (ty->isPointerTy())
    return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ty));
  return llvm::Constant::getNullValue(ty);
}

} // namespace cat::ir
