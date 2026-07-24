#pragma once

#include "ast_pass.h"
#include "utils.h"

namespace cat::opt::ast {

class DeadBranch : public ASTPass<DeadBranch> {
public:
  void on_function(FunctionDef &fn) { walk_block(fn.body); }

  void on_if_stmt(IfStmt &ifs) {
    if (is_truthy(ifs.condition)) {
      ifs.elif_branch.clear();
      ifs.else_branch.reset();
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
};

} // namespace cat::opt::ast
