#pragma once
#include "codegen_ctx.h"
#include "common.h"
#include "diag.h"
#include "env.h"
#include "expr.h"
#include "item.h"
#include "sema_ctx.h"
#include "stmt.h"
#include "mlir/IR/BuiltinTypes.h"

namespace cat::mmlir {

  class MlirEmitter {
public:
    MlirEmitter(const string &name, error::DiagCtxt &diag, semantics::SemaCtxt &sema_ctx);

    void compile(const Program &program);
    void compile_function(const FunctionDef &func, Span span);
    void compile_block(const Block &block);
    void compile_stmt(const StmtNode &stmt_node);
    mlir::Value compile_expr(const ExprNode &expr);

    void dump_module(std::ostream &os);

    class EnvGuard {
  public:
        EnvGuard(MlirEmitter &e, sptr<Env> enclosing_env)
            : emitter(e), previous_env(e.env) {
          emitter.env = std::move(enclosing_env);
        }
        ~EnvGuard() { emitter.env = std::move(previous_env); }

  private:
        MlirEmitter &emitter;
        sptr<Env> previous_env;
    };

private:
    mlir::Type mlir_type(const ast::Type &ast_type);
    mlir::Type infer_lit_type(const Expr &expr);
    void compile_if(const IfStmt &s);
    void compile_while(const LoopStmt &s);

    uptr<CodegenCtxt> ctx;
    sptr<Env> env;
    error::DiagCtxt &diag;
    semantics::SemaCtxt &sema;
  };

} // namespace cat::mmlir
