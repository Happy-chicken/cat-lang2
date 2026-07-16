#pragma once

#include <ostream>
#include <string>

namespace cat {
struct ExprNode;
struct StmtNode;
struct ItemNode;
struct Program;
struct Block;
struct Type;
} // namespace cat

namespace cat::ast {

inline const char *branch(bool is_last) { return is_last ? "└── " : "├── "; }

inline std::string next_prefix(const std::string &prefix, bool is_last) {
  if (is_last) {
    return prefix + "    ";
  } else {
    return prefix + "│   ";
  }
}

void print_type(std::ostream &os, const cat::Type &type);

void print_expr_node(std::ostream &os, const std::string &prefix, bool is_last,
                     const cat::ExprNode &node);
void print_stmt_node(std::ostream &os, const std::string &prefix, bool is_last,
                     const cat::StmtNode &node);
void print_block(std::ostream &os, const std::string &prefix, bool is_last,
                 const cat::Block &block);
void print_item_node(std::ostream &os, const std::string &prefix, bool is_last,
                     const cat::ItemNode &node);

void print(std::ostream &os, const cat::ExprNode &node);
void print(std::ostream &os, const cat::StmtNode &node);
void print(std::ostream &os, const cat::ItemNode &node);
void print(std::ostream &os, const cat::Program &program);

} // namespace cat::ast
