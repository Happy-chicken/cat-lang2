#include "jit.h"
#include "ir_emitter.h"
#include <llvm-20/llvm/ExecutionEngine/Orc/Core.h>
#include <llvm-20/llvm/ExecutionEngine/Orc/ExecutionUtils.h>
#include <llvm-20/llvm/ExecutionEngine/Orc/ThreadSafeModule.h>
#include <llvm-20/llvm/Support/Error.h>
#include <llvm-20/llvm/Support/TargetSelect.h>
#include <llvm-20/llvm/TargetParser/Host.h>

namespace cat::jit {

JIT::JIT(error::DiagCtxt &diag) : diag(diag) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto expected = llvm::orc::LLJITBuilder().create();
  if (!expected) {
    llvm::handleAllErrors(
        expected.takeError(), [&](const llvm::ErrorInfoBase &e) {
          diag.error(Span{}, string("JIT init: ") + e.message()).emit_to(diag);
        });
    return;
  }
  engine = std::move(*expected);

  auto &jd = engine->getMainJITDylib();
  auto gen = llvm::orc::DynamicLibrarySearchGenerator::GetForCurrentProcess(
      engine->getDataLayout().getGlobalPrefix());
  if (gen)
    jd.addGenerator(std::move(*gen));
  else
    llvm::consumeError(gen.takeError());
}

void JIT::add_symbol(const string &name, void *fn_ptr) {
  if (!engine)
    return;
  auto &jd = engine->getMainJITDylib();
  llvm::orc::MangleAndInterner mangle(jd.getExecutionSession(),
                                      engine->getDataLayout());
  llvm::orc::SymbolMap syms;
  syms[mangle(name)] = {llvm::orc::ExecutorAddr::fromPtr(fn_ptr),
                        llvm::JITSymbolFlags::Callable |
                            llvm::JITSymbolFlags::Exported};
  llvm::cantFail(jd.define(llvm::orc::absoluteSymbols(std::move(syms))));
}

void JIT::add_module(ir::IrEmitter &emitter) {
  if (!engine)
    return;
  auto handle = emitter.release_module();
  if (!handle.mod) {
    diag.error(Span{}, "JIT: emitter has no module").emit_to(diag);
    return;
  }

  auto tsm =
      llvm::orc::ThreadSafeModule(std::move(handle.mod), std::move(handle.ctx));
  if (auto err = engine->addIRModule(std::move(tsm))) {
    llvm::handleAllErrors(std::move(err), [&](const llvm::ErrorInfoBase &e) {
      diag.error(Span{}, string("JIT add module: ") + e.message())
          .emit_to(diag);
    });
  }
}

void *JIT::lookup(const string &name) {
  if (!engine)
    return nullptr;
  auto sym = engine->lookup(name);
  if (!sym) {
    llvm::handleAllErrors(sym.takeError(), [&](const llvm::ErrorInfoBase &e) {
      diag.warn(string("JIT lookup '") + name + "': " + e.message())
          .emit_to(diag);
    });
    return nullptr;
  }
  return reinterpret_cast<void *>(sym->getValue());
}

int JIT::run() {
  auto *fn = lookup("main");
  if (!fn)
    return -1;
  return reinterpret_cast<int (*)()>(fn)();
}

} // namespace cat::jit
