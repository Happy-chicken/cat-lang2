#include "ir_emitter.h"
#include "expr.h"
#include "llvm_helpers.h"
#include <llvm-20/llvm/IR/Constants.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/GlobalVariable.h>
#include <llvm-20/llvm/IR/Instructions.h>
#include <llvm-20/llvm/IR/Intrinsics.h>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/Support/raw_ostream.h>
#include <ranges>

namespace cat::ir {

namespace {

string list_type_key(llvm::Type *et) {
  if (et->isIntegerTy(32))
    return "list.i32";
  if (et->isIntegerTy(64))
    return "list.i64";
  if (et->isIntegerTy(8))
    return "list.i8";
  if (et->isIntegerTy(1))
    return "list.bool";
  if (et->isFloatTy())
    return "list.float";
  if (et->isPointerTy())
    return "list.ptr";
  if (auto *st = llvm::dyn_cast<llvm::StructType>(et))
    return "list." + st->getName().str();
  return "list.unknown";
}
} // namespace

// ── list / str type helpers ──

ListType *IrEmitter::lookup_or_create_list_type(llvm::Type *elem_ty) {
  auto key = list_type_key(elem_ty);
  auto &e = ctx->list_types[key];
  if (!e) {
    e = std::make_unique<ListType>();
    auto &c = *ctx->llvm_ctx;
    e->struct_ty = llvm::StructType::create(c, key);
    e->elem_ty = elem_ty;
    e->struct_ty->setBody({i64(c), i64(c), ptr_ty(c)});
  }
  return e.get();
}

ListType *IrEmitter::lookup_list_type_by_struct(llvm::StructType *st) {
  for (auto &kv : ctx->list_types)
    if (kv.second->struct_ty == st)
      return kv.second.get();
  return nullptr;
}

StrType *IrEmitter::get_str_type() {
  if (!ctx->str_type) {
    auto &c = *ctx->llvm_ctx;
    ctx->str_type = std::make_unique<StrType>();
    ctx->str_type->struct_ty =
        llvm::StructType::create(c, {i64(c), ptr_ty(c)}, "str");
  }
  return ctx->str_type.get();
}

IrEmitter::IrEmitter(const string &name, error::DiagCtxt &diag,
                     semantics::SemaCtxt &sema_ctx)
    : ctx(std::make_unique<CodeGenCtxt>(name)), env(std::make_shared<Env>()),
      cleanup_mgr(*ctx), diag(diag), current_function(nullptr),
      sema(sema_ctx) {}

// ── type helpers ──

llvm::Type *IrEmitter::llvm_type(const ast::Type &ast_type) {
  auto &c = *ctx->llvm_ctx;
  return std::visit(
      [&](auto &v) -> llvm::Type * {
        using T = std::decay_t<decltype(v)>;
        if constexpr (std::is_same_v<T, ast::Type::Int>)
          return i32(c);
        if constexpr (std::is_same_v<T, ast::Type::Float>)
          return llvm::Type::getFloatTy(c);
        if constexpr (std::is_same_v<T, ast::Type::Bool>)
          return llvm::IntegerType::getInt1Ty(c);
        if constexpr (std::is_same_v<T, ast::Type::Char>)
          return llvm::IntegerType::getInt8Ty(c);
        if constexpr (std::is_same_v<T, ast::Type::Void>)
          return void_ty(c);
        if constexpr (std::is_same_v<T, ast::Type::Str>)
          return get_str_type()->struct_ty;
        if constexpr (std::is_same_v<T, ast::Type::Ptr>)
          return ptr_ty(c);
        if constexpr (std::is_same_v<T, ast::Type::Own>)
          return ptr_ty(c);
        if constexpr (std::is_same_v<T, ast::Type::Ref>)
          return ptr_ty(c);
        if constexpr (std::is_same_v<T, ast::Type::CRef>)
          return ptr_ty(c);
        if constexpr (std::is_same_v<T, ast::Type::List>) {
          if (!v.inner)
            return ptr_ty(c);
          auto et = llvm_type(*v.inner);
          return lookup_or_create_list_type(et)->struct_ty;
        }
        if constexpr (std::is_same_v<T, ast::Type::Class>) {
          return ptr_ty(c);
        }
        if constexpr (std::is_same_v<T, ast::Type::Func>) {
          vector<llvm::Type *> ptypes;
          for (auto &p : v.params)
            ptypes.push_back(p ? llvm_type(*p) : i32(c));
          auto *ret = v.ret ? llvm_type(*v.ret) : void_ty(c);
          return llvm::PointerType::get(
              llvm::FunctionType::get(ret, ptypes, false), 0);
        }
        return i32(c);
      },
      ast_type.data);
}

llvm::FunctionType *IrEmitter::llvm_func_type(const ast::Type &ast_type) {
  if (auto *func = std::get_if<ast::Type::Func>(&ast_type.data)) {
    auto &c = *ctx->llvm_ctx;
    vector<llvm::Type *> ptypes;
    for (auto &p : func->params)
      ptypes.push_back(p ? llvm_type(*p) : i32(c));
    auto *ret = func->ret ? llvm_type(*func->ret) : void_ty(c);
    return llvm::FunctionType::get(ret, ptypes, false);
  }
  return nullptr;
}

llvm::Type *IrEmitter::ptr_pointee_llvm_type(const ast::Type &ast_type) {
  if (auto *ptr = std::get_if<ast::Type::Ptr>(&ast_type.data))
    return ptr->inner ? llvm_type(*ptr->inner) : nullptr;
  return nullptr;
}

vector<llvm::Type *> IrEmitter::ptr_deref_chain(const ast::Type &ast_type) {
  vector<llvm::Type *> chain;
  const ast::Type *cur = &ast_type;
  while (cur) {
    if (auto *ptr = std::get_if<ast::Type::Ptr>(&cur->data)) {
      cur = ptr->inner.get();
      if (!cur)
        break;
      chain.push_back(llvm_type(*cur));
    } else if (auto *ref = std::get_if<ast::Type::Ref>(&cur->data)) {
      cur = ref->inner.get();
      if (!cur)
        break;
      chain.push_back(llvm_type(*cur));
    } else if (auto *cref = std::get_if<ast::Type::CRef>(&cur->data)) {
      cur = cref->inner.get();
      if (!cur)
        break;
      chain.push_back(llvm_type(*cur));
    } else if (auto *own = std::get_if<ast::Type::Own>(&cur->data)) {
      cur = own->inner.get();
      if (!cur)
        break;
      chain.push_back(llvm_type(*cur));
    } else {
      break;
    }
  }
  return chain;
}

IrEmitter::ParamTypeInfo
IrEmitter::resolve_param_type(const ast::Type &ty) {
  auto &c = *ctx->llvm_ctx;
  ParamTypeInfo info{};

  if (auto *ref = std::get_if<ast::Type::Ref>(&ty.data)) {
    info.borrow_kind = BorrowKind::Ref;
    info.param_ty = ptr_ty(c);
    info.value_ty = ref->inner ? llvm_type(*ref->inner) : ptr_ty(c);
  } else if (auto *cref = std::get_if<ast::Type::CRef>(&ty.data)) {
    info.borrow_kind = BorrowKind::CRef;
    info.param_ty = ptr_ty(c);
    info.value_ty = cref->inner ? llvm_type(*cref->inner) : ptr_ty(c);
  } else if (auto *own = std::get_if<ast::Type::Own>(&ty.data)) {
    info.borrow_kind = BorrowKind::Own;
    info.param_ty = own->inner ? llvm_type(*own->inner) : llvm_type(ty);
    info.value_ty = own->inner ? llvm_type(*own->inner) : llvm_type(ty);
  } else {
    info.param_ty = llvm_type(ty);
    info.value_ty = info.param_ty;
    if (ty.is_struct_type())
      info.borrow_kind = BorrowKind::Own;
  }

  return info;
}

// ── variable access helpers ──

llvm::Value *IrEmitter::load_variable(const string &name, Span) {
  auto v = env->lookup_var(name);
  if (!v.ptr || !v.alloca_ty) {
    if (auto *fn = ctx->module->getFunction(name))
      return fn;
    return nullptr;
  }
  auto *val = ctx->builder->CreateLoad(v.alloca_ty, v.ptr, name);
  if (v.borrow_kind == BorrowKind::Ref || v.borrow_kind == BorrowKind::CRef)
    val = ctx->builder->CreateLoad(v.value_ty, val);
  return val;
}

llvm::Value *IrEmitter::variable_ptr(const string &name, Span) {
  auto vi = env->lookup_var(name);
  if (!vi.ptr)
    return nullptr;
  if (vi.borrow_kind == BorrowKind::Ref || vi.borrow_kind == BorrowKind::CRef)
    return ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
  return vi.ptr;
}

void IrEmitter::invalidate_source(const ExprNode &src_expr) {
  if (auto *var = std::get_if<Variable>(&src_expr.expr)) {
    auto vi = env->lookup_var(var->name);
    if (vi.ptr) {
      ctx->builder->CreateStore(llvm::UndefValue::get(vi.alloca_ty), vi.ptr);
      cleanup_mgr.cancel_cleanup(*env, vi.ptr);
    }
  }
}

llvm::Type *IrEmitter::infer_lit_type(const Expr &expr) {
  auto &c = *ctx->llvm_ctx;
  return std::visit(
      overloaded{
          [&](const LiteralExpr &e) -> llvm::Type * {
            return std::visit(
                overloaded{
                    [&](int64_t) -> llvm::Type * { return i32(c); },
                    [&](bool) -> llvm::Type * {
                      return llvm::IntegerType::getInt1Ty(c);
                    },
                    [&](float) -> llvm::Type * {
                      return llvm::Type::getFloatTy(c);
                    },
                    [&](char) -> llvm::Type * {
                      return llvm::IntegerType::getInt8Ty(c);
                    },
                    [&](const std::string &) -> llvm::Type * {
                      return ptr_ty(c);
                    },
                },
                e.lit);
          },
          [](const auto &) -> llvm::Type * { return nullptr; },
      },
      expr);
}

// ── runtime helpers ──

llvm::Function *IrEmitter::declare_runtime_func(const string &fn_name,
                                                llvm::Type *ret,
                                                vector<llvm::Type *> params,
                                                bool var_arg) {
  if (auto *f = ctx->module->getFunction(fn_name))
    return f;
  auto *ft = llvm::FunctionType::get(ret, params, var_arg);
  return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, fn_name,
                                ctx->module.get());
}

// ── top-level ──

void IrEmitter::compile(const Program &program) {
  build_classes(program);
  build_class_constructors();

  for (auto &item : program.items)
    std::visit(
        overloaded{
            [&](const FunctionDef &f) { compile_function(f, item.span); },
            [&](const Impl &i) {
              for (auto &m : i.methods)
                compile_method(i.class_name, m, item.span);
            },
            [&](const GlobalVar &v) { compile_global_var(v, item.span); },
            [](const auto &) {},
        },
        item.item);
}

// ── class registration ──

void IrEmitter::build_classes(const Program &program) {
  auto &c = *ctx->llvm_ctx;

  for (auto &item : program.items) {
    auto *cls = std::get_if<Class>(&item.item);
    if (!cls)
      continue;
    auto info = std::make_unique<ClassInfo>();
    info->struct_ty = llvm::StructType::create(c, cls->name);
    for (size_t i = 0; i < cls->fields.size(); ++i) {
      auto &f = cls->fields[i];
      info->field_names.push_back(f.name);
      info->field_types.push_back(f.ty.clone());
      info->field_indices[f.name] = static_cast<uint32_t>(i);
      info->field_defaults.push_back(
          f.init ? optional<const ExprNode *>(&*f.init) : std::nullopt);
    }
    ctx->class_registry[cls->name] = std::move(info);
  }

  for (auto &item : program.items) {
    auto *cls = std::get_if<Class>(&item.item);
    if (!cls)
      continue;
    auto it = ctx->class_registry.find(cls->name);
    if (it == ctx->class_registry.end())
      continue;
    vector<llvm::Type *> fields;
    for (auto &t : it->second->field_types)
      fields.push_back(llvm_type(t));
    it->second->struct_ty->setBody(fields);
  }

  for (auto &item : program.items) {
    auto *imp = std::get_if<Impl>(&item.item);
    if (!imp)
      continue;
    auto it = ctx->class_registry.find(imp->class_name);
    if (it == ctx->class_registry.end())
      continue;
    string prefix = imp->class_name + "_";
    for (auto &m : imp->methods)
      it->second->methods[m.function_header.name] =
          prefix + m.function_header.name;
  }
}

void IrEmitter::build_class_constructors() {
  for (auto &kv : ctx->class_registry) {
    string name = kv.first().str();
    compile_class_constructor(name, kv.second->struct_ty, *kv.second);
    compile_class_clone(name, kv.second->struct_ty, *kv.second);
    if (!kv.second->methods.count("clone"))
      kv.second->methods["clone"] = name + "_clone";
  }
}

void IrEmitter::compile_class_constructor(const string &name,
                                          llvm::StructType *st,
                                          const ClassInfo &info) {
  auto fn_name = name + "_ctor";
  if (ctx->module->getFunction(fn_name))
    return;

  auto &c = *ctx->llvm_ctx;
  auto field_tys = st->elements();
  vector<llvm::Type *> param_tys;
  for (size_t i = 0; i < field_tys.size(); ++i)
    param_tys.push_back(field_tys[i]);

  auto *ft = llvm::FunctionType::get(ptr_ty(c), param_tys, false);
  auto *fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                    fn_name, ctx->module.get());
  ctx->builder->SetInsertPoint(llvm::BasicBlock::Create(c, "entry", fn));

  auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
  auto *sz = llvm::ConstantExpr::getTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(st), i64(c));
  auto *instance = ctx->builder->CreateCall(malloc_fn, {sz}, "this");

  for (size_t i = 0; i < field_tys.size(); ++i) {
    auto *fp =
        ctx->builder->CreateStructGEP(st, instance, static_cast<unsigned>(i));
    ctx->builder->CreateStore(fn->getArg(static_cast<unsigned>(i)), fp);
  }
  ctx->builder->CreateRet(instance);
}

void IrEmitter::compile_class_clone(const string &name, llvm::StructType *st,
                                    const ClassInfo &info) {
  auto fn_name = name + "_clone";
  if (ctx->module->getFunction(fn_name))
    return;

  auto &c = *ctx->llvm_ctx;
  auto *ft = llvm::FunctionType::get(ptr_ty(c), {ptr_ty(c)}, false);
  auto *fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage,
                                    fn_name, ctx->module.get());
  fn->getArg(0)->setName("self");

  ctx->builder->SetInsertPoint(llvm::BasicBlock::Create(c, "entry", fn));
  auto *self = fn->getArg(0);

  auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
  auto *sz = llvm::ConstantExpr::getTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(st), i64(c));
  auto *new_obj = ctx->builder->CreateCall(malloc_fn, {sz}, "clone");

  auto field_tys = st->elements();
  for (size_t i = 0; i < info.field_types.size() && i < field_tys.size(); ++i) {
    auto idx = static_cast<unsigned>(i);
    auto *src_fp = ctx->builder->CreateStructGEP(st, self, idx);

    if (std::get_if<ast::Type::List>(&info.field_types[i].data)) {
      const auto &lt = info.field_types[i];
      auto et = std::get<ast::Type::List>(lt.data).inner
                    ? llvm_type(*std::get<ast::Type::List>(lt.data).inner)
                    : i32(c);
      auto *lst = lookup_or_create_list_type(et)->struct_ty;

      auto *len_val = ctx->builder->CreateLoad(i64(c),
                                               ctx->builder->CreateStructGEP(lst, src_fp, 0));
      auto *old_data = ctx->builder->CreateLoad(ptr_ty(c),
                                                ctx->builder->CreateStructGEP(lst, src_fp, 2));

      auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
          llvm::ConstantExpr::getSizeOf(et), i64(c));
      auto *total = ctx->builder->CreateMul(len_val, elem_sz);
      auto *new_data = ctx->builder->CreateCall(malloc_fn, {total}, "list.clone");
      auto *memcpy_fn = declare_runtime_func("memcpy", ptr_ty(c),
                                             {ptr_ty(c), ptr_ty(c), i64(c)});
      ctx->builder->CreateCall(memcpy_fn, {new_data, old_data, total});

      auto *dest_fp = ctx->builder->CreateStructGEP(st, new_obj, idx);
      ctx->builder->CreateStore(len_val,
                                ctx->builder->CreateStructGEP(lst, dest_fp, 0));
      ctx->builder->CreateStore(len_val,
                                ctx->builder->CreateStructGEP(lst, dest_fp, 1));
      ctx->builder->CreateStore(new_data,
                                ctx->builder->CreateStructGEP(lst, dest_fp, 2));
    } else if (std::get_if<ast::Type::Class>(&info.field_types[i].data)) {
      auto &cls_name = std::get<ast::Type::Class>(info.field_types[i].data).name;
      auto clone_fn_name = cls_name + "_clone";
      auto *clone_fn = ctx->module->getFunction(clone_fn_name);
      if (!clone_fn)
        clone_fn = llvm::Function::Create(
            llvm::FunctionType::get(ptr_ty(c), {ptr_ty(c)}, false),
            llvm::Function::ExternalLinkage, clone_fn_name, ctx->module.get());
      auto *src_cls =
          ctx->builder->CreateLoad(field_tys[i], src_fp);
      auto *cloned_cls = ctx->builder->CreateCall(clone_fn, {src_cls});
      auto *dest_fp = ctx->builder->CreateStructGEP(st, new_obj, idx);
      ctx->builder->CreateStore(cloned_cls, dest_fp);
    } else {
      auto *val = ctx->builder->CreateLoad(field_tys[i], src_fp);
      auto *dest_fp = ctx->builder->CreateStructGEP(st, new_obj, idx);
      ctx->builder->CreateStore(val, dest_fp);
    }
  }

  ctx->builder->CreateRet(new_obj);
}

// ── global variables ──

void IrEmitter::compile_global_var(const GlobalVar &gv, Span span) {
  llvm::Constant *cinit = nullptr;
  llvm::Type *var_ty = nullptr;

  if (gv.ty)
    var_ty = llvm_type(*gv.ty);

  if (gv.init) {
    auto *init_val = compile_expr(*gv.init);
    if (auto *c = llvm::dyn_cast<llvm::Constant>(init_val)) {
      cinit = c;
      if (!var_ty)
        var_ty = cinit->getType();
    } else if (!var_ty) {
      var_ty = init_val->getType();
    }
  }

  if (!var_ty)
    return;

  if (!cinit) {
    if (gv.init) {
      diag.error(span,
                 "Global variable requires constant initializer: " + gv.name)
          .emit_to(diag);
    }
    cinit = zero_const(var_ty);
  }

  auto *gv_ptr = new llvm::GlobalVariable(
      *ctx->module, var_ty, gv.ty.has_value(),
      llvm::GlobalValue::ExternalLinkage, cinit, gv.name);
  env->declare_var(gv.name, gv_ptr, var_ty, var_ty, BorrowKind::None, {},
                   gv.ty ? llvm_func_type(*gv.ty) : nullptr);
}

// ── functions ──

void IrEmitter::compile_function(const FunctionDef &func, Span span) {
  compile_named_function(func, func.function_header.name, span);
}

void IrEmitter::compile_method(const string &cls, const FunctionDef &func,
                               Span span) {
  compile_named_function(func, cls + "_" + func.function_header.name, span);
}

void IrEmitter::compile_named_function(const FunctionDef &func,
                                       const string &name, Span) {
  auto &c = *ctx->llvm_ctx;
  auto &hdr = func.function_header;

  vector<llvm::Type *> ptypes;
  for (auto &p : hdr.params) {
    auto info = resolve_param_type(p.ty);
    ptypes.push_back(info.param_ty);
  }
  auto *ret = hdr.return_type ? llvm_type(*hdr.return_type) : void_ty(c);

  auto *ft = llvm::FunctionType::get(ret, ptypes, false);
  auto *fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name,
                                    ctx->module.get());
  ctx->builder->SetInsertPoint(llvm::BasicBlock::Create(c, "entry", fn));

  auto saved_fn = std::exchange(current_function, fn);
  EnvGuard guard(*this, std::make_shared<Env>(env));

  for (size_t i = 0; auto &arg : fn->args()) {
    auto &p = hdr.params[i];
    arg.setName(p.name);
    auto *a = ctx->builder->CreateAlloca(arg.getType(), nullptr, p.name);
    ctx->builder->CreateStore(&arg, a);

    auto info = resolve_param_type(p.ty);
    env->declare_var(p.name, a, info.param_ty, info.value_ty, info.borrow_kind,
                     ptr_deref_chain(p.ty),
                     llvm_func_type(p.ty));

    if (info.borrow_kind == BorrowKind::None ||
        info.borrow_kind == BorrowKind::Own) {
      auto kind = CleanupManager::classify_type(p.ty);
      if (kind == CleanupKind::ClassFree)
        cleanup_mgr.register_class_cleanup(*env, a, info.param_ty);
      else if (kind == CleanupKind::ListDataFree) {
        auto *st = llvm::cast<llvm::StructType>(info.param_ty);
        cleanup_mgr.register_list_cleanup(*env, a, info.param_ty, st);
      } else if (kind == CleanupKind::OwnListFree) {
        auto &own = std::get<ast::Type::Own>(p.ty.data);
        auto &list_t = std::get<ast::Type::List>(own.inner->data);
        auto *et = list_t.inner ? llvm_type(*list_t.inner) : i32(c);
        auto *st = lookup_or_create_list_type(et)->struct_ty;
        cleanup_mgr.register_own_list_cleanup(*env, a, info.param_ty, st);
      } else if (kind == CleanupKind::StrDataFree) {
        auto *st = get_str_type()->struct_ty;
        cleanup_mgr.register_str_cleanup(*env, a, info.param_ty, st);
      }
    }

    ++i;
  }

  compile_block(func.body);
  if (!ctx->builder->GetInsertBlock()->getTerminator()) {
    cleanup_mgr.emit_scope_cleanup(*env);
    if (ret->isVoidTy())
      ctx->builder->CreateRetVoid();
    else
      ctx->builder->CreateUnreachable();
  }

  current_function = saved_fn;
}

llvm::Value *IrEmitter::compile_lambda(const LambdaExpr &lambda) {
  auto &c = *ctx->llvm_ctx;

  vector<llvm::Type *> ptypes;
  for (auto &p : lambda.params)
    ptypes.push_back(resolve_param_type(p.ty).param_ty);
  auto *ret = lambda.return_type ? llvm_type(*lambda.return_type) : void_ty(c);
  auto *ft = llvm::FunctionType::get(ret, ptypes, false);

  string name = "__lambda_" + std::to_string(ctx->lambda_counter);
  auto *fn = llvm::Function::Create(ft, llvm::Function::InternalLinkage, name,
                                    ctx->module.get());

  auto saved_fn = std::exchange(current_function, fn);
  auto saved_insert = ctx->builder->saveIP();

  auto view = lambda.params |
              std::views::transform([](const Parameter &p) { return p.name; });
  unordered_set<string> param_set(view.begin(), view.end());

  unordered_set<string> captured;
  if (lambda.body)
    for (auto &stmt : lambda.body->stmts)
      collect_free_vars(stmt, param_set, captured);

  unordered_map<string, llvm::GlobalVariable *> capture_globals;
  for (auto &cap_name : captured) {
    auto vi = env->lookup_var(cap_name);
    if (!vi.ptr)
      continue;
    auto *p_ty = llvm::PointerType::get(c, 0);
    auto *gv = new llvm::GlobalVariable(
        *ctx->module, p_ty, false, llvm::GlobalValue::InternalLinkage,
        llvm::ConstantPointerNull::get(p_ty), name + "_cap_" + cap_name);
    ctx->builder->restoreIP(saved_insert);
    if (vi.borrow_kind == BorrowKind::Ref ||
        vi.borrow_kind == BorrowKind::CRef) {
      auto *ref_ptr = ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
      ctx->builder->CreateStore(ref_ptr, gv);
    } else {
      auto *el_ty = vi.alloca_ty;
      auto *malloc_fn = declare_runtime_func("malloc", p_ty, {i64(c)});
      auto *sz = llvm::ConstantExpr::getTruncOrBitCast(
          llvm::ConstantExpr::getSizeOf(el_ty), i64(c));
      auto *heap_ptr =
          ctx->builder->CreateCall(malloc_fn, {sz}, cap_name + "_boxed");
      auto *val = ctx->builder->CreateLoad(el_ty, vi.ptr, cap_name + "_val");
      ctx->builder->CreateStore(val, heap_ptr);
      ctx->builder->CreateStore(heap_ptr, gv);
    }
    saved_insert = ctx->builder->saveIP();
    capture_globals[cap_name] = gv;
  }

  ctx->builder->SetInsertPoint(llvm::BasicBlock::Create(c, "entry", fn));
  EnvGuard guard(*this, std::make_shared<Env>(env));

  for (size_t i = 0; auto &arg : fn->args()) {
    auto &p = lambda.params[i];
    arg.setName(p.name);
    auto *a = ctx->builder->CreateAlloca(arg.getType(), nullptr, p.name);
    ctx->builder->CreateStore(&arg, a);

    auto info = resolve_param_type(p.ty);
    env->declare_var(p.name, a, info.param_ty, info.value_ty, info.borrow_kind,
                     ptr_deref_chain(p.ty),
                     llvm_func_type(p.ty));
    ++i;
  }

  for (auto &[cap_name, gv] : capture_globals) {
    auto vi = env->lookup_var(cap_name);
    auto *ptr = ctx->builder->CreateLoad(llvm::PointerType::get(c, 0), gv,
                                         cap_name + "_ptr");
    env->declare_var(cap_name, ptr, vi.alloca_ty, vi.value_ty, vi.borrow_kind,
                     {}, vi.func_ty);
  }

  if (lambda.body)
    compile_block(*lambda.body);
  if (!ctx->builder->GetInsertBlock()->getTerminator()) {
    cleanup_mgr.emit_scope_cleanup(*env);
    if (ret->isVoidTy())
      ctx->builder->CreateRetVoid();
    else
      ctx->builder->CreateUnreachable();
  }

  ctx->builder->restoreIP(saved_insert);
  current_function = saved_fn;
  ctx->lambda_counter++;
  return fn;
}

void IrEmitter::collect_free_vars(
    const StmtNode &stmt, const unordered_set<string> &params,
    unordered_set<string> &captured) {
  std::visit(overloaded{
                 [&](const VarDefStmt &s) {
                   if (s.init)
                     collect_free_vars_expr(*s.init, params, captured);
                 },
                 [&](const IfStmt &s) {
                   collect_free_vars_expr(s.condition, params, captured);
                   for (auto &st : s.then_branch->stmts)
                     collect_free_vars(st, params, captured);
                   for (auto &[cond, blk] : s.elif_branch) {
                     collect_free_vars_expr(cond, params, captured);
                     for (auto &st2 : blk->stmts)
                       collect_free_vars(st2, params, captured);
                   }
                   if (s.else_branch)
                     for (auto &st2 : s.else_branch->stmts)
                       collect_free_vars(st2, params, captured);
                 },
                 [&](const LoopStmt &s) {
                   collect_free_vars_expr(s.condition, params, captured);
                   for (auto &st : s.body->stmts)
                     collect_free_vars(st, params, captured);
                 },
                 [&](const ExprStmt &s) {
                   collect_free_vars_expr(s.expr, params, captured);
                 },
                 [&](const ReturnStmt &s) {
                   if (s.expr)
                     collect_free_vars_expr(*s.expr, params, captured);
                 },
                 [&](const BreakStmt &) {},
                 [&](const ContinueStmt &) {},
                 [&](const BlockStmt &s) {
                   for (auto &st : s.block->stmts)
                     collect_free_vars(st, params, captured);
                 },
             },
             stmt.stmt);
}

void IrEmitter::collect_free_vars_expr(
    const ExprNode &expr, const unordered_set<string> &params,
    unordered_set<string> &captured) {
  std::visit(overloaded{
                 [&](const Variable &v) {
                   if (params.find(v.name) == params.end())
                     captured.insert(v.name);
                 },
                 [&](const LiteralExpr &) {},
                 [&](const AssignExpr &a) {
                   collect_free_vars_expr(*a.target, params, captured);
                   collect_free_vars_expr(*a.value, params, captured);
                 },
                 [&](const BinaryExpr &b) {
                   collect_free_vars_expr(*b.lhs, params, captured);
                   collect_free_vars_expr(*b.rhs, params, captured);
                 },
                 [&](const UnaryExpr &u) {
                   collect_free_vars_expr(*u.expr, params, captured);
                 },
                 [&](const CallExpr &c) {
                   collect_free_vars_expr(*c.callee, params, captured);
                   for (auto &a : c.args)
                     collect_free_vars_expr(*a, params, captured);
                 },
                 [&](const MemberExpr &m) {
                   collect_free_vars_expr(*m.object, params, captured);
                 },
                 [&](const IndexExpr &i) {
                   collect_free_vars_expr(*i.object, params, captured);
                   collect_free_vars_expr(*i.index, params, captured);
                 },
                 [&](const ListExpr &l) {
                   for (auto &e : l.elements)
                     collect_free_vars_expr(*e, params, captured);
                 },
                 [&](const LambdaExpr &) {},
             },
             expr.expr);
}

// ── statements ──

void IrEmitter::compile_block(const Block &block) {
  for (auto &s : block.stmts) {
    if (ctx->builder->GetInsertBlock()->getTerminator())
      break;
    compile_stmt(s);
  }
}

void IrEmitter::compile_stmt(const StmtNode &sn) {
  std::visit(
      overloaded{
          [&](const VarDefStmt &s) { compile_var_def(s); },
          [&](const IfStmt &s) { compile_if(s); },
          [&](const LoopStmt &s) { compile_while(s); },
          [&](const ExprStmt &s) { (void)compile_expr(s.expr); },
          [&](const ReturnStmt &s) {
            llvm::Value *ret_val = nullptr;
            if (s.expr) {
              ret_val = compile_expr(*s.expr);
              if (!ret_val)
                return;
              if (auto *var = std::get_if<Variable>(&s.expr->expr)) {
                auto vi = env->lookup_var(var->name);
                if (vi.ptr && vi.borrow_kind == BorrowKind::Own)
                  cleanup_mgr.cancel_cleanup(*env, vi.ptr);
              }
            }
            cleanup_mgr.emit_all_cleanups(*env);
            if (s.expr)
              ctx->builder->CreateRet(ret_val);
            else
              ctx->builder->CreateRetVoid();
          },
          [&](const BreakStmt &) {
            if (auto lp = env->lookup_loop()) {
              cleanup_mgr.emit_until_loop(*env);
              ctx->builder->CreateBr(lp->exit_bb);
            }
          },
          [&](const ContinueStmt &) {
            if (auto lp = env->lookup_loop()) {
              cleanup_mgr.emit_until_loop(*env);
              ctx->builder->CreateBr(lp->cond_bb);
            }
          },
          [&](const BlockStmt &s) {
            EnvGuard g(*this, std::make_shared<Env>(env));
            compile_block(*s.block);
            cleanup_mgr.emit_scope_cleanup(*env);
          },
      },
      sn.stmt);
}

void IrEmitter::compile_var_def(const VarDefStmt &s) {
  auto &c = *ctx->llvm_ctx;

  BorrowKind kind = BorrowKind::None;
  if (s.ty) {
    if (std::get_if<ast::Type::Ref>(&s.ty->data))
      kind = BorrowKind::Ref;
    else if (std::get_if<ast::Type::CRef>(&s.ty->data))
      kind = BorrowKind::CRef;
    else if (std::get_if<ast::Type::Own>(&s.ty->data))
      kind = BorrowKind::Own;
    else if (s.ty->is_struct_type())
      kind = BorrowKind::Own;
  }

  llvm::Value *init_val = nullptr;
  if (s.init) {
    if (kind == BorrowKind::Ref || kind == BorrowKind::CRef) {
      if (auto *var = std::get_if<Variable>(&s.init->expr)) {
        auto vi = env->lookup_var(var->name);
        if (vi.ptr) {
          if (vi.borrow_kind == BorrowKind::Ref ||
              vi.borrow_kind == BorrowKind::CRef)
            init_val = ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
          else
            init_val = vi.ptr;
        }
      }
      if (!init_val) {
        init_val = compile_expr(*s.init);
      }
    } else {
      init_val = compile_expr(*s.init);
    }
  }

  llvm::Type *alloca_ty =
      init_val   ? init_val->getType()
      : s.ty     ? llvm_type(*s.ty)
                 : i32(c);
  if (llvm::isa<llvm::FunctionType>(alloca_ty))
    alloca_ty = llvm::PointerType::get(alloca_ty, 0);

  auto *a = ctx->builder->CreateAlloca(alloca_ty, nullptr, s.name);
  ctx->builder->CreateStore(
      init_val ? init_val : llvm::Constant::getNullValue(alloca_ty), a);

  llvm::FunctionType *func_ty = nullptr;
  if (s.ty) {
    func_ty = llvm_func_type(*s.ty);
  } else if (init_val && llvm::isa<llvm::Function>(init_val)) {
    func_ty = llvm::cast<llvm::Function>(init_val)->getFunctionType();
  } else if (s.init && std::holds_alternative<CallExpr>(s.init->expr)) {
    if (auto *sym = sema.get_symbol_table().resolve(s.name))
      if (sym->get_type().has_value())
        func_ty = llvm_func_type(*sym->get_type());
  }

  llvm::Type *val_ty = alloca_ty;
  if (kind == BorrowKind::Ref && s.ty) {
    auto &ref = std::get<ast::Type::Ref>(s.ty->data);
    val_ty = ref.inner ? llvm_type(*ref.inner) : alloca_ty;
  }
  if (kind == BorrowKind::CRef && s.ty) {
    auto &cref = std::get<ast::Type::CRef>(s.ty->data);
    val_ty = cref.inner ? llvm_type(*cref.inner) : alloca_ty;
  }

  if (kind == BorrowKind::None && !s.ty && init_val &&
      init_val->getType()->isPointerTy() &&
      !llvm::isa<llvm::Function>(init_val))
    kind = BorrowKind::Own;

  env->declare_var(s.name, a, alloca_ty, val_ty, kind,
                   s.ty ? ptr_deref_chain(*s.ty) : vector<llvm::Type *>{},
                   func_ty);

  bool registered_cleanup = false;

  if (s.ty && kind != BorrowKind::Ref && kind != BorrowKind::CRef) {
    auto ckind = CleanupManager::classify_type(*s.ty);
    switch (ckind) {
    case CleanupKind::ClassFree:
      cleanup_mgr.register_class_cleanup(*env, a, alloca_ty);
      registered_cleanup = true;
      break;
    case CleanupKind::ListDataFree:
      if (auto *st = llvm::dyn_cast<llvm::StructType>(alloca_ty)) {
        cleanup_mgr.register_list_cleanup(*env, a, alloca_ty, st);
        registered_cleanup = true;
      }
      break;
    case CleanupKind::OwnListFree: {
      auto &own = std::get<ast::Type::Own>(s.ty->data);
      auto &list_t = std::get<ast::Type::List>(own.inner->data);
      auto *et = list_t.inner ? llvm_type(*list_t.inner) : i32(c);
      auto *st = lookup_or_create_list_type(et)->struct_ty;
      cleanup_mgr.register_own_list_cleanup(*env, a, alloca_ty, st);
      registered_cleanup = true;
      break;
    }
    case CleanupKind::StrDataFree: {
      auto *st = get_str_type()->struct_ty;
      cleanup_mgr.register_str_cleanup(*env, a, alloca_ty, st);
      registered_cleanup = true;
      break;
    }
    case CleanupKind::None:
      break;
    }
  } else if (!s.ty && init_val && init_val->getType()->isPointerTy() &&
             !llvm::isa<llvm::Function>(init_val) &&
             !llvm::isa<llvm::Constant>(init_val)) {
    cleanup_mgr.register_class_cleanup(*env, a, alloca_ty);
    registered_cleanup = true;
  }

  if (kind == BorrowKind::Own) {
    if (s.init)
      invalidate_source(*s.init);
  } else if (registered_cleanup && s.init && !s.ty) {
    if (auto *var = std::get_if<Variable>(&s.init->expr)) {
      auto vi = env->lookup_var(var->name);
      if (vi.ptr &&
          vi.borrow_kind != BorrowKind::Ref &&
          vi.borrow_kind != BorrowKind::CRef)
        invalidate_source(*s.init);
    }
  }
}

void IrEmitter::compile_if(const IfStmt &s) {
  auto &c = *ctx->llvm_ctx;
  auto *fn = ctx->builder->GetInsertBlock()->getParent();
  auto *merge = llvm::BasicBlock::Create(c, "ifcont", fn);
  auto *then = llvm::BasicBlock::Create(c, "then", fn);
  auto *cond = compile_expr(s.condition);

  if (s.elif_branch.empty() && !s.else_branch) {
    ctx->builder->CreateCondBr(cond, then, merge);
    ctx->builder->SetInsertPoint(then);
    compile_block(*s.then_branch);
    if (!ctx->builder->GetInsertBlock()->getTerminator())
      ctx->builder->CreateBr(merge);
    ctx->builder->SetInsertPoint(merge);
    return;
  }

  if (s.elif_branch.empty()) {
    auto *els = llvm::BasicBlock::Create(c, "else", fn);
    ctx->builder->CreateCondBr(cond, then, els);
    ctx->builder->SetInsertPoint(then);
    compile_block(*s.then_branch);
    if (!ctx->builder->GetInsertBlock()->getTerminator())
      ctx->builder->CreateBr(merge);
    ctx->builder->SetInsertPoint(els);
    compile_block(*s.else_branch);
    if (!ctx->builder->GetInsertBlock()->getTerminator())
      ctx->builder->CreateBr(merge);
    ctx->builder->SetInsertPoint(merge);
    return;
  }

  auto *next = llvm::BasicBlock::Create(c, "elif", fn);
  ctx->builder->CreateCondBr(cond, then, next);
  ctx->builder->SetInsertPoint(then);
  compile_block(*s.then_branch);
  if (!ctx->builder->GetInsertBlock()->getTerminator())
    ctx->builder->CreateBr(merge);

  for (size_t i = 0; i < s.elif_branch.size(); ++i) {
    auto &el = s.elif_branch[i];
    ctx->builder->SetInsertPoint(next);
    auto *ethen = llvm::BasicBlock::Create(c, "elif.then", fn);
    llvm::BasicBlock *enext;
    if (i + 1 < s.elif_branch.size())
      enext = llvm::BasicBlock::Create(c, "elif", fn);
    else if (s.else_branch)
      enext = llvm::BasicBlock::Create(c, "else", fn);
    else
      enext = merge;

    ctx->builder->CreateCondBr(compile_expr(el.first), ethen, enext);
    ctx->builder->SetInsertPoint(ethen);
    compile_block(*el.second);
    if (!ctx->builder->GetInsertBlock()->getTerminator())
      ctx->builder->CreateBr(merge);

    if (i + 1 == s.elif_branch.size() && s.else_branch) {
      ctx->builder->SetInsertPoint(enext);
      compile_block(*s.else_branch);
      if (!ctx->builder->GetInsertBlock()->getTerminator())
        ctx->builder->CreateBr(merge);
    }
    next = enext;
  }
  ctx->builder->SetInsertPoint(merge);
}

void IrEmitter::compile_while(const LoopStmt &s) {
  auto &c = *ctx->llvm_ctx;
  auto *fn = ctx->builder->GetInsertBlock()->getParent();
  auto *cond_bb = llvm::BasicBlock::Create(c, "while.cond", fn);
  auto *body_bb = llvm::BasicBlock::Create(c, "while.body", fn);
  auto *exit_bb = llvm::BasicBlock::Create(c, "while.exit", fn);

  auto saved = env->lookup_loop();
  env->set_loop({cond_bb, exit_bb});

  ctx->builder->CreateBr(cond_bb);
  ctx->builder->SetInsertPoint(cond_bb);
  ctx->builder->CreateCondBr(compile_expr(s.condition), body_bb, exit_bb);
  ctx->builder->SetInsertPoint(body_bb);
  compile_block(*s.body);
  if (!ctx->builder->GetInsertBlock()->getTerminator())
    ctx->builder->CreateBr(cond_bb);

  ctx->builder->SetInsertPoint(exit_bb);
  if (saved)
    env->set_loop(*saved);
  else
    env->loop_info = std::nullopt;
}

// ── expressions ──

llvm::Value *IrEmitter::compile_expr(const ExprNode &expr_node) {
  auto &expr = expr_node.expr;
  auto &span = expr_node.span;
  return std::visit(
      overloaded{
          [&](const LiteralExpr &e) { return compile_literal(e); },
          [&](const Variable &e) { return load_variable(e.name, span); },
          [&](const AssignExpr &e) { return compile_assignment(e); },
          [&](const BinaryExpr &e) { return compile_binary(e); },
          [&](const UnaryExpr &e) { return compile_unary(e); },
          [&](const CallExpr &e) { return compile_call(e, span); },
          [&](const MemberExpr &e) {
            return compile_member_access(*e.object, e.field);
          },
          [&](const IndexExpr &e) {
            return compile_index(*e.object, *e.index);
          },
          [&](const ListExpr &e) {
            return emit_list_literal(e.elements, span);
          },
          [&](const LambdaExpr &e) { return compile_lambda(e); },
      },
      expr);
}

llvm::Value *IrEmitter::compile_literal(const LiteralExpr &lit) {
  auto &c = *ctx->llvm_ctx;
  return std::visit(
      overloaded{
          [&](int64_t v) -> llvm::Value * {
            if (static_cast<int64_t>(static_cast<int32_t>(v)) != v)
              diag.error(Span{}, "Integer literal out of i32 range: " +
                                     std::to_string(v))
                  .emit_to(diag);
            return llvm::ConstantInt::get(i32(c), static_cast<int32_t>(v));
          },
          [&](bool v) -> llvm::Value * {
            return llvm::ConstantInt::get(llvm::IntegerType::getInt1Ty(c), v);
          },
          [&](float v) -> llvm::Value * {
            return llvm::ConstantFP::get(llvm::Type::getFloatTy(c), v);
          },
          [&](char v) -> llvm::Value * {
            return llvm::ConstantInt::get(llvm::IntegerType::getInt8Ty(c), v);
          },
          [&](const std::string &s) -> llvm::Value * {
            return emit_string_literal(s);
          },
      },
      lit.lit);
}

llvm::Value *IrEmitter::compile_binary(const BinaryExpr &bin) {
  auto *l = compile_expr(*bin.lhs);
  auto *r = compile_expr(*bin.rhs);
  if (!l || !r)
    return nullptr;
  bool fp = l->getType()->isFloatTy();
  auto &b = *ctx->builder;
  switch (bin.op) {
  case BinaryOp::Add:
    return fp ? b.CreateFAdd(l, r) : b.CreateAdd(l, r);
  case BinaryOp::Sub:
    return fp ? b.CreateFSub(l, r) : b.CreateSub(l, r);
  case BinaryOp::Mul:
    return fp ? b.CreateFMul(l, r) : b.CreateMul(l, r);
  case BinaryOp::Div:
    return fp ? b.CreateFDiv(l, r) : b.CreateSDiv(l, r);
  case BinaryOp::Eq:
    return fp ? b.CreateFCmpOEQ(l, r) : b.CreateICmpEQ(l, r);
  case BinaryOp::NotEq:
    return fp ? b.CreateFCmpONE(l, r) : b.CreateICmpNE(l, r);
  case BinaryOp::Lt:
    return fp ? b.CreateFCmpOLT(l, r) : b.CreateICmpSLT(l, r);
  case BinaryOp::Gt:
    return fp ? b.CreateFCmpOGT(l, r) : b.CreateICmpSGT(l, r);
  case BinaryOp::Le:
    return fp ? b.CreateFCmpOLE(l, r) : b.CreateICmpSLE(l, r);
  case BinaryOp::Ge:
    return fp ? b.CreateFCmpOGE(l, r) : b.CreateICmpSGE(l, r);
  case BinaryOp::And:
    return b.CreateAnd(l, r);
  case BinaryOp::Or:
    return b.CreateOr(l, r);
  }
  return nullptr;
}

llvm::Value *IrEmitter::compile_unary(const UnaryExpr &u) {
  switch (u.op) {
  case UnaryOp::AddrOf: {
    return std::visit(
        overloaded{
            [&](const Variable &var) -> llvm::Value * {
              return variable_ptr(var.name, u.expr->span);
            },
            [&](const MemberExpr &) -> llvm::Value * {
              return compile_member_ptr(*u.expr);
            },
            [&](const IndexExpr &) -> llvm::Value * {
              return compile_index_ptr(*u.expr);
            },
            [&](const UnaryExpr &inner) -> llvm::Value * {
              if (inner.op == UnaryOp::Deref || inner.op == UnaryOp::AddrOf) {
                auto *v = compile_expr(*u.expr);
                if (!v)
                  return nullptr;
                auto *a =
                    ctx->builder->CreateAlloca(v->getType(), nullptr, "tmp");
                ctx->builder->CreateStore(v, a);
                return a;
              }
              return nullptr;
            },
            [](const auto &) -> llvm::Value * { return nullptr; },
        },
        u.expr->expr);
  }
  case UnaryOp::Deref: {
    if (auto *inner = std::get_if<UnaryExpr>(&u.expr->expr))
      if (inner->op == UnaryOp::AddrOf)
        return compile_expr(*inner->expr);

    auto *ptr_val = compile_expr(*u.expr);
    if (!ptr_val)
      return nullptr;

    int depth = 1;
    const ExprNode *cur = u.expr.get();
    while (true) {
      if (auto *inner = std::get_if<UnaryExpr>(&cur->expr)) {
        if (inner->op == UnaryOp::Deref) {
          ++depth;
          cur = inner->expr.get();
          continue;
        }
      }
      break;
    }
    llvm::Type *elem_ty = nullptr;
    if (auto *var = std::get_if<Variable>(&cur->expr)) {
      auto vi = env->lookup_var(var->name);
      if (depth > 0 && depth <= static_cast<int>(vi.deref_chain.size()))
        elem_ty = vi.deref_chain[depth - 1];
    }
    if (!elem_ty) {
      if (auto *var = std::get_if<Variable>(&u.expr->expr)) {
        auto vi = env->lookup_var(var->name);
        if (!vi.deref_chain.empty())
          elem_ty = vi.deref_chain[0];
      }
    }
    if (!elem_ty)
      return nullptr;
    return ctx->builder->CreateLoad(elem_ty, ptr_val);
  }
  case UnaryOp::Neg: {
    auto *v = compile_expr(*u.expr);
    if (!v)
      return nullptr;
    return v->getType()->isFloatTy()
               ? ctx->builder->CreateFNeg(v)
               : ctx->builder->CreateSub(
                     llvm::ConstantInt::get(v->getType(), 0), v);
  }
  case UnaryOp::Not: {
    auto *v = compile_expr(*u.expr);
    if (!v)
      return nullptr;
    return ctx->builder->CreateIsNull(v);
  }
  default:
    return nullptr;
  }
}

llvm::Value *IrEmitter::compile_call(const CallExpr &call, Span span) {
  return std::visit(
      overloaded{
          [&](const Variable &callee) -> llvm::Value * {
            auto *direct = ctx->module->getFunction(callee.name);
            auto *ctor = ctx->module->getFunction(callee.name + "_ctor");
            if (direct || ctor)
              return compile_direct_or_ctor_call(callee, call, span);

            if (env->has_var(callee.name))
              return compile_indirect_call(callee, call, span);

            if (auto desc =
                    sema.get_builtins().lookup_standalone(callee.name)) {
              vector<llvm::Value *> args;
              for (auto &arg : call.args) {
                auto *arg_val = compile_expr(*arg);
                if (!arg_val)
                  return nullptr;
                args.push_back(arg_val);
              }
              return desc->get().emit(make_ir_gen_context(), args, span);
            }

            return nullptr;
          },
          [&](const MemberExpr &callee) -> llvm::Value * {
            return compile_method_call(callee, call, span);
          },
          [&](const auto &) -> llvm::Value * { return nullptr; },
      },
      call.callee->expr);
}

llvm::Value *
IrEmitter::compile_direct_or_ctor_call(const Variable &callee,
                                       const CallExpr &call, Span) {
  bool is_ctor = false;
  auto *fn = ctx->module->getFunction(callee.name);
  if (!fn) {
    fn = ctx->module->getFunction(callee.name + "_ctor");
    is_ctor = true;
  }

  ClassInfo *ctor_info = nullptr;
  if (is_ctor) {
    auto it = ctx->class_registry.find(callee.name);
    if (it != ctx->class_registry.end())
      ctor_info = it->second.get();
  }

  auto args = compile_args(fn, call.args, 0, ctor_info);

  if (!is_ctor) {
    auto *fn_sym = sema.get_symbol_table().resolve(callee.name);
    auto *fn_data =
        fn_sym ? std::get_if<FunctionData>(&fn_sym->get_kind()) : nullptr;
    if (fn_data) {
      for (size_t i = 0; i < args.size() && i < fn_data->params.size(); ++i) {
        auto &pty = fn_data->params[i].data;
        if (std::get_if<ast::Type::Ref>(&pty) ||
            std::get_if<ast::Type::CRef>(&pty)) {
          if (auto *var = std::get_if<Variable>(&call.args[i]->expr)) {
            auto vi = env->lookup_var(var->name);
            if (vi.ptr)
              args[i] = vi.ptr;
          }
        }
      }
    }
  }

  auto *call_inst = ctx->builder->CreateCall(fn, args);
  if (!is_ctor)
    invalidate_owned_args(callee.name, call.args, 0);
  return call_inst;
}

llvm::Value *
IrEmitter::compile_indirect_call(const Variable &callee, const CallExpr &call,
                                 Span) {
  auto vi = env->lookup_var(callee.name);
  if (!vi.ptr || !vi.func_ty)
    return nullptr;

  auto *fn_ptr = ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
  auto *fn_ty = vi.func_ty;

  vector<llvm::Value *> args;
  args.reserve(call.args.size());
  for (size_t i = 0; i < call.args.size(); ++i) {
    auto *arg_val = compile_expr(*call.args[i]);
    if (!arg_val)
      return nullptr;
    auto *param_ty = fn_ty->getParamType(static_cast<unsigned>(i));
    if (param_ty->isPointerTy() && !arg_val->getType()->isPointerTy())
      if (auto *var = std::get_if<Variable>(&call.args[i]->expr))
        if (auto vi2 = env->lookup_var(var->name); vi2.ptr)
          arg_val = vi2.ptr;
    args.push_back(arg_val);
  }
  return ctx->builder->CreateCall(fn_ty, fn_ptr, args);
}

llvm::Value *IrEmitter::compile_method_call(const MemberExpr &callee,
                                            const CallExpr &call, Span span) {
  auto *self = compile_expr(*callee.object);
  if (!self)
    return nullptr;

  if (auto *st = llvm::dyn_cast<llvm::StructType>(self->getType()))
    if (auto *lt = lookup_list_type_by_struct(st))
      if (auto desc = sema.get_builtins().lookup(runtime::LIST_TAG, callee.field))
        return emit_builtin_method(desc->get(), st, lt->elem_ty,
                                   *callee.object, call.args, span);

  auto *str_st = get_str_type()->struct_ty;
  if (self->getType() == str_st)
    if (auto desc = sema.get_builtins().lookup(runtime::STR_TAG, callee.field))
      return emit_builtin_method(desc->get(), str_st,
                                 llvm::IntegerType::getInt8Ty(*ctx->llvm_ctx),
                                 *callee.object, call.args, span);

  string mangled = callee.field;
  for (auto &kv : ctx->class_registry) {
    auto it = kv.second->methods.find(callee.field);
    if (it != kv.second->methods.end()) {
      mangled = it->second;
      break;
    }
  }
  auto *fn = ctx->module->getFunction(mangled);
  if (!fn) {
    if (self->getType()->isPointerTy()) {
      auto univ = sema.get_builtins().lookup_universal(callee.field);
      if (univ && univ->get().ptr_emit)
        return (*univ->get().ptr_emit)(make_ir_gen_context(), self, {}, span);
    }
    return nullptr;
  }

  const FunctionData *fn_data = nullptr;
  {
    auto *fn_sym = sema.get_symbol_table().resolve(mangled);
    fn_data =
        fn_sym ? std::get_if<FunctionData>(&fn_sym->get_kind()) : nullptr;
  }
  if (fn_data && !fn_data->params.empty()) {
    if (auto *obj_var = std::get_if<Variable>(&callee.object->expr)) {
      auto &self_ty = fn_data->params[0].data;
      if (std::get_if<ast::Type::Ref>(&self_ty) ||
          std::get_if<ast::Type::CRef>(&self_ty)) {
        auto vi = env->lookup_var(obj_var->name);
        if (vi.ptr)
          self = vi.ptr;
      }
    }
  }

  auto args = compile_args(fn, call.args, 1);
  if (fn_data) {
    for (size_t i = 0; i < call.args.size() && (i + 1) < fn_data->params.size(); ++i) {
      auto &pty = fn_data->params[i + 1].data;
      if (std::get_if<ast::Type::Ref>(&pty) ||
          std::get_if<ast::Type::CRef>(&pty)) {
        if (auto *var = std::get_if<Variable>(&call.args[i]->expr)) {
          auto vi = env->lookup_var(var->name);
          if (vi.ptr)
            args[i] = vi.ptr;
        }
      }
    }
  }
  args.insert(args.begin(), self);
  auto *call_inst = ctx->builder->CreateCall(fn, args);

  if (fn_data && !fn_data->params.empty()) {
    auto &self_ty = fn_data->params[0];
    bool is_own = std::get_if<ast::Type::Own>(&self_ty.data) != nullptr;
    bool is_move_struct = !is_own && self_ty.is_struct_type();
    if (is_own || is_move_struct)
      invalidate_source(*callee.object);
  }
  invalidate_owned_args(mangled, call.args, 1);
  return call_inst;
}

vector<llvm::Value *>
IrEmitter::compile_args(llvm::Function *fn, const vector<uptr<ExprNode>> &args,
                        size_t param_offset, const ClassInfo *ctor_info) {
  vector<llvm::Value *> out;
  size_t n_params = fn->getFunctionType()->getNumParams();
  size_t total = ctor_info ? n_params : args.size();

  for (size_t i = 0; i + param_offset < n_params && i < total; ++i) {
    if (ctor_info && i >= args.size()) {
      if (i < ctor_info->field_defaults.size() && ctor_info->field_defaults[i])
        out.push_back(compile_expr(**ctor_info->field_defaults[i]));
      else
        out.push_back(zero_const(
            fn->getFunctionType()->getParamType(
                static_cast<unsigned>(i + param_offset))));
      continue;
    }
    auto *arg_val = compile_expr(*args[i]);
    auto *param_ty =
        fn->getFunctionType()->getParamType(
            static_cast<unsigned>(i + param_offset));
    if (param_ty->isPointerTy() && arg_val &&
        !arg_val->getType()->isPointerTy())
      if (auto *var = std::get_if<Variable>(&args[i]->expr))
        if (auto vi = env->lookup_var(var->name); vi.ptr)
          arg_val = vi.ptr;
    out.push_back(arg_val);
  }
  return out;
}

void IrEmitter::invalidate_owned_args(const string &fn_name,
                                      const vector<uptr<ExprNode>> &args,
                                      size_t param_offset) {
  auto *fn_sym = sema.get_symbol_table().resolve(fn_name);
  auto *fn_data =
      fn_sym ? std::get_if<FunctionData>(&fn_sym->get_kind()) : nullptr;
  if (!fn_data)
    return;
  for (size_t i = 0;
       i < args.size() && (i + param_offset) < fn_data->params.size(); ++i) {
    auto &pty = fn_data->params[i + param_offset].data;
    bool is_own = std::get_if<ast::Type::Own>(&pty) != nullptr;
    bool is_move_struct = !is_own &&
        (std::get_if<ast::Type::Class>(&pty) ||
         std::get_if<ast::Type::List>(&pty) ||
         std::get_if<ast::Type::Str>(&pty));
    if (!is_own && !is_move_struct)
      continue;
    invalidate_source(*args[i]);
  }
}

llvm::Value *IrEmitter::compile_assignment(const AssignExpr &a) {
  auto *val = compile_expr(*a.value);
  if (!val)
    return nullptr;
  std::visit(
      overloaded{
          [&](const Variable &t) {
            auto vi = env->lookup_var(t.name);
            if (!vi.ptr)
              return;
            if (vi.borrow_kind == BorrowKind::CRef) {
              diag.error(a.target->span,
                         "Cannot assign to variable '" + t.name +
                             "' of const reference type")
                  .emit_to(diag);
              return;
            }
            bool self_assign =
                std::holds_alternative<Variable>(a.value->expr) &&
                std::get<Variable>(a.value->expr).name == t.name;
            if (vi.borrow_kind != BorrowKind::Ref && !self_assign)
              cleanup_mgr.emit_var_free(*env, vi.ptr);
            if (vi.borrow_kind == BorrowKind::Ref) {
              auto *dst = ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
              ctx->builder->CreateStore(val, dst);
            } else {
              ctx->builder->CreateStore(val, vi.ptr);
            }
            if (vi.borrow_kind != BorrowKind::Ref && !self_assign) {
              if (auto *st = llvm::dyn_cast<llvm::StructType>(val->getType());
                  st && lookup_list_type_by_struct(st))
                cleanup_mgr.register_list_cleanup(*env, vi.ptr, vi.alloca_ty,
                                                  st);
              else if (auto *st = llvm::dyn_cast<llvm::StructType>(val->getType());
                       st && st == get_str_type()->struct_ty)
                cleanup_mgr.register_str_cleanup(*env, vi.ptr, vi.alloca_ty, st);
              else if (val->getType()->isPointerTy() &&
                       !llvm::isa<llvm::Function>(val))
                cleanup_mgr.register_class_cleanup(*env, vi.ptr, vi.alloca_ty);
            }
            if (!self_assign &&
                (vi.borrow_kind == BorrowKind::Own ||
                 (vi.borrow_kind == BorrowKind::None &&
                  val->getType()->isPointerTy() &&
                  !llvm::isa<llvm::Function>(val))))
              invalidate_source(*a.value);
          },
          [&](const MemberExpr &) {
            if (auto *p = compile_member_ptr(*a.target))
              ctx->builder->CreateStore(val, p);
          },
          [&](const IndexExpr &) {
            if (auto *p = compile_index_ptr(*a.target))
              ctx->builder->CreateStore(val, p);
          },
          [&](const UnaryExpr &u) {
            if (u.op == UnaryOp::Deref) {
              auto *ptr = compile_expr(*u.expr);
              if (ptr)
                ctx->builder->CreateStore(val, ptr);
            }
          },
          [](const auto &) {},
      },
      a.target->expr);
  return val;
}

// ── member / index access ──

llvm::Value *IrEmitter::compile_member_access(const ExprNode &obj,
                                              const string &field) {
  auto *obj_val = compile_expr(obj);
  if (!obj_val)
    return nullptr;
  for (auto &kv : ctx->class_registry) {
    auto it = kv.second->field_indices.find(field);
    if (it == kv.second->field_indices.end())
      continue;
    auto *st = kv.second->struct_ty;
    auto *fp = ctx->builder->CreateStructGEP(st, obj_val, it->second);
    return ctx->builder->CreateLoad(st->getElementType(it->second), fp);
  }
  return nullptr;
}

llvm::Value *IrEmitter::compile_member_ptr(const ExprNode &e) {
  return std::visit(overloaded{
                        [&](const MemberExpr &m) -> llvm::Value * {
                          auto *ov = compile_expr(*m.object);
                          if (!ov)
                            return nullptr;
                          for (auto &kv : ctx->class_registry) {
                            auto it = kv.second->field_indices.find(m.field);
                            if (it != kv.second->field_indices.end())
                              return ctx->builder->CreateStructGEP(
                                  kv.second->struct_ty, ov, it->second);
                          }
                          return nullptr;
                        },
                        [](const auto &) -> llvm::Value * { return nullptr; },
                    },
                    e.expr);
}

llvm::Value *IrEmitter::compile_index(const ExprNode &obj,
                                      const ExprNode &idx) {
  auto *ov = compile_expr(obj);
  auto *iv = compile_expr(idx);
  if (!ov || !iv)
    return nullptr;
  auto &c = *ctx->llvm_ctx;
  if (!iv->getType()->isIntegerTy(64))
    iv = ctx->builder->CreateZExt(iv, i64(c));

  auto *st = llvm::dyn_cast<llvm::StructType>(ov->getType());
  if (!st)
    return nullptr;
  auto *lt = lookup_list_type_by_struct(st);
  if (!lt)
    return nullptr;

  auto *tmp = ctx->builder->CreateAlloca(st, nullptr, "list.tmp");
  ctx->builder->CreateStore(ov, tmp);

  auto *len = ctx->builder->CreateLoad(
      i64(c), ctx->builder->CreateStructGEP(st, tmp, 0));
  emit_bounds_check(iv, len, idx.span);
  auto *data = ctx->builder->CreateLoad(
      ptr_ty(c), ctx->builder->CreateStructGEP(st, tmp, 2));
  return ctx->builder->CreateLoad(
      lt->elem_ty, ctx->builder->CreateGEP(lt->elem_ty, data, iv));
}

llvm::Value *IrEmitter::compile_index_ptr(const ExprNode &e) {
  return std::visit(
      overloaded{
          [&](const IndexExpr &ie) -> llvm::Value * {
            auto *ov = compile_expr(*ie.object);
            auto *iv = compile_expr(*ie.index);
            if (!ov || !iv)
              return nullptr;
            auto &c = *ctx->llvm_ctx;
            if (!iv->getType()->isIntegerTy(64))
              iv = ctx->builder->CreateZExt(iv, i64(c));

            auto *st = llvm::dyn_cast<llvm::StructType>(ov->getType());
            if (st) {
              auto *lt = lookup_list_type_by_struct(st);
              if (lt) {
                auto *tmp =
                    ctx->builder->CreateAlloca(st, nullptr, "list.tmp");
                ctx->builder->CreateStore(ov, tmp);
                auto *len = ctx->builder->CreateLoad(
                    i64(c), ctx->builder->CreateStructGEP(st, tmp, 0));
                emit_bounds_check(iv, len, ie.index->span);
                auto *data = ctx->builder->CreateLoad(
                    ptr_ty(c), ctx->builder->CreateStructGEP(st, tmp, 2));
                return ctx->builder->CreateGEP(lt->elem_ty, data, iv);
              }
              if (st == get_str_type()->struct_ty) {
                auto *tmp =
                    ctx->builder->CreateAlloca(st, nullptr, "str.tmp");
                ctx->builder->CreateStore(ov, tmp);
                auto *len = ctx->builder->CreateLoad(
                    i64(c), ctx->builder->CreateStructGEP(st, tmp, 0));
                emit_bounds_check(iv, len, ie.index->span);
                auto *data = ctx->builder->CreateLoad(
                    ptr_ty(c), ctx->builder->CreateStructGEP(st, tmp, 1));
                return ctx->builder->CreateGEP(
                    llvm::IntegerType::getInt8Ty(c), data, iv);
              }
            }
            if (ov->getType()->isPointerTy())
              return ctx->builder->CreateGEP(llvm::IntegerType::getInt8Ty(c),
                                             ov, iv);
            return nullptr;
          },
          [](const auto &) -> llvm::Value * { return nullptr; },
      },
      e.expr);
}

void IrEmitter::emit_bounds_check(llvm::Value *ix, llvm::Value *len, Span) {
  auto &c = *ctx->llvm_ctx;
  auto *fn = ctx->builder->GetInsertBlock()->getParent();
  auto *ok = llvm::BasicBlock::Create(c, "bounds.ok", fn);
  auto *fail = llvm::BasicBlock::Create(c, "bounds.fail", fn);
  if (ix->getType() != len->getType())
    ix = ctx->builder->CreateZExt(ix, len->getType());
  ctx->builder->CreateCondBr(ctx->builder->CreateICmpULT(ix, len), ok, fail);
  ctx->builder->SetInsertPoint(fail);
  auto *trap = llvm::Intrinsic::getOrInsertDeclaration(ctx->module.get(),
                                                       llvm::Intrinsic::trap);
  ctx->builder->CreateCall(trap);
  ctx->builder->CreateUnreachable();
  ctx->builder->SetInsertPoint(ok);
}

// ── list / string ──

llvm::Value *
IrEmitter::emit_list_literal(const vector<uptr<ExprNode>> &elements,
                             Span) {
  auto &c = *ctx->llvm_ctx;
  vector<llvm::Value *> vals;
  vals.reserve(elements.size());
  llvm::Type *et = i32(c);
  for (size_t i = 0; i < elements.size(); ++i) {
    auto *v = compile_expr(*elements[i]);
    if (v) {
      vals.push_back(v);
      if (i == 0)
        et = v->getType();
    }
  }
  if (vals.size() != elements.size() && vals.empty())
    vals.resize(elements.size(), llvm::Constant::getNullValue(et));

  auto *entry = lookup_or_create_list_type(et);
  auto *a = ctx->builder->CreateAlloca(entry->struct_ty, nullptr, "list");
  size_t n = vals.size();
  auto *len = llvm::ConstantInt::get(i64(c), n);
  auto *cap = llvm::ConstantInt::get(i64(c), n == 0 ? 4 : n * 2);
  ctx->builder->CreateStore(
      len, ctx->builder->CreateStructGEP(entry->struct_ty, a, 0));
  ctx->builder->CreateStore(
      cap, ctx->builder->CreateStructGEP(entry->struct_ty, a, 1));

  auto *df = ctx->builder->CreateStructGEP(entry->struct_ty, a, 2);
  if (n == 0) {
    ctx->builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty(c)), df);
    return ctx->builder->CreateLoad(entry->struct_ty, a);
  }

  auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
  auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(et), i64(c));
  auto *total = ctx->builder->CreateMul(len, elem_sz);
  auto *data = ctx->builder->CreateCall(malloc_fn, {total}, "listdata");
  ctx->builder->CreateStore(data, df);

  for (size_t i = 0; i < n; ++i)
    ctx->builder->CreateStore(
        vals[i],
        ctx->builder->CreateGEP(et, data, llvm::ConstantInt::get(i64(c), i)));

  return ctx->builder->CreateLoad(entry->struct_ty, a);
}

void IrEmitter::emit_list_with_init(llvm::Value *a,
                                    const vector<uptr<ExprNode>> &elements,
                                    Span span) {
  llvm::Type *et = i32(*ctx->llvm_ctx);
  if (!elements.empty()) {
    auto *first = compile_expr(*elements[0]);
    if (first)
      et = first->getType();
  }
  emit_list_with_init_fields(a, elements, et, span);
}

void IrEmitter::emit_list_with_init_fields(
    llvm::Value *a, const vector<uptr<ExprNode>> &elements, llvm::Type *elem_ty,
    Span) {
  auto &c = *ctx->llvm_ctx;
  auto *entry = lookup_or_create_list_type(elem_ty);
  auto *st = entry->struct_ty;

  size_t n = elements.size();
  auto *len = llvm::ConstantInt::get(i64(c), n);
  auto *cap = llvm::ConstantInt::get(i64(c), n == 0 ? 4 : n * 2);
  ctx->builder->CreateStore(len, ctx->builder->CreateStructGEP(st, a, 0));
  ctx->builder->CreateStore(cap, ctx->builder->CreateStructGEP(st, a, 1));

  auto *df = ctx->builder->CreateStructGEP(st, a, 2);
  if (n == 0) {
    ctx->builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty(c)), df);
    return;
  }

  auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
  auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
      llvm::ConstantExpr::getSizeOf(elem_ty), i64(c));
  auto *total = ctx->builder->CreateMul(len, elem_sz);
  auto *buf = ctx->builder->CreateCall(malloc_fn, {total}, "listdata");
  ctx->builder->CreateStore(buf, df);

  for (size_t i = 0; i < n; ++i)
    ctx->builder->CreateStore(
        compile_expr(*elements[i]),
        ctx->builder->CreateGEP(elem_ty, buf,
                                llvm::ConstantInt::get(i64(c), i)));
}

llvm::Value *IrEmitter::emit_builtin_method(
    const runtime::BuiltinMethod &desc, llvm::StructType *st,
    llvm::Type *elem_ty, const ExprNode &obj_expr,
    const vector<uptr<ExprNode>> &args, Span span) {
  auto *list_ptr =
      std::visit(overloaded{
                     [&](const Variable &var) -> llvm::Value * {
                       return variable_ptr(var.name, obj_expr.span);
                     },
                     [](const auto &) -> llvm::Value * { return nullptr; },
                 },
                 obj_expr.expr);
  if (!list_ptr) {
    diag.error(span, "Cannot call '" + desc.meta.name +
                         "' on non-variable expression")
        .emit_to(diag);
    return nullptr;
  }

  vector<llvm::Value *> arg_vals;
  for (auto &a : args) {
    auto *v = compile_expr(*a);
    if (!v)
      return nullptr;
    arg_vals.push_back(v);
  }

  return (*desc.value_emit)(make_ir_gen_context(), list_ptr, st, elem_ty,
                          arg_vals, span);
}

llvm::Value *IrEmitter::emit_string_literal(const string &s) {
  auto &c = *ctx->llvm_ctx;
  auto *st = get_str_type()->struct_ty;
  auto *gv = ctx->builder->CreateGlobalString(s);
  auto *len = llvm::ConstantInt::get(i64(c), s.size());
  llvm::Value *result = llvm::UndefValue::get(st);
  result = ctx->builder->CreateInsertValue(result, len, {0u});
  result = ctx->builder->CreateInsertValue(result, gv, {1u});
  return result;
}

// ── module i/o ──

void IrEmitter::dump_module(std::ostream &os) {
  std::string s;
  llvm::raw_string_ostream rso(s);
  ctx->module->print(rso, nullptr);
  os << rso.str();
}

runtime::IrGenCtxtRef IrEmitter::make_ir_gen_context() {
  return {*ctx->builder, *ctx->module};
}

IrEmitter::ModuleHandle IrEmitter::release_module() {
  return {std::move(ctx->llvm_ctx), std::move(ctx->module)};
}

} // namespace cat::ir
