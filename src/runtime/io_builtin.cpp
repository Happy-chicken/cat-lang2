#include "io_builtin.h"
#include "../frontend/type_checker/type.h"
#include "../midend/ir_emitter/llvm_helpers.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/Operator.h"

namespace cat::runtime {

namespace {

auto build_print_type() -> semantics::Type {
  std::vector<std::unique_ptr<semantics::Type>> params;
  params.push_back(std::make_unique<semantics::Type>(
      semantics::Type::prim(semantics::PrimType::Str)));
  return semantics::Type::func(
      std::move(params), semantics::Type::prim(semantics::PrimType::Void));
}

static std::string extract_format_string(llvm::Value *val) {
  if (auto *gv = llvm::dyn_cast<llvm::GlobalVariable>(val)) {
    if (auto *init =
            llvm::dyn_cast<llvm::ConstantDataArray>(gv->getInitializer())) {
      auto raw = init->getAsString();
      if (!raw.empty() && raw.back() == '\0')
        return raw.drop_back().str();
      return raw.str();
    }
  }

  auto *gep_op = llvm::dyn_cast<llvm::GEPOperator>(val);
  if (!gep_op)
    return {};

  if (auto *gv =
          llvm::dyn_cast<llvm::GlobalVariable>(gep_op->getPointerOperand())) {
    if (auto *init =
            llvm::dyn_cast<llvm::ConstantDataArray>(gv->getInitializer())) {
      auto raw = init->getAsString();
      if (!raw.empty() && raw.back() == '\0')
        return raw.drop_back().str();
      return raw.str();
    }
  }
  return {};
}

static std::string build_printf_format(const std::string &raw_fmt,
                                       llvm::ArrayRef<llvm::Value *> args) {
  std::string result;
  size_t arg_idx = 0;
  for (size_t i = 0; i < raw_fmt.size(); ++i) {
    if (raw_fmt[i] == '{' && i + 1 < raw_fmt.size() && raw_fmt[i + 1] == '}') {
      if (arg_idx < args.size()) {
        auto *ty = args[arg_idx]->getType();
        if (ty->isPointerTy()) {
          result += "%s";
        } else if (ty->isFloatTy() || ty->isDoubleTy()) {
          result += "%f";
        } else if (ty->isIntegerTy()) {
          result += "%d";
        } else {
          result += "%d";
        }
        ++arg_idx;
      }
      ++i;
    } else {
      result += raw_fmt[i];
    }
  }
  return result;
}

static llvm::Value *emit_formatted(const IrGenCtxtRef &ctx,
                                   llvm::ArrayRef<llvm::Value *> args,
                                   bool newline) {
  // TODO: we now dont have to generate origin fmt;
  auto raw_fmt = extract_format_string(args[0]);
  auto *fn = ctx.declare_runtime("printf", cat::ir::i32(ctx.ctx()),
                                 {cat::ir::ptr_ty(ctx.ctx())}, true);
  auto var_args = args.slice(1);

  if (raw_fmt.empty()) {
    auto *call = ctx.builder.CreateCall(fn, args);
    if (newline) {
      auto *nl = ctx.builder.CreateGlobalString("\n");
      return ctx.builder.CreateCall(fn, {nl});
    }
    return call;
  }

  auto fmt = build_printf_format(raw_fmt, var_args);
  if (newline)
    fmt += "\n";

  auto *fmt_gv = ctx.builder.CreateGlobalString(fmt);
  std::vector<llvm::Value *> call_args = {fmt_gv};
  for (auto *a : var_args)
    call_args.push_back(a);
  return ctx.builder.CreateCall(fn, call_args);
}

auto emit_print(const IrGenCtxtRef &ctx, llvm::ArrayRef<llvm::Value *> args,
                Span) -> llvm::Value * {
  if (args.empty())
    return nullptr;

  return emit_formatted(ctx, args, false);
}

auto emit_println(const IrGenCtxtRef &ctx, llvm::ArrayRef<llvm::Value *> args,
                  Span) -> llvm::Value * {
  if (args.empty())
    return nullptr;

  auto &c = ctx.ctx();
  if (args.size() == 1) {
    auto *puts_fn = ctx.declare_runtime("puts", cat::ir::i32(c),
                                        {cat::ir::ptr_ty(c)}, false);
    return ctx.builder.CreateCall(puts_fn, {args[0]});
  }
  return emit_formatted(ctx, args, true);
}

} // namespace

void register_io_builtins(BuiltinRegistry &reg) {
  reg.register_func({"print", 1, true, build_print_type, emit_print});
  reg.register_func({"println", 1, true, build_print_type, emit_println});
}

} // namespace cat::runtime
