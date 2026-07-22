#include "mlir_emitter.h"
#include "expr.h"
#include "item.h"
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlowOps.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/Attributes.h"
#include "mlir/IR/OperationSupport.h"
#include "mlir/IR/SymbolTable.h"
#include "stmt.h"

namespace cat::mmlir {

  template<class... Ts>
  struct overload : Ts... {
    using Ts::operator()...;
  };

  namespace {
    inline mlir::Type i32_ty(mlir::MLIRContext &c) { return mlir::IntegerType::get(&c, 32); }
    inline mlir::Type f32_ty(mlir::MLIRContext &c) { return mlir::Float32Type::get(&c); }
    inline mlir::Type i1_ty(mlir::MLIRContext &c) { return mlir::IntegerType::get(&c, 1); }
    inline mlir::Type i8_ty(mlir::MLIRContext &c) { return mlir::IntegerType::get(&c, 8); }
    inline mlir::MemRefType memref_ty(mlir::Type elem) {
      return mlir::MemRefType::get({}, elem);
    }
  }// namespace

  MlirEmitter::MlirEmitter(const string & /*name*/, error::DiagCtxt &diag, semantics::SemaCtxt &sema_ctx)
      : ctx(std::make_unique<CodegenCtxt>()),
        env(std::make_shared<Env>()), diag(diag), sema(sema_ctx) {}

  // ── type helpers ──

  mlir::Type MlirEmitter::mlir_type(const ast::Type &ast_type) {
    return std::visit(
        overload{
            [&](const ast::Type::Int &) { return i32_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Float &) { return f32_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Bool &) { return i1_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Char &) { return i8_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Ptr &) { return i32_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Ref &) { return i32_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Own &) { return i32_ty(ctx->mlir_ctx); },
            [&](const ast::Type::List &) { return i32_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Class &) { return i32_ty(ctx->mlir_ctx); },
            [&](const ast::Type::Void &) -> mlir::Type { return {}; },
            [&](const ast::Type::Str &) { return i32_ty(ctx->mlir_ctx); },
        },
        ast_type.data
    );
  }

  mlir::Type MlirEmitter::infer_lit_type(const Expr &expr) {
    return std::visit(
        overload{
            [&](const LiteralExpr &e) -> mlir::Type {
              return std::visit(
                  overload{
                      [&](int64_t) { return i32_ty(ctx->mlir_ctx); },
                      [&](bool) { return i1_ty(ctx->mlir_ctx); },
                      [&](float) { return f32_ty(ctx->mlir_ctx); },
                      [&](char) { return i8_ty(ctx->mlir_ctx); },
                      [&](const std::string &) { return i32_ty(ctx->mlir_ctx); },
                  },
                  e.lit
              );
            },
            [](const auto &) -> mlir::Type { return {}; },
        },
        expr
    );
  }

  // ── top-level ──

  void MlirEmitter::compile(const Program &program) {
    for (auto &item: program.items) {
      std::visit(
          overload{
              [&](const FunctionDef &f) { compile_function(f, item.span); },
              [](const auto &) {},
          },
          item.item
      );
    }
  }

  // ── functions ──

  void MlirEmitter::compile_function(const FunctionDef &func, Span span) {
    auto loc = ctx->builder->getUnknownLoc();
    auto &hdr = func.function_header;

    ctx->builder->setInsertionPointToEnd(ctx->module.getBody());

    llvm::SmallVector<mlir::Type> param_tys;
    for (auto &p: hdr.params) {
      auto t = mlir_type(p.ty);
      if (!t) t = i32_ty(ctx->mlir_ctx);
      param_tys.push_back(t);
    }

    llvm::SmallVector<mlir::Type> result_tys;
    if (hdr.return_type) {
      auto ret_ty = mlir_type(*hdr.return_type);
      if (ret_ty) result_tys.push_back(ret_ty);
    }

    auto fn_type = ctx->builder->getFunctionType(param_tys, result_tys);
    auto fn = ctx->builder->create<mlir::func::FuncOp>(loc, hdr.name, fn_type);
    auto &entry_block = *fn.addEntryBlock();
    ctx->builder->setInsertionPointToStart(&entry_block);

    EnvGuard guard(*this, std::make_shared<Env>(env));

    for (size_t i = 0; i < hdr.params.size(); ++i) {
      auto &p = hdr.params[i];
      auto arg = entry_block.getArgument(static_cast<unsigned>(i));
      auto elem_ty = param_tys[i];
      auto alloc = ctx->builder->create<mlir::memref::AllocaOp>(loc, memref_ty(elem_ty));
      ctx->builder->create<mlir::memref::StoreOp>(loc, arg, alloc.getResult(), mlir::ValueRange{});
      env->declare_var(p.name, alloc.getResult(), elem_ty);
    }

    compile_block(func.body);

    if (!entry_block.empty() && !entry_block.back().hasTrait<mlir::OpTrait::IsTerminator>()) {
      if (result_tys.empty()) {
        ctx->builder->create<mlir::func::ReturnOp>(loc);
      } else {
        auto ret_ty = result_tys[0];
        auto zero = ctx->builder->create<mlir::arith::ConstantOp>(loc, ret_ty, ctx->builder->getIntegerAttr(ret_ty, 0));
        ctx->builder->create<mlir::func::ReturnOp>(loc, zero.getResult());
      }
    }
  }

  // ── statements ──

  void MlirEmitter::compile_block(const Block &block) {
    for (auto &s: block.stmts) {
      auto *current_block = ctx->builder->getBlock();
      if (!current_block->empty() &&
          current_block->back().hasTrait<mlir::OpTrait::IsTerminator>())
        break;
      compile_stmt(s);
    }
  }

  void MlirEmitter::compile_stmt(const StmtNode &sn) {
    auto loc = ctx->builder->getUnknownLoc();
    std::visit(
        overload{
            [&](const VarDefStmt &s) {
              mlir::Value init_val;
              mlir::Type elem_ty;

              if (s.init) {
                init_val = compile_expr(*s.init);
                elem_ty = init_val.getType();
              } else if (s.ty) {
                elem_ty = mlir_type(*s.ty);
                if (!elem_ty) elem_ty = i32_ty(ctx->mlir_ctx);
              } else {
                elem_ty = i32_ty(ctx->mlir_ctx);
              }

              auto alloc = ctx->builder->create<mlir::memref::AllocaOp>(loc, memref_ty(elem_ty));
              auto alloca = alloc.getResult();

              if (init_val) {
                ctx->builder->create<mlir::memref::StoreOp>(loc, init_val, alloca, mlir::ValueRange{});
              } else {
                auto zero = ctx->builder->create<mlir::arith::ConstantOp>(
                    loc, elem_ty, ctx->builder->getIntegerAttr(elem_ty, 0)
                );
                ctx->builder->create<mlir::memref::StoreOp>(loc, zero.getResult(), alloca, mlir::ValueRange{});
              }

              env->declare_var(s.name, alloca, elem_ty);
            },
            [&](const IfStmt &s) { compile_if(s); },
            [&](const LoopStmt &s) { compile_while(s); },
            [&](const ExprStmt &s) { (void) compile_expr(s.expr); },
            [&](const ReturnStmt &s) {
              if (s.expr.has_value()) {
                auto val = compile_expr(*s.expr);
                ctx->builder->create<mlir::func::ReturnOp>(loc, val);
              } else {
                ctx->builder->create<mlir::func::ReturnOp>(loc);
              }
            },
            [&](const BreakStmt &) {
              if (auto lp = env->lookup_loop())
                ctx->builder->create<mlir::cf::BranchOp>(loc, lp->exit_block);
            },
            [&](const ContinueStmt &) {
              if (auto lp = env->lookup_loop())
                ctx->builder->create<mlir::cf::BranchOp>(loc, lp->cond_block);
            },
            [&](const BlockStmt &s) {
              EnvGuard g(*this, std::make_shared<Env>(env));
              compile_block(*s.block);
            },
        },
        sn.stmt
    );
  }

  // ── control flow ──

  void MlirEmitter::compile_if(const IfStmt &s) {
    auto loc = ctx->builder->getUnknownLoc();
    auto *region = ctx->builder->getBlock()->getParent();

    auto cond = compile_expr(s.condition);

    auto *then_block = new mlir::Block();
    auto *merge_block = new mlir::Block();
    mlir::Block *else_block = s.else_branch ? new mlir::Block() : merge_block;

    region->getBlocks().insertAfter(ctx->builder->getBlock()->getIterator(), then_block);
    if (s.else_branch)
      region->getBlocks().insertAfter(then_block->getIterator(), else_block);
    region->getBlocks().insertAfter(
        s.else_branch ? else_block->getIterator() : then_block->getIterator(),
        merge_block
    );

    ctx->builder->create<mlir::cf::CondBranchOp>(loc, cond, then_block, else_block);

    ctx->builder->setInsertionPointToStart(then_block);
    compile_block(*s.then_branch);
    if (!ctx->builder->getBlock()->back().hasTrait<mlir::OpTrait::IsTerminator>())
      ctx->builder->create<mlir::cf::BranchOp>(loc, merge_block);

    if (s.else_branch) {
      ctx->builder->setInsertionPointToStart(else_block);
      compile_block(*s.else_branch);
      if (!ctx->builder->getBlock()->back().hasTrait<mlir::OpTrait::IsTerminator>())
        ctx->builder->create<mlir::cf::BranchOp>(loc, merge_block);
    }

    ctx->builder->setInsertionPointToStart(merge_block);
  }

  void MlirEmitter::compile_while(const LoopStmt &s) {
    auto loc = ctx->builder->getUnknownLoc();
    auto *region = ctx->builder->getBlock()->getParent();

    auto *cond_block = new mlir::Block();
    auto *body_block = new mlir::Block();
    auto *exit_block = new mlir::Block();

    region->getBlocks().insertAfter(ctx->builder->getBlock()->getIterator(), cond_block);
    region->getBlocks().insertAfter(cond_block->getIterator(), body_block);
    region->getBlocks().insertAfter(body_block->getIterator(), exit_block);

    ctx->builder->create<mlir::cf::BranchOp>(loc, cond_block);

    auto saved = env->lookup_loop();
    env->set_loop({cond_block, exit_block});

    ctx->builder->setInsertionPointToStart(cond_block);
    auto cond = compile_expr(s.condition);
    ctx->builder->create<mlir::cf::CondBranchOp>(loc, cond, body_block, exit_block);

    ctx->builder->setInsertionPointToStart(body_block);
    compile_block(*s.body);
    if (!ctx->builder->getBlock()->back().hasTrait<mlir::OpTrait::IsTerminator>())
      ctx->builder->create<mlir::cf::BranchOp>(loc, cond_block);

    ctx->builder->setInsertionPointToStart(exit_block);
    if (saved) env->set_loop(*saved);
    else
      env->loop_info = std::nullopt;
  }

  // ── expressions ──

  mlir::Value MlirEmitter::compile_expr(const ExprNode &expr_node) {
    auto loc = ctx->builder->getUnknownLoc();
    auto &expr = expr_node.expr;
    auto &span = expr_node.span;

    return std::visit(
        overload{
            [&](const LiteralExpr &e) -> mlir::Value {
              return std::visit(
                  overload{
                      [&](int64_t v) -> mlir::Value {
                        auto ty = i32_ty(ctx->mlir_ctx);
                        return ctx->builder->create<mlir::arith::ConstantOp>(loc, ty, ctx->builder->getI32IntegerAttr(static_cast<int32_t>(v))).getResult();
                      },
                      [&](bool v) -> mlir::Value {
                        auto ty = i1_ty(ctx->mlir_ctx);
                        return ctx->builder->create<mlir::arith::ConstantOp>(loc, ty, ctx->builder->getBoolAttr(v)).getResult();
                      },
                      [&](float v) -> mlir::Value {
                        auto ty = f32_ty(ctx->mlir_ctx);
                        auto val = mlir::FloatAttr::get(ty, v);
                        return ctx->builder->create<mlir::arith::ConstantOp>(loc, ty, val).getResult();
                      },
                      [&](char v) -> mlir::Value {
                        auto ty = i8_ty(ctx->mlir_ctx);
                        return ctx->builder->create<mlir::arith::ConstantOp>(loc, ty, ctx->builder->getIntegerAttr(ty, static_cast<int64_t>(v))).getResult();
                      },
                      [&](const std::string &) -> mlir::Value {
                        return {};
                      },
                  },
                  e.lit
              );
            },
            [&](const Variable &e) -> mlir::Value {
              auto vi = env->lookup_var(e.name);
              if (!vi.memref) {
                diag.error(span, "Undefined variable: " + e.name).emit_to(diag);
                return {};
              }
              auto load = ctx->builder->create<mlir::memref::LoadOp>(loc, vi.memref, mlir::ValueRange{});
              return load.getResult();
            },
            [&](const AssignExpr &e) -> mlir::Value {
              auto val = compile_expr(*e.value);
              if (!val) return {};
              std::visit(
                  overload{
                      [&](const Variable &t) {
                        auto vi = env->lookup_var(t.name);
                        if (vi.memref)
                          ctx->builder->create<mlir::memref::StoreOp>(loc, val, vi.memref, mlir::ValueRange{});
                      },
                      [](const auto &) {},
                  },
                  e.target->expr
              );
              return val;
            },
            [&](const BinaryExpr &e) -> mlir::Value {
              auto lhs = compile_expr(*e.lhs);
              auto rhs = compile_expr(*e.rhs);
              if (!lhs || !rhs) return {};
              bool is_float = lhs.getType().isF32();
              switch (e.op) {
                case BinaryOp::Add:
                  return is_float
                             ? ctx->builder->create<mlir::arith::AddFOp>(loc, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::AddIOp>(loc, lhs, rhs).getResult();
                case BinaryOp::Sub:
                  return is_float
                             ? ctx->builder->create<mlir::arith::SubFOp>(loc, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::SubIOp>(loc, lhs, rhs).getResult();
                case BinaryOp::Mul:
                  return is_float
                             ? ctx->builder->create<mlir::arith::MulFOp>(loc, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::MulIOp>(loc, lhs, rhs).getResult();
                case BinaryOp::Div:
                  return is_float
                             ? ctx->builder->create<mlir::arith::DivFOp>(loc, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::DivSIOp>(loc, lhs, rhs).getResult();
                case BinaryOp::Eq:
                  return is_float
                             ? ctx->builder->create<mlir::arith::CmpFOp>(loc, mlir::arith::CmpFPredicate::OEQ, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, lhs, rhs).getResult();
                case BinaryOp::NotEq:
                  return is_float
                             ? ctx->builder->create<mlir::arith::CmpFOp>(loc, mlir::arith::CmpFPredicate::ONE, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::ne, lhs, rhs).getResult();
                case BinaryOp::Lt:
                  return is_float
                             ? ctx->builder->create<mlir::arith::CmpFOp>(loc, mlir::arith::CmpFPredicate::OLT, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::slt, lhs, rhs).getResult();
                case BinaryOp::Gt:
                  return is_float
                             ? ctx->builder->create<mlir::arith::CmpFOp>(loc, mlir::arith::CmpFPredicate::OGT, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::sgt, lhs, rhs).getResult();
                case BinaryOp::Le:
                  return is_float
                             ? ctx->builder->create<mlir::arith::CmpFOp>(loc, mlir::arith::CmpFPredicate::OLE, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::sle, lhs, rhs).getResult();
                case BinaryOp::Ge:
                  return is_float
                             ? ctx->builder->create<mlir::arith::CmpFOp>(loc, mlir::arith::CmpFPredicate::OGE, lhs, rhs).getResult()
                             : ctx->builder->create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::sge, lhs, rhs).getResult();
                case BinaryOp::And:
                  return ctx->builder->create<mlir::arith::AndIOp>(loc, lhs, rhs).getResult();
                case BinaryOp::Or:
                  return ctx->builder->create<mlir::arith::OrIOp>(loc, lhs, rhs).getResult();
              }
              return {};
            },
            [&](const UnaryExpr &e) -> mlir::Value {
              auto val = compile_expr(*e.expr);
              if (!val) return {};
              switch (e.op) {
                case UnaryOp::Neg:
                  if (val.getType().isF32())
                    return ctx->builder->create<mlir::arith::NegFOp>(loc, val).getResult();
                  else {
                    auto ty = val.getType();
                    auto zero = ctx->builder->create<mlir::arith::ConstantOp>(loc, ty, ctx->builder->getIntegerAttr(ty, 0));
                    return ctx->builder->create<mlir::arith::SubIOp>(loc, zero.getResult(), val).getResult();
                  }
                case UnaryOp::Not: {
                  auto ty = val.getType();
                  auto zero = ctx->builder->create<mlir::arith::ConstantOp>(loc, ty, ctx->builder->getIntegerAttr(ty, 0));
                  return ctx->builder->create<mlir::arith::CmpIOp>(loc, mlir::arith::CmpIPredicate::eq, val, zero.getResult()).getResult();
                }
                default:
                  return {};
              }
            },
            [&](const CallExpr &e) -> mlir::Value {
              return std::visit(
                  overload{
                      [&](const Variable &callee) -> mlir::Value {
                        llvm::SmallVector<mlir::Value> args;
                        for (auto &a: e.args) {
                          auto v = compile_expr(*a);
                          if (v) args.push_back(v);
                        }

                        mlir::TypeRange result_tys;
                        auto callee_fn = mlir::SymbolTable::lookupNearestSymbolFrom<mlir::func::FuncOp>(
                            ctx->module, mlir::StringAttr::get(&ctx->mlir_ctx, callee.name)
                        );
                        if (callee_fn) {
                          result_tys = callee_fn.getFunctionType().getResults();
                        }

                        auto call = ctx->builder->create<mlir::func::CallOp>(loc, callee.name, result_tys, args);
                        if (result_tys.empty())
                          return mlir::Value{};
                        return call.getResult(0);
                      },
                      [&](const auto &) -> mlir::Value {
                        diag.error(span, "Unsupported call target").emit_to(diag);
                        return {};
                      },
                  },
                  e.callee->expr
              );
            },
            [&](const ListExpr &) -> mlir::Value {
              return {};
            },
            [&](const MemberExpr &) -> mlir::Value {
              return {};
            },
            [&](const IndexExpr &) -> mlir::Value {
              return {};
            },
        },
        expr
    );
  }

  // ── output ──

  void MlirEmitter::dump_module(std::ostream &os) {
    if (!ctx || !ctx->module) return;

    std::string str;
    llvm::raw_string_ostream rso(str);
    auto flags = mlir::OpPrintingFlags().assumeVerified().printGenericOpForm();
    ctx->module.print(rso, flags);
    rso << "\n";
    os << str;
  }

}// namespace cat::mmlir
