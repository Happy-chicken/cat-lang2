#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class DeadBranch : public ASTPass<DeadBranch> {
public:
  static constexpr const char *name = "DeadBranch";

  void before_if_stmt(IfStmt &ifs) { fold(ifs); }

private:
  void fold(IfStmt &ifs) {
    if (is_truthy(ifs.condition)) {
      // keep then branch, drop elif and else
      keep_only(ifs, std::move(ifs.then_branch));
      return;
    }
    if (is_falsy(ifs.condition)) {
      if (!ifs.elif_branch.empty()) {
        ifs.then_branch = std::move(ifs.elif_branch[0].second);
        ifs.elif_branch.erase(ifs.elif_branch.begin());
      } else if (ifs.else_branch) {
        ifs.then_branch = std::move(ifs.else_branch);
      } else {
        ifs.then_branch = std::make_unique<Block>();
      }
      ifs.elif_branch.clear();
      ifs.else_branch.reset();
    }
  }

  static void keep_only(IfStmt &ifs, uptr<Block> kept) {
    ifs.elif_branch.clear();
    ifs.else_branch.reset();
    if (kept) ifs.then_branch = std::move(kept);
  }
};

} // namespace cat::opt::ast
