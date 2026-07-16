#pragma once
namespace cat {
  class Program;
}
namespace cat::error {
  class DiagCtxt;
}
namespace cat::semantics {

  class SemaCtxt;

  class Pass {
public:
    virtual ~Pass() = default;

    virtual const char *name() const noexcept = 0;
    virtual bool run(::cat::Program &program, SemaCtxt &ctx, ::cat::error::DiagCtxt &diag) = 0;
  };

}// namespace cat::semantics
