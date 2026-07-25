#include "io_builtin.h"
#include "../frontend/type_checker/type.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"

namespace cat::runtime {

namespace {

llvm::IntegerType *i32(llvm::LLVMContext &c) {
  return llvm::IntegerType::getInt32Ty(c);
}
llvm::PointerType *ptr_ty(llvm::LLVMContext &c) {
  return llvm::PointerType::get(c, 0);
}
llvm::Type *void_ty(llvm::LLVMContext &c) { return llvm::Type::getVoidTy(c); }

semantics::Type build_print_type() {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(
      semantics::Type::prim(semantics::PrimType::Str)));
  return semantics::Type::func(
      std::move(params),
      semantics::Type::prim(semantics::PrimType::Void));
}

llvm::Value *ir_print(const IrGenParams &p,
                      const std::vector<llvm::Value *> &args, Span) {
  if (args.empty())
    return nullptr;

  bool is_ptr = args[0]->getType()->isPointerTy();

  if (is_ptr) {
    auto *printf_fn = p.declare_runtime(
        "printf", i32(p.llvm_ctx), {ptr_ty(p.llvm_ctx)}, true);
    return p.builder.CreateCall(printf_fn, args);
  }

  auto *fmt = p.builder.CreateGlobalString("%d\n");
  std::vector<llvm::Value *> printf_args = {fmt, args[0]};
  auto *printf_fn = p.declare_runtime(
      "printf", i32(p.llvm_ctx), {ptr_ty(p.llvm_ctx)}, true);
  return p.builder.CreateCall(printf_fn, printf_args);
}

llvm::Value *ir_println(const IrGenParams &p,
                        const std::vector<llvm::Value *> &args, Span) {
  if (args.empty())
    return nullptr;

  bool is_ptr = args[0]->getType()->isPointerTy();

  if (is_ptr && args.size() == 1) {
    auto *puts_fn = p.declare_runtime("puts", i32(p.llvm_ctx),
                                      {ptr_ty(p.llvm_ctx)}, false);
    return p.builder.CreateCall(puts_fn, {args[0]});
  }

  if (is_ptr) {
    auto *printf_fn = p.declare_runtime(
        "printf", i32(p.llvm_ctx), {ptr_ty(p.llvm_ctx)}, true);
    p.builder.CreateCall(printf_fn, args);
    auto *nl_fmt = p.builder.CreateGlobalString("\n");
    return p.builder.CreateCall(printf_fn, {nl_fmt});
  }

  auto *fmt = p.builder.CreateGlobalString("%d\n");
  std::vector<llvm::Value *> printf_args = {fmt, args[0]};
  auto *printf_fn = p.declare_runtime(
      "printf", i32(p.llvm_ctx), {ptr_ty(p.llvm_ctx)}, true);
  return p.builder.CreateCall(printf_fn, printf_args);
}

} // namespace

void register_io_builtins(BuiltinRegistry &reg) {
  reg.register_func({"print", 0, build_print_type, ir_print});
  reg.register_func({"println", 0, build_print_type, ir_println});
}

} // namespace cat::runtime
