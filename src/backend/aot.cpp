#include "aot.h"
#include <llvm-20/llvm/IR/LegacyPassManager.h>
#include <llvm-20/llvm/IR/Module.h>
#include <llvm-20/llvm/MC/TargetRegistry.h>
#include <llvm-20/llvm/Support/CodeGen.h>
#include <llvm-20/llvm/Support/TargetSelect.h>
#include <llvm-20/llvm/Support/raw_ostream.h>
#include <llvm-20/llvm/Target/TargetMachine.h>
#include <llvm-20/llvm/Target/TargetOptions.h>
#include <llvm-20/llvm/TargetParser/Host.h>
#include <cstdlib>
#include <iostream>
#include <sys/wait.h>

namespace cat::aot {

bool AOTCompiler::compile(llvm::Module &module,
                           const std::string &obj_path,
                           const std::string &exe_path) {
  llvm::InitializeNativeTarget();
  llvm::InitializeNativeTargetAsmPrinter();
  llvm::InitializeNativeTargetAsmParser();

  auto triple = llvm::sys::getDefaultTargetTriple();
  std::string err;
  auto *target = llvm::TargetRegistry::lookupTarget(triple, err);
  if (!target) {
    std::cerr << "AOT: target lookup failed: " << err << std::endl;
    return false;
  }

  llvm::TargetOptions opts;
  auto tm = std::unique_ptr<llvm::TargetMachine>(
      target->createTargetMachine(triple, "", "", opts,
                                   llvm::Reloc::PIC_,
                                   std::nullopt,
                                   llvm::CodeGenOptLevel::Default));
  module.setDataLayout(tm->createDataLayout());
  module.setTargetTriple(triple);

  std::error_code ec;
  llvm::raw_fd_ostream dest(obj_path, ec);
  if (ec) {
    std::cerr << "AOT: cannot open " << obj_path << ": " << ec.message()
              << std::endl;
    return false;
  }

  llvm::legacy::PassManager pm;
  if (tm->addPassesToEmitFile(pm, dest, nullptr,
                               llvm::CodeGenFileType::ObjectFile)) {
    std::cerr << "AOT: target cannot emit object file" << std::endl;
    return false;
  }
  pm.run(module);
  dest.close();

  // Link the object file to an executable
  std::string cmd = "g++ -no-pie " + obj_path + " -o " + exe_path + " 2>/dev/null";
  if (system(cmd.c_str()) != 0) {
    std::cerr << "AOT: link failed" << std::endl;
    return false;
  }
  return true;
}

int AOTCompiler::run(const std::string &exe_path) {
  int rc = system(exe_path.c_str());
  if (rc == -1) return -1;
  if (WIFEXITED(rc)) return WEXITSTATUS(rc);
  return -1;
}

} // namespace cat::aot
