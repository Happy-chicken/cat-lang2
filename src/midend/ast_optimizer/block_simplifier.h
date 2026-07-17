#pragma once

#include "ast_pass.h"

namespace cat::opt::ast {

class BlockSimplifier : public ASTPass<BlockSimplifier> {
public:
  static constexpr const char *name = "BlockSimplifier";

  void after_block(Block &block) {
    // merge adjacent block statements: BlockStmt (which contains a Block)
    // into the parent's stmt list
    merge_block_stmts(block);
  }

private:
  void merge_block_stmts(Block &block) {
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

} // namespace cat::midend
