#include "ir_emitter.h"
#include "expr.h"
#include <llvm-20/llvm/IR/Constants.h>
#include <llvm-20/llvm/IR/Function.h>
#include <llvm-20/llvm/IR/GlobalVariable.h>
#include <llvm-20/llvm/IR/Instructions.h>
#include <llvm-20/llvm/IR/Intrinsics.h>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/Support/raw_ostream.h>

namespace cat::ir {

  template<class... Ts>
  struct overload : Ts... {
    using Ts::operator()...;
  };

  namespace {
    llvm::IntegerType *i32(llvm::LLVMContext &c) { return llvm::IntegerType::getInt32Ty(c); }
    llvm::IntegerType *i64(llvm::LLVMContext &c) { return llvm::IntegerType::getInt64Ty(c); }
    llvm::PointerType *ptr_ty(llvm::LLVMContext &c) { return llvm::PointerType::get(c, 0); }
    llvm::Type *void_ty(llvm::LLVMContext &c) { return llvm::Type::getVoidTy(c); }

    llvm::Constant *zero_const(llvm::Type *ty) {
      if (ty->isIntegerTy()) return llvm::ConstantInt::get(ty, 0);
      if (ty->isFloatTy()) return llvm::ConstantFP::get(ty, 0.0);
      if (ty->isPointerTy()) return llvm::ConstantPointerNull::get(llvm::cast<llvm::PointerType>(ty));
      return llvm::Constant::getNullValue(ty);
    }

    string list_type_key(llvm::Type *et) {
      if (et->isIntegerTy(32)) return "list.i32";
      if (et->isIntegerTy(64)) return "list.i64";
      if (et->isIntegerTy(8)) return "list.i8";
      if (et->isIntegerTy(1)) return "list.bool";
      if (et->isFloatTy()) return "list.float";
      if (et->isPointerTy()) return "list.ptr";
      if (auto *st = llvm::dyn_cast<llvm::StructType>(et))
        return "list." + st->getName().str();
      return "list.unknown";
    }
  }// namespace

  IrEmitter::IrEmitter(const string &name, error::DiagCtxt &diag, semantics::SemaCtxt &sema_ctx)
      : ctx(std::make_unique<CodeGenCtxt>(name)),
        env(std::make_shared<Env>()), diag(diag), current_function(nullptr),
        sema(sema_ctx) {}

  // ── type helpers ──

  llvm::Type *IrEmitter::llvm_type(const ast::Type &ast_type) {
    auto &c = *ctx->llvm_ctx;
    return std::visit(
        [&](auto &v) -> llvm::Type * {
          using T = std::decay_t<decltype(v)>;
          if constexpr (std::is_same_v<T, ast::Type::Int>) return i32(c);
          if constexpr (std::is_same_v<T, ast::Type::Float>) return llvm::Type::getFloatTy(c);
          if constexpr (std::is_same_v<T, ast::Type::Bool>) return llvm::IntegerType::getInt1Ty(c);
          if constexpr (std::is_same_v<T, ast::Type::Char>) return llvm::IntegerType::getInt8Ty(c);
          if constexpr (std::is_same_v<T, ast::Type::Void>) return void_ty(c);
          if constexpr (std::is_same_v<T, ast::Type::Str>) return ptr_ty(c);
          if constexpr (std::is_same_v<T, ast::Type::Ptr>) return ptr_ty(c);
          if constexpr (std::is_same_v<T, ast::Type::Own>) return ptr_ty(c);
          if constexpr (std::is_same_v<T, ast::Type::Ref>) return ptr_ty(c);
          if constexpr (std::is_same_v<T, ast::Type::List>) {
            if (!v.inner) return ptr_ty(c);
            auto et = llvm_type(*v.inner);
            auto key = list_type_key(et);
            auto &e = ctx->list_types[key];
            if (!e) {
              e = std::make_unique<ListType>();
              e->struct_ty = llvm::StructType::create(c, key);
              e->elem_ty = et;
              e->struct_ty->setBody({i64(c), i64(c), ptr_ty(c)});
            }
            return e->struct_ty;
          }
          if constexpr (std::is_same_v<T, ast::Type::Class>) {
            return ptr_ty(c);
          }
          if constexpr (std::is_same_v<T, ast::Type::Func>) {
            vector<llvm::Type *> ptypes;
            for (auto &p: v.params) ptypes.push_back(p ? llvm_type(*p) : i32(c));
            auto *ret = v.ret ? llvm_type(*v.ret) : void_ty(c);
            return llvm::PointerType::get(llvm::FunctionType::get(ret, ptypes, false), 0);
          }
          return i32(c);
        },
        ast_type.data
    );
  }

  llvm::Type *IrEmitter::ast_type_to_llvm_type(const ast::Type &ast_type) {
    return llvm_type(ast_type);
  }

  llvm::FunctionType *IrEmitter::llvm_func_type(const ast::Type &ast_type) {
    if (auto *func = std::get_if<ast::Type::Func>(&ast_type.data)) {
      auto &c = *ctx->llvm_ctx;
      vector<llvm::Type *> ptypes;
      for (auto &p: func->params)
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
        if (!cur) break;
        chain.push_back(llvm_type(*cur));
      } else if (auto *ref = std::get_if<ast::Type::Ref>(&cur->data)) {
        cur = ref->inner.get();
        if (!cur) break;
        chain.push_back(llvm_type(*cur));
      } else if (auto *own = std::get_if<ast::Type::Own>(&cur->data)) {
        cur = own->inner.get();
        if (!cur) break;
        chain.push_back(llvm_type(*cur));
      } else {
        break;
      }
    }
    return chain;
  }

  llvm::Type *IrEmitter::infer_lit_type(const Expr &expr) {
    auto &c = *ctx->llvm_ctx;
    return std::visit(
        overload{
            [&](const LiteralExpr &e) -> llvm::Type * {
              return std::visit(
                  overload{
                      [&](int64_t) -> llvm::Type * { return i32(c); },
                      [&](bool) -> llvm::Type * { return llvm::IntegerType::getInt1Ty(c); },
                      [&](float) -> llvm::Type * { return llvm::Type::getFloatTy(c); },
                      [&](char) -> llvm::Type * { return llvm::IntegerType::getInt8Ty(c); },
                      [&](const std::string &) -> llvm::Type * { return ptr_ty(c); },
                  },
                  e.lit
              );
            },
            [](const auto &) -> llvm::Type * { return nullptr; },
        },
        expr
    );
  }

  // ── runtime helpers ──

  llvm::Function *IrEmitter::declare_runtime_func(const string &fn_name, llvm::Type *ret, vector<llvm::Type *> params, bool var_arg) {
    if (auto *f = ctx->module->getFunction(fn_name)) return f;
    auto *ft = llvm::FunctionType::get(ret, params, var_arg);
    return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, fn_name, ctx->module.get());
  }

  // ── top-level ──

  void IrEmitter::compile(const Program &program) {
    build_classes(program);
    build_class_constructors();

    for (auto &item: program.items)
      std::visit(
          overload{
              [&](const FunctionDef &f) { compile_function(f, item.span); },
              [&](const Impl &i) {
                for (auto &m: i.methods)
                  compile_method(i.class_name, m, item.span);
              },
              [&](const GlobalVar &v) { compile_global_var(v, item.span); },
              [](const auto &) {},
          },
          item.item
      );
  }

  // ── class registration ──

  void IrEmitter::build_classes(const Program &program) {
    auto &c = *ctx->llvm_ctx;

    for (auto &item: program.items) {
      auto *cls = std::get_if<Class>(&item.item);
      if (!cls) continue;
      auto info = std::make_unique<ClassInfo>();
      info->struct_ty = llvm::StructType::create(c, cls->name);
      for (size_t i = 0; i < cls->fields.size(); ++i) {
        auto &f = cls->fields[i];
        info->field_names.push_back(f.name);
        info->field_types.push_back(f.ty.clone());
        info->field_indices[f.name] = static_cast<uint32_t>(i);
        info->field_defaults.push_back(
            f.init ? optional<const ExprNode *>(&*f.init) : std::nullopt
        );
      }
      ctx->class_registry[cls->name] = std::move(info);
    }

    for (auto &item: program.items) {
      auto *cls = std::get_if<Class>(&item.item);
      if (!cls) continue;
      auto it = ctx->class_registry.find(cls->name);
      if (it == ctx->class_registry.end()) continue;
      vector<llvm::Type *> fields;
      for (auto &t: it->second->field_types) fields.push_back(llvm_type(t));
      it->second->struct_ty->setBody(fields);
    }

    for (auto &item: program.items) {
      auto *imp = std::get_if<Impl>(&item.item);
      if (!imp) continue;
      auto it = ctx->class_registry.find(imp->class_name);
      if (it == ctx->class_registry.end()) continue;
      string prefix = imp->class_name + "_";
      for (auto &m: imp->methods)
        it->second->methods[m.function_header.name] = prefix + m.function_header.name;
    }
  }

  void IrEmitter::build_class_constructors() {
    for (auto &kv: ctx->class_registry)
      compile_class_constructor(kv.first().str(), kv.second->struct_ty, *kv.second);
  }

  void IrEmitter::compile_class_constructor(const string &name, llvm::StructType *st, const ClassInfo &info) {
    auto fn_name = name + "_ctor";
    if (ctx->module->getFunction(fn_name)) return;

    auto &c = *ctx->llvm_ctx;
    auto field_tys = st->elements();
    vector<llvm::Type *> param_tys;
    for (size_t i = 0; i < field_tys.size(); ++i)
      param_tys.push_back(field_tys[i]);

    auto *ft = llvm::FunctionType::get(ptr_ty(c), param_tys, false);
    auto *fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, fn_name, ctx->module.get());
    ctx->builder->SetInsertPoint(
        llvm::BasicBlock::Create(c, "entry", fn)
    );

    auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
    auto *sz = llvm::ConstantExpr::getTruncOrBitCast(
        llvm::ConstantExpr::getSizeOf(st), i64(c)
    );
    auto *instance = ctx->builder->CreateCall(malloc_fn, {sz}, "this");

    for (size_t i = 0; i < field_tys.size(); ++i) {
      auto *fp = ctx->builder->CreateStructGEP(st, instance, static_cast<unsigned>(i));
      ctx->builder->CreateStore(fn->getArg(static_cast<unsigned>(i)), fp);
    }
    ctx->builder->CreateRet(instance);
  }

  // ── global variables ──

  void IrEmitter::compile_global_var(const GlobalVar &gv, Span span) {
    llvm::Constant *cinit = nullptr;
    llvm::Type *var_ty = nullptr;

    if (gv.ty) {
      var_ty = llvm_type(*gv.ty);
    }

    if (gv.init) {
      auto *init_val = compile_expr(*gv.init);
      if (auto *c = llvm::dyn_cast<llvm::Constant>(init_val)) {
        cinit = c;
        if (!var_ty) var_ty = cinit->getType();
      } else if (!var_ty) {
        var_ty = init_val->getType();
      }
    }

    if (!var_ty) {
      diag.error(span, "Cannot determine type for global var: " + gv.name).emit_to(diag);
      return;
    }

    if (!cinit) {
      if (gv.init) {
        diag.error(span, "Global variable requires constant initializer: " + gv.name).emit_to(diag);
      }
      cinit = zero_const(var_ty);
    }

    auto *gv_ptr = new llvm::GlobalVariable(
        *ctx->module, var_ty, gv.ty.has_value(),
        llvm::GlobalValue::ExternalLinkage, cinit, gv.name
    );
    env->declare_var(gv.name, gv_ptr, var_ty, var_ty, false, {}, false,
                      gv.ty ? llvm_func_type(*gv.ty) : nullptr);
  }

  // ── functions ──

  void IrEmitter::compile_function(const FunctionDef &func, Span span) {
    compile_named_function(func, func.function_header.name, span);
  }

  void IrEmitter::compile_method(const string &cls, const FunctionDef &func, Span span) {
    compile_named_function(func, cls + "_" + func.function_header.name, span);
  }

  void IrEmitter::compile_named_function(const FunctionDef &func, const string &name, Span span) {
    auto &c = *ctx->llvm_ctx;
    auto &hdr = func.function_header;

    vector<llvm::Type *> ptypes;
    for (auto &p: hdr.params) {
      bool is_ref = std::get_if<ast::Type::Ref>(&p.ty.data) != nullptr;
      bool is_own = std::get_if<ast::Type::Own>(&p.ty.data) != nullptr;
      if (is_own) {
        auto &own = std::get<ast::Type::Own>(p.ty.data);
        ptypes.push_back(own.inner ? llvm_type(*own.inner) : llvm_type(p.ty));
      } else {
        ptypes.push_back(is_ref ? ptr_ty(c) : llvm_type(p.ty));
      }
    }
    auto *ret = hdr.return_type ? llvm_type(*hdr.return_type) : void_ty(c);

    auto *ft = llvm::FunctionType::get(ret, ptypes, false);
    auto *fn = llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, ctx->module.get());
    ctx->builder->SetInsertPoint(
        llvm::BasicBlock::Create(c, "entry", fn)
    );

    auto saved_fn = std::exchange(current_function, fn);
    EnvGuard guard(*this, std::make_shared<Env>(env));

    for (size_t i = 0; auto &arg: fn->args()) {
      auto &p = hdr.params[i];
      arg.setName(p.name);
      auto *a = ctx->builder->CreateAlloca(arg.getType(), nullptr, p.name);
      ctx->builder->CreateStore(&arg, a);
      auto val_ty = llvm_type(p.ty);
      bool is_ref = std::get_if<ast::Type::Ref>(&p.ty.data) != nullptr;
      bool is_own = std::get_if<ast::Type::Own>(&p.ty.data) != nullptr;
      if (is_ref) {
        auto &ref = std::get<ast::Type::Ref>(p.ty.data);
        val_ty = ref.inner ? llvm_type(*ref.inner) : val_ty;
      } else if (is_own) {
        auto &own = std::get<ast::Type::Own>(p.ty.data);
        val_ty = own.inner ? llvm_type(*own.inner) : val_ty;
      }
      env->declare_var(p.name, a, arg.getType(), val_ty, is_ref, ptr_deref_chain(p.ty), is_own,
                       llvm_func_type(p.ty));

      if (!is_ref && !is_own && std::get_if<ast::Type::List>(&p.ty.data)) {
        auto &list_t = std::get<ast::Type::List>(p.ty.data);
        auto *et = list_t.inner ? llvm_type(*list_t.inner) : i32(c);
        auto *st = llvm::cast<llvm::StructType>(arg.getType());
        auto &b = *ctx->builder;
        auto *len_val = b.CreateExtractValue(&arg, {0u});
        auto *old_data = b.CreateExtractValue(&arg, {2u});
        auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
            llvm::ConstantExpr::getSizeOf(et), i64(c)
        );
        auto *total = b.CreateMul(len_val, elem_sz);
        auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
        auto *new_data = b.CreateCall(malloc_fn, {total}, "listcopy");
        auto *memcpy_fn = declare_runtime_func("memcpy", ptr_ty(c), {ptr_ty(c), ptr_ty(c), i64(c)});
        b.CreateCall(memcpy_fn, {new_data, old_data, total});
        b.CreateStore(new_data, b.CreateStructGEP(st, a, 2u));
      }

      ++i;
    }

    compile_block(func.body);
    if (!ctx->builder->GetInsertBlock()->getTerminator()) {
      if (ret->isVoidTy())
        ctx->builder->CreateRetVoid();
      else
        ctx->builder->CreateUnreachable();
    }

    current_function = saved_fn;
  }

  // ── statements ──

  void IrEmitter::compile_block(const Block &block) {
    for (auto &s: block.stmts) {
      if (ctx->builder->GetInsertBlock()->getTerminator()) break;
      compile_stmt(s);
    }
  }

  void IrEmitter::compile_stmt(const StmtNode &sn) {
    std::visit(
        overload{
            [&](const VarDefStmt &s) {
              llvm::Value *iv = s.init ? compile_expr(*s.init) : nullptr;
              llvm::Type *vt = iv     ? iv->getType()
                                : s.ty ? llvm_type(*s.ty)
                                       : i32(*ctx->llvm_ctx);
              if (llvm::isa<llvm::FunctionType>(vt)) {
                vt = llvm::PointerType::get(vt, 0);
              }
              auto *a = ctx->builder->CreateAlloca(vt, nullptr, s.name);
              if (iv) ctx->builder->CreateStore(iv, a);

              bool is_ref = s.ty && std::get_if<ast::Type::Ref>(&s.ty->data);
              bool is_own = s.ty && std::get_if<ast::Type::Own>(&s.ty->data);
              env->declare_var(s.name, a, vt, vt, is_ref, s.ty ? ptr_deref_chain(*s.ty) : vector<llvm::Type *>{}, is_own,
                               s.ty ? llvm_func_type(*s.ty) : nullptr);

              if (s.ty && std::get_if<ast::Type::Own>(&s.ty->data)) {
                if (auto *var = std::get_if<Variable>(&s.init->expr))
                  if (auto vi = env->lookup_var(var->name); vi.ptr)
                    ctx->builder->CreateStore(llvm::UndefValue::get(vi.alloca_ty), vi.ptr);
              }
            },
            [&](const IfStmt &s) { compile_if(s); },
            [&](const LoopStmt &s) { compile_while(s); },
            [&](const ExprStmt &s) { (void) compile_expr(s.expr); },
            [&](const ReturnStmt &s) {
              if (!s.expr.has_value()) {
                ctx->builder->CreateRetVoid();
              } else if (auto *v = compile_expr(*s.expr)) {
                ctx->builder->CreateRet(v);
              }
            },
            [&](const BreakStmt &) {
              if (auto lp = env->lookup_loop()) ctx->builder->CreateBr(lp->exit_bb);
            },
            [&](const ContinueStmt &) {
              if (auto lp = env->lookup_loop()) ctx->builder->CreateBr(lp->cond_bb);
            },
            [&](const BlockStmt &s) {
              EnvGuard g(*this, std::make_shared<Env>(env));
              compile_block(*s.block);
            },
        },
        sn.stmt
    );
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
      if (!ctx->builder->GetInsertBlock()->getTerminator()) ctx->builder->CreateBr(merge);
      ctx->builder->SetInsertPoint(merge);
      return;
    }

    if (s.elif_branch.empty()) {
      auto *els = llvm::BasicBlock::Create(c, "else", fn);
      ctx->builder->CreateCondBr(cond, then, els);
      ctx->builder->SetInsertPoint(then);
      compile_block(*s.then_branch);
      if (!ctx->builder->GetInsertBlock()->getTerminator()) ctx->builder->CreateBr(merge);
      ctx->builder->SetInsertPoint(els);
      compile_block(*s.else_branch);
      if (!ctx->builder->GetInsertBlock()->getTerminator()) ctx->builder->CreateBr(merge);
      ctx->builder->SetInsertPoint(merge);
      return;
    }

    auto *next = llvm::BasicBlock::Create(c, "elif", fn);
    ctx->builder->CreateCondBr(cond, then, next);
    ctx->builder->SetInsertPoint(then);
    compile_block(*s.then_branch);
    if (!ctx->builder->GetInsertBlock()->getTerminator()) ctx->builder->CreateBr(merge);

    for (size_t i = 0; i < s.elif_branch.size(); ++i) {
      auto &el = s.elif_branch[i];
      ctx->builder->SetInsertPoint(next);
      auto *ethen = llvm::BasicBlock::Create(c, "elif.then", fn);
      llvm::BasicBlock *enext;
      if (i + 1 < s.elif_branch.size()) enext = llvm::BasicBlock::Create(c, "elif", fn);
      else if (s.else_branch)
        enext = llvm::BasicBlock::Create(c, "else", fn);
      else
        enext = merge;

      ctx->builder->CreateCondBr(compile_expr(el.first), ethen, enext);
      ctx->builder->SetInsertPoint(ethen);
      compile_block(*el.second);
      if (!ctx->builder->GetInsertBlock()->getTerminator()) ctx->builder->CreateBr(merge);

      if (i + 1 == s.elif_branch.size() && s.else_branch) {
        ctx->builder->SetInsertPoint(enext);
        compile_block(*s.else_branch);
        if (!ctx->builder->GetInsertBlock()->getTerminator()) ctx->builder->CreateBr(merge);
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
    if (!ctx->builder->GetInsertBlock()->getTerminator()) ctx->builder->CreateBr(cond_bb);

    ctx->builder->SetInsertPoint(exit_bb);
    if (saved) env->set_loop(*saved);
    else
      env->loop_info = std::nullopt;
  }

  // ── expressions ──

  llvm::Value *IrEmitter::compile_expr(const ExprNode &expr_node) {
    auto &expr = expr_node.expr;
    auto &span = expr_node.span;
    return std::visit(
        overload{
            [&](const LiteralExpr &e) { return compile_literal(e); },
            [&](const Variable &e) -> llvm::Value * {
              auto v = env->lookup_var(e.name);
              if (!v.ptr || !v.alloca_ty) {
                auto *fn = ctx->module->getFunction(e.name);
                if (fn) return fn;
                diag.error(span, "Undefined variable: " + e.name).emit_to(diag);
                return llvm::Constant::getNullValue(i32(*ctx->llvm_ctx));
              }
              auto *val = ctx->builder->CreateLoad(v.alloca_ty, v.ptr, e.name);
              if (v.is_ref)
                val = ctx->builder->CreateLoad(v.value_ty, val);
              return val;
            },
            [&](const AssignExpr &e) { return compile_assignment(e); },
            [&](const BinaryExpr &e) { return compile_binary(e); },
            [&](const UnaryExpr &e) { return compile_unary(e); },
            [&](const CallExpr &e) { return compile_call(e, span); },
            [&](const MemberExpr &e) { return compile_member_access(*e.object, e.field); },
            [&](const IndexExpr &e) { return compile_index(*e.object, *e.index); },
            [&](const ListExpr &e) { return emit_list_literal(e.elements, span); },
        },
        expr
    );
  }

  llvm::Value *IrEmitter::compile_literal(const LiteralExpr &lit) {
    auto &c = *ctx->llvm_ctx;
    return std::visit(
        overload{
            [&](int64_t v) -> llvm::Value * {
              if (static_cast<int64_t>(static_cast<int32_t>(v)) != v)
                diag.error(Span{}, "Integer literal out of i32 range: " + std::to_string(v)).emit_to(diag);
              return llvm::ConstantInt::get(i32(c), static_cast<int32_t>(v));
            },
            [&](bool v) -> llvm::Value * { return llvm::ConstantInt::get(llvm::IntegerType::getInt1Ty(c), v); },
            [&](float v) -> llvm::Value * { return llvm::ConstantFP::get(llvm::Type::getFloatTy(c), v); },
            [&](char v) -> llvm::Value * { return llvm::ConstantInt::get(llvm::IntegerType::getInt8Ty(c), v); },
            [&](const std::string &s) -> llvm::Value * { return emit_string_literal(s); },
        },
        lit.lit
    );
  }

  llvm::Value *IrEmitter::compile_binary(const BinaryExpr &bin) {
    auto *l = compile_expr(*bin.lhs);
    auto *r = compile_expr(*bin.rhs);
    if (!l || !r) return nullptr;
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
            overload{
                [&](const Variable &var) -> llvm::Value * {
                  auto vi = env->lookup_var(var.name);
                  if (vi.is_ref)
                    return ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
                  return vi.ptr;
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
                    if (!v) return nullptr;
                    auto *a = ctx->builder->CreateAlloca(v->getType(), nullptr, "tmp");
                    ctx->builder->CreateStore(v, a);
                    return a;
                  }
                  return nullptr;
                },
                [](const auto &) -> llvm::Value * { return nullptr; },
            },
            u.expr->expr
        );
      }
      case UnaryOp::Deref: {
        // *&e  ->  e  (addr-of cancelled by deref)
        // TODO:
        if (auto *inner = std::get_if<UnaryExpr>(&u.expr->expr)) {
          if (inner->op == UnaryOp::AddrOf)
            return compile_expr(*inner->expr);
        }

        auto *ptr_val = compile_expr(*u.expr);
        if (!ptr_val) return nullptr;

        // walk deref chain to find root variable + depth
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
          // fallback: if we have a pointer value, try to deref based on deref_chain from any known var
          if (auto *var = std::get_if<Variable>(&u.expr->expr)) {
            auto vi = env->lookup_var(var->name);
            if (!vi.deref_chain.empty())
              elem_ty = vi.deref_chain[0];
          }
        }
        if (!elem_ty) return nullptr;
        return ctx->builder->CreateLoad(elem_ty, ptr_val);
      }
      case UnaryOp::Neg: {
        auto *v = compile_expr(*u.expr);
        if (!v) return nullptr;
        return v->getType()->isFloatTy()
                   ? ctx->builder->CreateFNeg(v)
                   : ctx->builder->CreateSub(llvm::ConstantInt::get(v->getType(), 0), v);
      }
      case UnaryOp::Not: {
        auto *v = compile_expr(*u.expr);
        if (!v) return nullptr;
        return ctx->builder->CreateIsNull(v);
      }
      default:
        return nullptr;
    }
  }

  llvm::Value *IrEmitter::compile_call(const CallExpr &call, Span span) {
    return std::visit(
        overload{
            [&](const Variable &callee) -> llvm::Value * {
              auto *fn = ctx->module->getFunction(callee.name);
              bool is_ctor = false;
              if (!fn) {
                fn = ctx->module->getFunction(callee.name + "_ctor");
                is_ctor = true;
              }
              if (fn) {
                ClassInfo *ctor_info = nullptr;
                if (is_ctor) {
                  auto it = ctx->class_registry.find(callee.name);
                  if (it != ctx->class_registry.end()) ctor_info = it->second.get();
                }

                auto args = compile_args(fn, call.args, 0, ctor_info);

                auto *call_inst = ctx->builder->CreateCall(fn, args);
                if (!is_ctor)
                  invalidate_owned_args(callee.name, call.args, 0);
                return call_inst;
              }

              if (env->has_var(callee.name)) {
                auto vi = env->lookup_var(callee.name);
                if (!vi.ptr) {
                  diag.error(span, "Undefined function: " + callee.name).emit_to(diag);
                  return nullptr;
                }
                auto *fn_ptr = ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
                auto *fn_ty = vi.func_ty;

                if (!fn_ty) {
                  diag.error(span, "Cannot determine function type for indirect call of '" + callee.name + "'").emit_to(diag);
                  return nullptr;
                }

                vector<llvm::Value *> args;
                args.reserve(call.args.size());
                for (auto &arg: call.args) {
                  auto *arg_val = compile_expr(*arg);
                  if (!arg_val) return nullptr;
                  args.push_back(arg_val);
                }
                return ctx->builder->CreateCall(fn_ty, fn_ptr, args);
              }

              diag.error(span, "Undefined function: " + callee.name).emit_to(diag);
              return nullptr;
            },
            [&](const MemberExpr &callee) -> llvm::Value * {
              auto *self = compile_expr(*callee.object);
              if (!self) return nullptr;

              string mangled = callee.field;
              for (auto &kv: ctx->class_registry) {
                auto it = kv.second->methods.find(callee.field);
                if (it != kv.second->methods.end()) {
                  mangled = it->second;
                  break;
                }
              }
              auto *fn = ctx->module->getFunction(mangled);
              if (!fn) {
                diag.error(span, "Undefined method: " + mangled).emit_to(diag);
                return nullptr;
              }

              auto args = compile_args(fn, call.args, 1);
              args.insert(args.begin(), self);

              auto *call_inst = ctx->builder->CreateCall(fn, args);

              if (auto *obj_var = std::get_if<Variable>(&callee.object->expr)) {
                auto *fn_sym = sema.get_symbol_table().resolve(mangled);
                auto *fn_data = fn_sym ? std::get_if<FunctionData>(&fn_sym->get_kind()) : nullptr;
                if (fn_data && fn_data->params.size() > 0 &&
                    std::get_if<ast::Type::Own>(&fn_data->params[0].data))
                  if (auto vi = env->lookup_var(obj_var->name); vi.ptr)
                    ctx->builder->CreateStore(llvm::UndefValue::get(vi.alloca_ty), vi.ptr);
              }
              invalidate_owned_args(mangled, call.args, 1);
              return call_inst;
            },
            [&](const auto &) -> llvm::Value * {
              diag.error(span, "Invalid call expression").emit_to(diag);
              return nullptr;
            },
        },
        call.callee->expr
    );
  }

  vector<llvm::Value *> IrEmitter::compile_args(llvm::Function *fn, const vector<uptr<ExprNode>> &args, size_t param_offset, const ClassInfo *ctor_info) {
    vector<llvm::Value *> out;
    size_t total = ctor_info ? fn->getFunctionType()->getNumParams()
                             : args.size();
    for (size_t i = 0; i + param_offset < fn->getFunctionType()->getNumParams() && i < total; ++i) {
      if (ctor_info && i >= args.size()) {
        if (i < ctor_info->field_defaults.size() && ctor_info->field_defaults[i])
          out.push_back(compile_expr(**ctor_info->field_defaults[i]));
        else
          out.push_back(zero_const(fn->getFunctionType()->getParamType(static_cast<unsigned>(i + param_offset))));
        continue;
      }
      auto *arg_val = compile_expr(*args[i]);
      auto *param_ty = fn->getFunctionType()->getParamType(static_cast<unsigned>(i + param_offset));
      if (param_ty->isPointerTy() && arg_val && !arg_val->getType()->isPointerTy())
        if (auto *var = std::get_if<Variable>(&args[i]->expr))
          if (auto vi = env->lookup_var(var->name); vi.ptr)
            arg_val = vi.ptr;
      out.push_back(arg_val);
    }
    return out;
  }

  void IrEmitter::invalidate_owned_args(const string &fn_name, const vector<uptr<ExprNode>> &args, size_t param_offset) {
    auto *fn_sym = sema.get_symbol_table().resolve(fn_name);
    auto *fn_data = fn_sym ? std::get_if<FunctionData>(&fn_sym->get_kind()) : nullptr;
    if (!fn_data) return;
    for (size_t i = 0; i < args.size() && (i + param_offset) < fn_data->params.size(); ++i) {
      if (!std::get_if<ast::Type::Own>(&fn_data->params[i + param_offset].data)) continue;
      if (auto *var = std::get_if<Variable>(&args[i]->expr))
        if (auto vi = env->lookup_var(var->name); vi.ptr)
          ctx->builder->CreateStore(llvm::UndefValue::get(vi.alloca_ty), vi.ptr);
    }
  }

  llvm::Value *IrEmitter::compile_assignment(const AssignExpr &a) {
    auto *val = compile_expr(*a.value);
    if (!val) return nullptr;
    std::visit(
        overload{
            [&](const Variable &t) {
              auto vi = env->lookup_var(t.name);
              if (!vi.ptr) return;
              if (vi.is_ref) {
                auto *dst = ctx->builder->CreateLoad(vi.alloca_ty, vi.ptr);
                ctx->builder->CreateStore(val, dst);
              } else {
                ctx->builder->CreateStore(val, vi.ptr);
              }
              if (vi.is_own)
                if (auto *src_var = std::get_if<Variable>(&a.value->expr))
                  if (auto sv = env->lookup_var(src_var->name); sv.ptr)
                    ctx->builder->CreateStore(llvm::UndefValue::get(sv.alloca_ty), sv.ptr);
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
                if (ptr) ctx->builder->CreateStore(val, ptr);
              }
            },
            [](const auto &) {},
        },
        a.target->expr
    );
    return val;
  }

  // ── member / index access ──
  llvm::Value *IrEmitter::compile_member_access(const ExprNode &obj, const string &field) {
    auto *obj_val = compile_expr(obj);
    if (!obj_val) return nullptr;
    for (auto &kv: ctx->class_registry) {
      auto it = kv.second->field_indices.find(field);
      if (it == kv.second->field_indices.end()) continue;
      auto *st = kv.second->struct_ty;
      auto *fp = ctx->builder->CreateStructGEP(st, obj_val, it->second);
      return ctx->builder->CreateLoad(st->getElementType(it->second), fp);
    }
    return nullptr;
  }

  llvm::Value *IrEmitter::compile_member_ptr(const ExprNode &e) {
    return std::visit(
        overload{
            [&](const MemberExpr &m) -> llvm::Value * {
              auto *ov = compile_expr(*m.object);
              if (!ov) return nullptr;
              for (auto &kv: ctx->class_registry) {
                auto it = kv.second->field_indices.find(m.field);
                if (it != kv.second->field_indices.end())
                  return ctx->builder->CreateStructGEP(kv.second->struct_ty, ov, it->second);
              }
              return nullptr;
            },
            [](const auto &) -> llvm::Value * { return nullptr; },
        },
        e.expr
    );
  }

  llvm::Value *IrEmitter::compile_index(const ExprNode &obj, const ExprNode &idx) {
    auto *ov = compile_expr(obj);
    auto *iv = compile_expr(idx);
    if (!ov || !iv) return nullptr;
    auto &c = *ctx->llvm_ctx;
    if (!iv->getType()->isIntegerTy(64))
      iv = ctx->builder->CreateZExt(iv, i64(c));

    for (auto &kv: ctx->list_types) {
      if (ov->getType() != kv.second->struct_ty) continue;
      auto *st = kv.second->struct_ty;
      auto *et = kv.second->elem_ty;

      auto *tmp = ctx->builder->CreateAlloca(st, nullptr, "list.tmp");
      ctx->builder->CreateStore(ov, tmp);

      auto *len = ctx->builder->CreateLoad(i64(c), ctx->builder->CreateStructGEP(st, tmp, 0));
      emit_bounds_check(iv, len, idx.span);
      auto *data = ctx->builder->CreateLoad(ptr_ty(c), ctx->builder->CreateStructGEP(st, tmp, 2));
      return ctx->builder->CreateLoad(et, ctx->builder->CreateGEP(et, data, iv));
    }
    return nullptr;
  }

  llvm::Value *IrEmitter::compile_index_ptr(const ExprNode &e) {
    return std::visit(
        overload{
            [&](const IndexExpr &ie) -> llvm::Value * {
              auto *ov = compile_expr(*ie.object);
              auto *iv = compile_expr(*ie.index);
              if (!ov || !iv) return nullptr;
              auto &c = *ctx->llvm_ctx;
              if (!iv->getType()->isIntegerTy(64))
                iv = ctx->builder->CreateZExt(iv, i64(c));

              for (auto &kv: ctx->list_types) {
                if (ov->getType() != kv.second->struct_ty) continue;
                auto *st = kv.second->struct_ty;
                auto *et = kv.second->elem_ty;

                auto *tmp = ctx->builder->CreateAlloca(st, nullptr, "list.tmp");
                ctx->builder->CreateStore(ov, tmp);

                auto *len = ctx->builder->CreateLoad(i64(c), ctx->builder->CreateStructGEP(st, tmp, 0));
                emit_bounds_check(iv, len, ie.index->span);
                auto *data = ctx->builder->CreateLoad(ptr_ty(c), ctx->builder->CreateStructGEP(st, tmp, 2));
                return ctx->builder->CreateGEP(et, data, iv);
              }
              if (ov->getType()->isPointerTy())
                return ctx->builder->CreateGEP(llvm::IntegerType::getInt8Ty(c), ov, iv);
              return nullptr;
            },
            [](const auto &) -> llvm::Value * { return nullptr; },
        },
        e.expr
    );
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
    auto *trap = llvm::Intrinsic::getOrInsertDeclaration(ctx->module.get(), llvm::Intrinsic::trap);
    ctx->builder->CreateCall(trap);
    ctx->builder->CreateUnreachable();
    ctx->builder->SetInsertPoint(ok);
  }

  // ── list / string ──

  llvm::Value *IrEmitter::emit_list_literal(const vector<uptr<ExprNode>> &elements, Span span) {
    auto &c = *ctx->llvm_ctx;
    vector<llvm::Value *> vals;
    vals.reserve(elements.size());
    llvm::Type *et = i32(c);
    for (size_t i = 0; i < elements.size(); ++i) {
      auto *v = compile_expr(*elements[i]);
      if (v) {
        vals.push_back(v);
        if (i == 0) et = v->getType();
      }
    }
    if (vals.size() != elements.size() && vals.empty()) {
      vals.resize(elements.size(), llvm::Constant::getNullValue(et));
    }

    auto key = list_type_key(et);
    auto &entry = ctx->list_types[key];
    if (!entry) {
      entry = std::make_unique<ListType>();
      entry->struct_ty = llvm::StructType::create(c, key);
      entry->elem_ty = et;
      entry->struct_ty->setBody({i64(c), i64(c), ptr_ty(c)});
    }

    auto *a = ctx->builder->CreateAlloca(entry->struct_ty, nullptr, "list");
    size_t n = vals.size();
    auto *nv = llvm::ConstantInt::get(i64(c), n);
    ctx->builder->CreateStore(nv, ctx->builder->CreateStructGEP(entry->struct_ty, a, 0));
    ctx->builder->CreateStore(nv, ctx->builder->CreateStructGEP(entry->struct_ty, a, 1));

    auto *df = ctx->builder->CreateStructGEP(entry->struct_ty, a, 2);
    if (n == 0) {
      ctx->builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty(c)), df);
      return ctx->builder->CreateLoad(entry->struct_ty, a);
    }

    auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
    auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
        llvm::ConstantExpr::getSizeOf(et), i64(c)
    );
    auto *total = ctx->builder->CreateMul(nv, elem_sz);
    auto *data = ctx->builder->CreateCall(malloc_fn, {total}, "listdata");
    ctx->builder->CreateStore(data, df);

    for (size_t i = 0; i < n; ++i)
      ctx->builder->CreateStore(
          vals[i],
          ctx->builder->CreateGEP(et, data, llvm::ConstantInt::get(i64(c), i))
      );

    return ctx->builder->CreateLoad(entry->struct_ty, a);
  }

  void IrEmitter::emit_list_with_init(llvm::Value *a, const vector<uptr<ExprNode>> &elements, Span span) {
    llvm::Type *et = i32(*ctx->llvm_ctx);
    if (!elements.empty()) {
      auto *first = compile_expr(*elements[0]);
      if (first) et = first->getType();
    }
    emit_list_with_init_fields(a, elements, et, span);
  }

  void IrEmitter::emit_list_with_init_fields(llvm::Value *a, const vector<uptr<ExprNode>> &elements, llvm::Type *elem_ty, Span) {
    auto &c = *ctx->llvm_ctx;
    llvm::StructType *st = nullptr;
    for (auto &kv: ctx->list_types)
      if (kv.second->elem_ty == elem_ty) {
        st = kv.second->struct_ty;
        break;
      }
    if (!st) return;

    size_t n = elements.size();
    auto *nv = llvm::ConstantInt::get(i64(c), n);
    ctx->builder->CreateStore(nv, ctx->builder->CreateStructGEP(st, a, 0));
    ctx->builder->CreateStore(nv, ctx->builder->CreateStructGEP(st, a, 1));

    auto *df = ctx->builder->CreateStructGEP(st, a, 2);
    if (n == 0) {
      ctx->builder->CreateStore(llvm::ConstantPointerNull::get(ptr_ty(c)), df);
      return;
    }

    auto *malloc_fn = declare_runtime_func("malloc", ptr_ty(c), {i64(c)});
    auto *elem_sz = llvm::ConstantExpr::getTruncOrBitCast(
        llvm::ConstantExpr::getSizeOf(elem_ty), i64(c)
    );
    auto *total = ctx->builder->CreateMul(nv, elem_sz);
    auto *buf = ctx->builder->CreateCall(malloc_fn, {total}, "listdata");
    ctx->builder->CreateStore(buf, df);

    for (size_t i = 0; i < n; ++i)
      ctx->builder->CreateStore(
          compile_expr(*elements[i]),
          ctx->builder->CreateGEP(elem_ty, buf, llvm::ConstantInt::get(i64(c), i))
      );
  }

  llvm::Value *IrEmitter::emit_string_literal(const string &s) {
    return ctx->builder->CreateGlobalString(s);
  }

  // ── module i/o ──

  void IrEmitter::dump_module(std::ostream &os) {
    std::string s;
    llvm::raw_string_ostream rso(s);
    ctx->module->print(rso, nullptr);
    os << rso.str();
  }

  IrEmitter::ModuleHandle IrEmitter::release_module() {
    return {std::move(ctx->llvm_ctx), std::move(ctx->module)};
  }

}// namespace cat::ir
