#include "cleanup.h"
#include "../../frontend/ast/type.h"
#include "codegen_ctx.h"
#include "env.h"
#include "llvm_helpers.h"
#include <llvm-20/llvm/IR/Constants.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/Instructions.h>
#include <llvm-20/llvm/IR/Intrinsics.h>

namespace cat::ir {

CleanupManager::CleanupManager(CodeGenCtxt &ctx) : ctx(ctx) {}

CleanupKind CleanupManager::classify_type(const ast::Type &ty) {
  return std::visit(
      overloaded{
          [](const ast::Type::Ref &) { return CleanupKind::None; },
          [](const ast::Type::CRef &) { return CleanupKind::None; },
          [](const ast::Type::Own &o) {
            if (!o.inner)
              return CleanupKind::None;
            return std::visit(
                overloaded{
                    [](const ast::Type::Class &) {
                      return CleanupKind::ClassFree;
                    },
                    [](const ast::Type::List &) {
                      return CleanupKind::OwnListFree;
                    },
                    [](const auto &) { return CleanupKind::None; },
                },
                o.inner->data);
          },
          [](const ast::Type::Class &) { return CleanupKind::ClassFree; },
          [](const ast::Type::List &) { return CleanupKind::ListDataFree; },
          // Str cleanup requires runtime-owned tracking; defer.
          [](const ast::Type::Str &) { return CleanupKind::None; },
          [](const auto &) { return CleanupKind::None; },
      },
      ty.data);
}

void CleanupManager::register_class_cleanup(Env &env, llvm::Value *alloca,
                                            llvm::Type *alloca_ty) {
  env.add_cleanup(alloca, alloca_ty, true);
}

void CleanupManager::register_list_cleanup(Env &env, llvm::Value *alloca,
                                           llvm::Type *alloca_ty,
                                           llvm::StructType *list_st) {
  env.add_cleanup(alloca, alloca_ty, false, list_st);
}

void CleanupManager::register_own_list_cleanup(Env &env, llvm::Value *alloca,
                                               llvm::Type *alloca_ty,
                                               llvm::StructType *list_st) {
  env.add_cleanup(alloca, alloca_ty, false, list_st, true);
}

void CleanupManager::register_str_cleanup(Env &env, llvm::Value *alloca,
                                          llvm::Type *alloca_ty,
                                          llvm::StructType *str_st) {
  env.add_cleanup(alloca, alloca_ty, false, str_st);
  env.cleanups.back().is_str = true;
}

bool CleanupManager::cancel_cleanup(Env &env, llvm::Value *alloca) {
  return env.cancel_cleanup(alloca);
}

llvm::Function *CleanupManager::declare_runtime_func(
    const string &fn_name, llvm::Type *ret, vector<llvm::Type *> params,
    bool var_arg) {
  if (auto *f = ctx.module->getFunction(fn_name))
    return f;
  auto *ft = llvm::FunctionType::get(ret, params, var_arg);
  return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, fn_name,
                                ctx.module.get());
}

void CleanupManager::emit_scope_cleanup(Env &env_scope) {
  auto *bb = ctx.builder->GetInsertBlock();
  if (!bb || bb->getTerminator())
    return;
  auto &cc = *ctx.llvm_ctx;
  auto *free_fn = declare_runtime_func("free", void_ty(cc), {ptr_ty(cc)});
  for (auto it = env_scope.cleanups.rbegin(); it != env_scope.cleanups.rend();
       ++it) {
    if (it->cancelled)
      continue;
    if (it->is_class) {
      auto *ptr = ctx.builder->CreateLoad(it->alloca_ty, it->alloca);
      ctx.builder->CreateCall(free_fn, {ptr});
    } else if (it->is_str && it->list_st) {
      auto *gp = ctx.builder->CreateStructGEP(it->list_st, it->alloca, 1u);
      auto *data = ctx.builder->CreateLoad(ptr_ty(cc), gp);
      if (!llvm::isa<llvm::Constant>(data))
        ctx.builder->CreateCall(free_fn, {data});
    } else if (it->list_st) {
      auto *base = it->alloca;
      if (it->free_base_ptr)
        base = ctx.builder->CreateLoad(it->alloca_ty, it->alloca);
      auto *gp = ctx.builder->CreateStructGEP(it->list_st, base, 2u);
      auto *data = ctx.builder->CreateLoad(ptr_ty(cc), gp);
      ctx.builder->CreateCall(free_fn, {data});
      if (it->free_base_ptr) {
        auto *ptr = ctx.builder->CreateLoad(it->alloca_ty, it->alloca);
        ctx.builder->CreateCall(free_fn, {ptr});
      }
    }
    it->cancelled = true;
  }
}

void CleanupManager::emit_all_cleanups(Env &env) {
  auto *e = &env;
  while (e && e->parent) {
    emit_scope_cleanup(*e);
    e = e->parent.get();
  }
}

void CleanupManager::emit_until_loop(Env &env) {
  auto *e = &env;
  while (e) {
    emit_scope_cleanup(*e);
    if (e->loop_info.has_value())
      break;
    e = e->parent.get();
  }
}

void CleanupManager::emit_var_free(Env &env, llvm::Value *alloca) {
  auto *env_ptr = &env;
  while (env_ptr) {
    for (auto &c : env_ptr->cleanups) {
      if (c.alloca == alloca && !c.cancelled) {
        auto &cc = *ctx.llvm_ctx;
        auto *free_fn =
            declare_runtime_func("free", void_ty(cc), {ptr_ty(cc)});
            if (c.is_class) {
              auto *ptr = ctx.builder->CreateLoad(c.alloca_ty, c.alloca);
              ctx.builder->CreateCall(free_fn, {ptr});
            } else if (c.is_str && c.list_st) {
              auto *gp =
                  ctx.builder->CreateStructGEP(c.list_st, c.alloca, 1u);
              auto *data = ctx.builder->CreateLoad(ptr_ty(cc), gp);
              if (!llvm::isa<llvm::Constant>(data))
                ctx.builder->CreateCall(free_fn, {data});
            } else if (c.list_st) {
          auto *base = c.alloca;
          if (c.free_base_ptr)
            base = ctx.builder->CreateLoad(c.alloca_ty, c.alloca);
          auto *gp = ctx.builder->CreateStructGEP(c.list_st, base, 2u);
          auto *data = ctx.builder->CreateLoad(ptr_ty(cc), gp);
          ctx.builder->CreateCall(free_fn, {data});
          if (c.free_base_ptr) {
            auto *ptr = ctx.builder->CreateLoad(c.alloca_ty, c.alloca);
            ctx.builder->CreateCall(free_fn, {ptr});
          }
        }
        c.cancelled = true;
        return;
      }
    }
    env_ptr = env_ptr->parent.get();
  }
}

} // namespace cat::ir
