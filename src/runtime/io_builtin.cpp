#include "io_builtin.h"
#include "../frontend/type_checker/type.h"
#include "../midend/ir_emitter/llvm_helpers.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"

namespace cat::runtime {

namespace {

auto build_print_type() -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(
      semantics::Type::prim(semantics::PrimType::Str)));
  return semantics::Type::func(
      std::move(params), semantics::Type::prim(semantics::PrimType::Void));
}

auto emit_print(const IrGenCtxtRef &ctx, llvm::ArrayRef<llvm::Value *> args,
                Span) -> llvm::Value * {
  if (args.empty())
    return nullptr;

  bool is_ptr = args[0]->getType()->isPointerTy();
  auto &c = ctx.ctx();

  if (is_ptr) {
    auto *printf_fn = ctx.declare_runtime("printf", cat::ir::i32(c),
                                          {cat::ir::ptr_ty(c)}, true);
    return ctx.builder.CreateCall(printf_fn, args);
  }

  auto *fmt = ctx.builder.CreateGlobalString("%d\n");
  auto *printf_fn = ctx.declare_runtime("printf", cat::ir::i32(c),
                                        {cat::ir::ptr_ty(c)}, true);
  return ctx.builder.CreateCall(printf_fn, {fmt, args[0]});
}

auto emit_println(const IrGenCtxtRef &ctx, llvm::ArrayRef<llvm::Value *> args,
                  Span) -> llvm::Value * {
  if (args.empty())
    return nullptr;

  bool is_ptr = args[0]->getType()->isPointerTy();
  auto &c = ctx.ctx();

  if (is_ptr && args.size() == 1) {
    auto *puts_fn = ctx.declare_runtime("puts", cat::ir::i32(c),
                                        {cat::ir::ptr_ty(c)}, false);
    return ctx.builder.CreateCall(puts_fn, {args[0]});
  }

  if (is_ptr) {
    auto *printf_fn = ctx.declare_runtime("printf", cat::ir::i32(c),
                                          {cat::ir::ptr_ty(c)}, true);
    ctx.builder.CreateCall(printf_fn, args);
    auto *nl_fmt = ctx.builder.CreateGlobalString("\n");
    return ctx.builder.CreateCall(printf_fn, {nl_fmt});
  }

  auto *fmt = ctx.builder.CreateGlobalString("%d\n");
  auto *printf_fn = ctx.declare_runtime("printf", cat::ir::i32(c),
                                        {cat::ir::ptr_ty(c)}, true);
  return ctx.builder.CreateCall(printf_fn, {fmt, args[0]});
}

} // namespace

void register_io_builtins(BuiltinRegistry &reg) {
  reg.register_func({"print", 1, build_print_type, emit_print});
  reg.register_func({"println", 1, build_print_type, emit_println});
}

} // namespace cat::runtime
