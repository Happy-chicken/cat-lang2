#pragma once
#include "common.h"
#include <string>

namespace llvm {
class Module;
}

namespace cat::error {
class DiagCtxt;
}

namespace cat::aot {

// Ahead-of-time compilation: compile a Module to an object file,
// link it, and optionally execute.  All LLVM CodeGen dependencies
// are isolated here so they don't pollute libcatlang_lib.a.
class AOTCompiler {
public:
  // Compile module to an object file at `obj_path`.
  static bool compile(llvm::Module &module, const std::string &obj_path,
                      const std::string &exe_path);

  // Run the binary at `exe_path` and return its exit code.
  static int run(const std::string &exe_path);
};

} // namespace cat::aot
