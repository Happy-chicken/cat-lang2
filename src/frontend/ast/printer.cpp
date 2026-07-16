#include "printer.h"
#include "expr.h"
#include "item.h"
#include "stmt.h"
#include "type.h"
#include <sstream>
#include <variant>

namespace cat::ast {

namespace {

template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;

const char *to_string(BinaryOp op) {
  switch (op) {
  case BinaryOp::Add:
    return "Add";
  case BinaryOp::Sub:
    return "Sub";
  case BinaryOp::Mul:
    return "Mul";
  case BinaryOp::Div:
    return "Div";
  case BinaryOp::Eq:
    return "Eq";
  case BinaryOp::NotEq:
    return "NotEq";
  case BinaryOp::Lt:
    return "Lt";
  case BinaryOp::Gt:
    return "Gt";
  case BinaryOp::Le:
    return "Le";
  case BinaryOp::Ge:
    return "Ge";
  case BinaryOp::And:
    return "And";
  case BinaryOp::Or:
    return "Or";
  }
  return "???";
}

const char *to_string(UnaryOp op) {
  switch (op) {
  case UnaryOp::Neg:
    return "Neg";
  case UnaryOp::Not:
    return "Not";
  case UnaryOp::Inc:
    return "Inc";
  case UnaryOp::Dec:
    return "Dec";
  case UnaryOp::Deref:
    return "Deref";
  case UnaryOp::AddrOf:
    return "AddrOf";
  }
  return "???";
}

std::string type_to_string(const Type &type) {
  std::stringstream ss;
  std::visit(overloaded{
                 [&](const Type::Int &) { ss << "Int"; },
                 [&](const Type::Float &) { ss << "Float"; },
                 [&](const Type::Bool &) { ss << "Bool"; },
                 [&](const Type::Char &) { ss << "Char"; },
                 [&](const Type::Str &) { ss << "Str"; },
                 [&](const Type::Void &) { ss << "Void"; },
                 [&](const Type::Ptr &t) {
                   ss << "Ptr<" << type_to_string(*t.inner) << ">";
                 },
                 [&](const Type::List &t) {
                   ss << "List<" << type_to_string(*t.inner) << ">";
                 },
                 [&](const Type::Class &t) { ss << t.name; },
             },
             type.data);
  return ss.str();
}

void print_literal(std::ostream &os, const Literal &lit) {
  std::visit(overloaded{
                 [&](int64_t v) { os << v; },
                 [&](bool v) { os << (v ? "true" : "false"); },
                 [&](float v) { os << v; },
                 [&](char v) { os << "'" << v << "'"; },
                 [&](const std::string &v) { os << "\"" << v << "\""; },
             },
             lit);
}

void print_params_inline(std::ostream &os,
                         const std::vector<Parameter> &params) {
  os << "(";
  for (size_t i = 0; i < params.size(); ++i) {
    if (i > 0)
      os << ", ";
    os << params[i].name << ": " << type_to_string(params[i].ty);
  }
  os << ")";
}

void print_expr_impl(std::ostream &os, const std::string &prefix, bool is_last,
                     const Expr &expr);
void print_stmt_impl(std::ostream &os, const std::string &prefix, bool is_last,
                     const Stmt &stmt);
void print_item_impl(std::ostream &os, const std::string &prefix, bool is_last,
                     const Item &item);

void print_expr_ptr(std::ostream &os, const std::string &prefix, bool is_last,
                    const uptr<ExprNode> &ptr) {
  if (ptr) {
    print_expr_node(os, prefix, is_last, *ptr);
  } else {
    os << prefix << branch(is_last) << "(null)\n";
  }
}

void print_opt_expr_node(std::ostream &os, const std::string &prefix,
                         bool is_last, const std::optional<ExprNode> &opt) {
  if (opt) {
    print_expr_node(os, prefix, is_last, *opt);
  } else {
    os << prefix << branch(is_last) << "(none)\n";
  }
}

void print_expr_impl(std::ostream &os, const std::string &prefix, bool is_last,
                     const Expr &expr) {
  std::visit(overloaded{
                 [&](const LiteralExpr &e) {
                   os << prefix << branch(is_last) << "LiteralExpr: ";
                   print_literal(os, e.lit);
                   os << "\n";
                 },
                 [&](const Variable &e) {
                   os << prefix << branch(is_last) << "Variable: " << e.name
                      << "\n";
                 },
                 [&](const AssignExpr &e) {
                   os << prefix << branch(is_last) << "AssignExpr\n";
                   auto child_prefix = next_prefix(prefix, is_last);
                   print_expr_ptr(os, child_prefix, false, e.target);
                   print_expr_ptr(os, child_prefix, true, e.value);
                 },
                 [&](const BinaryExpr &e) {
                   os << prefix << branch(is_last)
                      << "BinaryExpr: " << to_string(e.op) << "\n";
                   auto child_prefix = next_prefix(prefix, is_last);
                   print_expr_ptr(os, child_prefix, false, e.lhs);
                   print_expr_ptr(os, child_prefix, true, e.rhs);
                 },
                 [&](const UnaryExpr &e) {
                   os << prefix << branch(is_last)
                      << "UnaryExpr: " << to_string(e.op) << "\n";
                   auto child_prefix = next_prefix(prefix, is_last);
                   print_expr_ptr(os, child_prefix, true, e.expr);
                 },
                 [&](const CallExpr &e) {
                   os << prefix << branch(is_last) << "CallExpr\n";
                   auto child_prefix = next_prefix(prefix, is_last);
                   print_expr_ptr(os, child_prefix, e.args.empty(), e.callee);
                   for (size_t i = 0; i < e.args.size(); ++i) {
                     print_expr_ptr(os, child_prefix, i == e.args.size() - 1,
                                    e.args[i]);
                   }
                 },
                 [&](const MemberExpr &e) {
                   os << prefix << branch(is_last) << "MemberExpr: ." << e.field
                      << "\n";
                   auto child_prefix = next_prefix(prefix, is_last);
                   print_expr_ptr(os, child_prefix, true, e.object);
                 },
                 [&](const IndexExpr &e) {
                   os << prefix << branch(is_last) << "IndexExpr\n";
                   auto child_prefix = next_prefix(prefix, is_last);
                   print_expr_ptr(os, child_prefix, false, e.object);
                   print_expr_ptr(os, child_prefix, true, e.index);
                 },
                 [&](const ListExpr &e) {
                   os << prefix << branch(is_last) << "ListExpr\n";
                   if (e.elements.empty())
                     return;
                   auto child_prefix = next_prefix(prefix, is_last);
                   for (size_t i = 0; i < e.elements.size(); ++i) {
                     print_expr_ptr(os, child_prefix,
                                    i == e.elements.size() - 1, e.elements[i]);
                   }
                 },
             },
             expr);
}

void print_stmt_impl(std::ostream &os, const std::string &prefix, bool is_last,
                     const Stmt &stmt) {
  std::visit(
      overloaded{
          [&](const VarDefStmt &e) {
            os << prefix << branch(is_last) << "VarDef: " << e.name;
            if (e.ty)
              os << " : " << type_to_string(*e.ty);
            os << "\n";
            if (e.init) {
              auto child_prefix = next_prefix(prefix, is_last);
              print_expr_node(os, child_prefix, true, *e.init);
            }
          },
          [&](const IfStmt &e) {
            os << prefix << branch(is_last) << "IfStmt\n";
            auto if_prefix = next_prefix(prefix, is_last);

            bool has_elif = !e.elif_branch.empty();
            bool has_else = e.else_branch != nullptr;

            os << if_prefix << branch(false) << "Condition:\n";
            auto cond_prefix = next_prefix(if_prefix, false);
            print_expr_node(os, cond_prefix, true, e.condition);

            bool then_is_last = !has_elif && !has_else;
            os << if_prefix << branch(then_is_last) << "Then:\n";
            auto then_prefix = next_prefix(if_prefix, then_is_last);
            print_block(os, then_prefix, true, *e.then_branch);

            for (size_t i = 0; i < e.elif_branch.size(); ++i) {
              bool elif_is_last = (i == e.elif_branch.size() - 1) && !has_else;
              os << if_prefix << branch(elif_is_last) << "Elif " << i << ":\n";
              auto elif_prefix = next_prefix(if_prefix, elif_is_last);

              os << elif_prefix << branch(false) << "Condition:\n";
              auto elif_cond_prefix = next_prefix(elif_prefix, false);
              print_expr_node(os, elif_cond_prefix, true,
                              e.elif_branch[i].first);

              os << elif_prefix << branch(true) << "Then:\n";
              auto elif_then_prefix = next_prefix(elif_prefix, true);
              print_block(os, elif_then_prefix, true, *e.elif_branch[i].second);
            }

            if (has_else) {
              os << if_prefix << branch(true) << "Else:\n";
              auto else_prefix = next_prefix(if_prefix, true);
              print_block(os, else_prefix, true, *e.else_branch);
            }
          },
          [&](const LoopStmt &e) {
            os << prefix << branch(is_last) << "LoopStmt\n";
            auto loop_prefix = next_prefix(prefix, is_last);

            os << loop_prefix << branch(false) << "Condition:\n";
            auto cond_prefix = next_prefix(loop_prefix, false);
            print_expr_node(os, cond_prefix, true, e.condition);

            os << loop_prefix << branch(true) << "Body:\n";
            auto body_prefix = next_prefix(loop_prefix, true);
            print_block(os, body_prefix, true, *e.body);
          },
          [&](const ExprStmt &e) {
            os << prefix << branch(is_last) << "ExprStmt\n";
            auto child_prefix = next_prefix(prefix, is_last);
            print_expr_node(os, child_prefix, true, e.expr);
          },
          [&](const ReturnStmt &e) {
            os << prefix << branch(is_last) << "ReturnStmt\n";
            if (e.expr) {
              auto child_prefix = next_prefix(prefix, is_last);
              print_expr_node(os, child_prefix, true, *e.expr);
            }
          },
          [&](const BreakStmt &) {
            os << prefix << branch(is_last) << "BreakStmt\n";
          },
          [&](const ContinueStmt &) {
            os << prefix << branch(is_last) << "ContinueStmt\n";
          },
          [&](const BlockStmt &e) {
            os << prefix << branch(is_last) << "BlockStmt\n";
            auto child_prefix = next_prefix(prefix, is_last);
            print_block(os, child_prefix, true, *e.block);
          },
      },
      stmt);
}

void print_item_impl(std::ostream &os, const std::string &prefix, bool is_last,
                     const Item &item) {
  std::visit(
      overloaded{
          [&](const FunctionDef &e) {
            os << prefix << branch(is_last)
               << "FunctionDef: " << e.function_header.name;
            print_params_inline(os, e.function_header.params);
            if (e.function_header.return_type) {
              os << " -> " << type_to_string(*e.function_header.return_type);
            }
            os << "\n";

            auto fn_prefix = next_prefix(prefix, is_last);
            os << fn_prefix << branch(true) << "Body:\n";
            auto body_prefix = next_prefix(fn_prefix, true);
            print_block(os, body_prefix, true, e.body);
          },
          [&](const FunctionDecl &e) {
            os << prefix << branch(is_last) << "FunctionDecl: " << e.name;
            print_params_inline(os, e.params);
            if (e.return_type) {
              os << " -> " << type_to_string(*e.return_type);
            }
            os << "\n";
          },
          [&](const Class &e) {
            os << prefix << branch(is_last) << "Class: " << e.name << "\n";
            auto cls_prefix = next_prefix(prefix, is_last);
            for (size_t i = 0; i < e.fields.size(); ++i) {
              os << cls_prefix << branch(i == e.fields.size() - 1)
                 << "Field: " << e.fields[i].name << " : "
                 << type_to_string(e.fields[i].ty);
              if (e.fields[i].init) {
                os << " with init\n";
                auto field_prefix =
                    next_prefix(cls_prefix, i == e.fields.size() - 1);
                print_expr_node(os, field_prefix, true, *e.fields[i].init);
              } else {
                os << "\n";
              }
            }
          },
          [&](const Trait &e) {
            os << prefix << branch(is_last) << "Trait: " << e.name << "\n";
            auto trait_prefix = next_prefix(prefix, is_last);
            for (size_t i = 0; i < e.methods.size(); ++i) {
              os << trait_prefix << branch(i == e.methods.size() - 1)
                 << "Method: " << e.methods[i].name;
              print_params_inline(os, e.methods[i].params);
              if (e.methods[i].return_type) {
                os << " -> " << type_to_string(*e.methods[i].return_type);
              }
              os << "\n";
            }
          },
          [&](const Impl &e) {
            os << prefix << branch(is_last) << "Impl: ";
            if (e.trait_name) {
              os << *e.trait_name << " for " << e.class_name;
            } else {
              os << e.class_name;
            }
            os << "\n";
            auto impl_prefix = next_prefix(prefix, is_last);
            for (size_t i = 0; i < e.methods.size(); ++i) {
              os << impl_prefix << branch(i == e.methods.size() - 1)
                 << "Method: " << e.methods[i].function_header.name;
              print_params_inline(os, e.methods[i].function_header.params);
              if (e.methods[i].function_header.return_type) {
                os << " -> "
                   << type_to_string(*e.methods[i].function_header.return_type);
              }
              os << "\n";

              auto meth_prefix =
                  next_prefix(impl_prefix, i == e.methods.size() - 1);
              os << meth_prefix << branch(true) << "Body:\n";
              auto body_prefix = next_prefix(meth_prefix, true);
              print_block(os, body_prefix, true, e.methods[i].body);
            }
          },
          [&](const GlobalVar &e) {
            os << prefix << branch(is_last) << "GlobalVar: " << e.name;
            if (e.ty)
              os << " : " << type_to_string(*e.ty);
            os << "\n";
            if (e.init) {
              auto child_prefix = next_prefix(prefix, is_last);
              print_expr_node(os, child_prefix, true, *e.init);
            }
          },
      },
      item);
}

} // namespace

void print_type(std::ostream &os, const cat::Type &type) {
  os << type_to_string(type);
}

void print_expr_node(std::ostream &os, const std::string &prefix, bool is_last,
                     const cat::ExprNode &node) {
  print_expr_impl(os, prefix, is_last, node.expr);
}

void print_stmt_node(std::ostream &os, const std::string &prefix, bool is_last,
                     const cat::StmtNode &node) {
  print_stmt_impl(os, prefix, is_last, node.stmt);
}

void print_block(std::ostream &os, const std::string &prefix, bool is_last,
                 const cat::Block &block) {
  os << prefix << branch(is_last) << "Block\n";
  auto child_prefix = next_prefix(prefix, is_last);
  for (size_t i = 0; i < block.stmts.size(); ++i) {
    print_stmt_node(os, child_prefix, i == block.stmts.size() - 1,
                    block.stmts[i]);
  }
}

void print_item_node(std::ostream &os, const std::string &prefix, bool is_last,
                     const cat::ItemNode &node) {
  print_item_impl(os, prefix, is_last, node.item);
}

void print(std::ostream &os, const cat::ExprNode &node) {
  print_expr_node(os, "", true, node);
}

void print(std::ostream &os, const cat::StmtNode &node) {
  print_stmt_node(os, "", true, node);
}

void print(std::ostream &os, const cat::ItemNode &node) {
  print_item_node(os, "", true, node);
}

void print(std::ostream &os, const cat::Program &program) {
  os << "Program\n";
  for (size_t i = 0; i < program.items.size(); ++i) {
    print_item_node(os, "", i == program.items.size() - 1, program.items[i]);
  }
}

} // namespace cat::ast
