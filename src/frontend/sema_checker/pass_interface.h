#pragma once
namespace cat::semantics {

class Program;
class SemaCtxt;
class DiagCtxt;

class Pass {
public:
  virtual ~Pass() = default;

  virtual const char *name() const noexcept = 0;
  virtual bool run(Program &program, SemaCtxt &ctx, DiagCtxt &diag) = 0;
};

} // namespace cat::semantics