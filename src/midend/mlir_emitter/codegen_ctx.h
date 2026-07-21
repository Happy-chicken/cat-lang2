#pragma once
#include "mlir/Dialect/Arith/IR/Arith.h"
#include "mlir/Dialect/ControlFlow/IR/ControlFlow.h"
#include "mlir/Dialect/Func/IR/FuncOps.h"
#include "mlir/Dialect/MemRef/IR/MemRef.h"
#include "mlir/IR/BuiltinOps.h"
#include "mlir/IR/DialectRegistry.h"
#include "mlir/IR/MLIRContext.h"
#include "mlir/IR/Builders.h"
#include "mlir/IR/Value.h"
#include "common.h"

namespace cat::mmlir {

  inline mlir::DialectRegistry make_mlir_registry() {
    mlir::DialectRegistry reg;
    reg.insert<mlir::arith::ArithDialect>();
    reg.insert<mlir::func::FuncDialect>();
    reg.insert<mlir::cf::ControlFlowDialect>();
    reg.insert<mlir::memref::MemRefDialect>();
    return reg;
  }

  class CodegenCtxt {
public:
    CodegenCtxt()
        : mlir_ctx(make_mlir_registry()),
          builder(std::make_unique<mlir::OpBuilder>(&mlir_ctx)) {
      mlir_ctx.getOrLoadDialect<mlir::arith::ArithDialect>();
      mlir_ctx.getOrLoadDialect<mlir::func::FuncDialect>();
      mlir_ctx.getOrLoadDialect<mlir::cf::ControlFlowDialect>();
      mlir_ctx.getOrLoadDialect<mlir::memref::MemRefDialect>();
      module = mlir::ModuleOp::create(builder->getUnknownLoc());
      builder->setInsertionPointToStart(module.getBody());
    }

    mlir::MLIRContext mlir_ctx;
    uptr<mlir::OpBuilder> builder;
    mlir::ModuleOp module;
  };

} // namespace cat::mmlir
