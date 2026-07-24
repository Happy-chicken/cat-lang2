#pragma once

#include "ast_pass.h"
#include "stmt.h"

namespace cat::opt::ast {

class BlockSimplifier : public ASTPass<BlockSimplifier> {
public:
  void on_function(FunctionDef &fn) { walk_block(fn.body); }

  void on_block(Block &block) {
    std::vector<StmtNode> flattened;
    for (auto &s : block.stmts) {
      if (auto *bs = std::get_if<BlockStmt>(&s.stmt)) {
        for (auto &inner : bs->block->stmts) {
          flattened.push_back(std::move(inner));
        }
      } else {
        flattened.push_back(std::move(s));
      }
    }
    block.stmts = std::move(flattened);
  }
};

} // namespace cat::opt::ast
