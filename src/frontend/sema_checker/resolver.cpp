#include "resolver.h"
#include "item.h"
#include <unordered_set>
#include <utility>
#include <variant>

static optional<std::string> find_duplicate_field(const vector<cat::Field> &fields) {
  std::unordered_set<std::string> seen;
  for (const auto &field: fields) {
    if (!seen.insert(field.name).second) {
      return field.name;
    }
  }
  return std::nullopt;
}

static optional<std::string> find_duplicate_param(const vector<cat::Parameter> &params) {
  std::unordered_set<std::string> seen;
  for (const auto &param: params) {
    if (!seen.insert(param.name).second) {
      return param.name;
    }
  }
  return std::nullopt;
}

namespace cat {
  template<class... Ts>
  struct overloaded : Ts... {
    using Ts::operator()...;
  };
  template<class... Ts>
  overloaded(Ts...) -> overloaded<Ts...>;
  bool Resolver::run(Program &program, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    for (const auto &item_node: program.items) {
      std::visit(
          overloaded{
              [&](const Class &cls) { declare_type_item(item_node, ctx, diag); },
              [&](const Trait &tr) { declare_trait_item(item_node, ctx, diag); },
              [&](const Impl &imp) {
                declare_impl_methods(imp, item_node.span, ctx, diag);
                validate_impl_target(imp, item_node.span, ctx, diag);
              },
              [&](const FunctionDef &fn) { declare_value_item(item_node, ctx, diag); },
              [&](const GlobalVar &gv) { declare_global_var(gv, item_node.span, ctx, diag); },
              [](const auto &) {}
          },
          item_node.item
      );
    }
    check_struct_recursion(program, diag);
    return !diag.has_errors();
  }

  void Resolver::declare_top_level(Symbol symbol, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    auto name = symbol.get_name();
    auto span = symbol.get_span();
    auto kind_name = symbol.kindname();

    auto existing = ctx.get_symbol_table().declare(std::move(symbol));
    if (existing) {
      std::string existing_name = existing->get_name();
      const char *existing_kind = existing->kindname();

      diag.error(span, "the name `" + name + "` (" + kind_name + ") is defined multiple times")
          .note("`" + existing_name + "` (" + existing_kind + ") redefined here")
          .emit_to(diag);
    }
  }

  void Resolver::declare_type_item(const ItemNode &item_node, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    std::visit(overloaded{[&](const Class &cls) {
                            if (auto dup = find_duplicate_field(cls.fields)) {
                              diag.error(item_node.span, "duplicate field name `" + *dup + "`")
                                  .emit_to(diag);
                            }

      vector<std::pair<string, ast::Type>> fields_sym;
      vector<bool> has_default;
      fields_sym.reserve(cls.fields.size());
      has_default.reserve(cls.fields.size());
      for (const auto &f: cls.fields) {
        auto ty = f.ty.clone();
        fields_sym.emplace_back(f.name, std::move(ty));
        has_default.push_back(f.init.has_value());
      }

      Symbol sym = Symbol::new_class(cls.name, std::move(fields_sym), std::move(has_default), item_node.span);
                            declare_top_level(std::move(sym), ctx, diag);
                          },
                          [](const auto &) {}},
               item_node.item);
  }

  void Resolver::declare_trait_item(const ItemNode &item_node, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    std::visit(overloaded{[&](const Trait &tr) {
                            std::unordered_set<std::string> method_names;

                            for (const auto &method: tr.methods) {
                              if (!method_names.insert(method.name).second) {
                                diag.error(item_node.span, "duplicate method name `" + method.name + "` in trait `" + tr.name + "`")
                                    .emit_to(diag);
                              }

                              if (auto dup = find_duplicate_param(method.params)) {
                                diag.error(item_node.span, "duplicate parameter name `" + *dup + "` in trait method `" + method.name + "`")
                                    .emit_to(diag);
                              }
                            }

                            std::vector<std::string> method_name_list;
                            method_name_list.reserve(tr.methods.size());
                            for (const auto &m: tr.methods) {
                              method_name_list.push_back(m.name);
                            }

                            Symbol sym = Symbol::new_trait(tr.name, std::move(method_name_list), item_node.span);
                            declare_top_level(std::move(sym), ctx, diag);
                          },
                          [](const auto &) {}},
               item_node.item);
  }

  void Resolver::declare_value_item(const ItemNode &item_node, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    string name;
    optional<ast::Type> return_type;
    const std::vector<Parameter> *params_ptr = nullptr;

    bool found = std::visit(overloaded{[&](const FunctionDecl &func) -> bool {
                                         name = func.name;
                                         params_ptr = &func.params;
                                         return_type = func.return_type ? optional<ast::Type>(func.return_type->clone()) : std::nullopt;
                                         return true;
                                       },
                                       [&](const FunctionDef &func) -> bool {
                                         name = func.function_header.name;
                                         params_ptr = &func.function_header.params;
                                         return_type = func.function_header.return_type ? optional<ast::Type>(func.function_header.return_type->clone()) : std::nullopt;
                                         return true;
                                       },
                                       [](const auto &) -> bool { return false; }},
                            item_node.item);

    if (!found) return;

    if (auto dup = find_duplicate_param(*params_ptr)) {
      diag.error(item_node.span, "duplicate parameter name `" + *dup + "`")
          .emit_to(diag);
    }

    vector<ast::Type> param_types;
    param_types.reserve(params_ptr->size());
    for (const auto &p: *params_ptr) {
      param_types.push_back(p.ty.clone());
    }

    ast::Type ret_ty = return_type ? return_type->clone() : ast::type_void();
    Symbol sym = Symbol::new_function(name, std::move(param_types), std::move(ret_ty), item_node.span);
    declare_top_level(std::move(sym), ctx, diag);
  }

  void Resolver::declare_impl_methods(const Impl &impl, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    for (const auto &method: impl.methods) {
      const auto &header = method.function_header;

      if (header.params.empty()) {
        diag.error(span, "Method '" + header.name + "' must have 'self' as its first parameter")
            .emit_to(diag);
      } else {
        const auto &first = header.params[0];
        if (first.name != "self") {
          diag.error(span, "Method '" + header.name + "' must have 'self' as its first parameter, found '" + first.name + "'")
              .emit_to(diag);
        } else {
          ast::Type expected = ast::type_class(impl.class_name);

          if (first.ty != expected) {
            diag.error(span, "expected `self: " + impl.class_name + "`, found `self: " + first.ty.to_string() + "`")
                .emit_to(diag);
          }
        }
      }

      // 构建参数类型列表
      std::vector<ast::Type> param_types;
      param_types.reserve(header.params.size());
      for (const auto &p: header.params) {
        param_types.push_back(p.ty.clone());
      }

      // 返回类型
      ast::Type ret_ty = header.return_type ? header.return_type->clone() : ast::type_void();

      // 符号名：类名_方法名（避免冲突）
      std::string mangled = impl.class_name + "_" + header.name;

      Symbol sym = Symbol::new_function(mangled, std::move(param_types), std::move(ret_ty), span);
      declare_top_level(std::move(sym), ctx, diag);
    }
  }

  void Resolver::declare_global_var(const GlobalVar &gv, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    optional<ast::Type> ty = gv.ty ? optional<ast::Type>(gv.ty->clone()) : std::nullopt;
    Symbol sym = Symbol::new_variable(gv.name, std::move(ty), false, span);
    declare_top_level(std::move(sym), ctx, diag);
  }

  void Resolver::validate_impl_target(const Impl &impl, Span span, semantics::SemaCtxt &ctx, error::DiagCtxt &diag) {
    if (!ctx.get_symbol_table().resolve_global(impl.class_name)) {
      diag.error(span, "class `" + impl.class_name + "` not found")
          .emit_to(diag);
    }

    if (impl.trait_name) {
      if (!ctx.get_symbol_table().resolve_global(*impl.trait_name)) {
        diag.error(span, "trait `" + *impl.trait_name + "` not found")
            .emit_to(diag);
      }
    }
  }

  void Resolver::collect_class_deps(const ast::Type &ty, vector<string> &deps) {
    std::visit([&](const auto &v) {
      using T = std::decay_t<decltype(v)>;
      if constexpr (std::is_same_v<T, ast::Type::Class>) {
        deps.push_back(v.name);
      } else if constexpr (std::is_same_v<T, ast::Type::Ptr>) {
        if (v.inner) {
          collect_class_deps(*v.inner, deps);
        }
      } else if constexpr (std::is_same_v<T, ast::Type::Ref>) {
        if (v.inner) {
          collect_class_deps(*v.inner, deps);
        }
      } else if constexpr (std::is_same_v<T, ast::Type::Own>) {
        if (v.inner) {
          collect_class_deps(*v.inner, deps);
        }
      } else if constexpr (std::is_same_v<T, ast::Type::List>) {
        if (v.inner) {
          collect_class_deps(*v.inner, deps);
        }
      }
    },
               ty.data);
  }

  void Resolver::detect_cycle(
      const string &current,
      const std::unordered_map<string, vector<string>> &deps,
      const std::unordered_map<string, Span> &spans,
      std::unordered_set<string> &visited,
      vector<string> &stack,
      std::unordered_set<string> &in_stack,
      error::DiagCtxt &diag
  ) {

    visited.insert(current);
    stack.push_back(current);
    in_stack.insert(current);

    auto it = deps.find(current);
    if (it != deps.end()) {
      for (const auto &next: it->second) {
        if (!visited.count(next)) {
          detect_cycle(next, deps, spans, visited, stack, in_stack, diag);
        } else if (in_stack.count(next)) {
          // 找到循环
          auto cycle_start = std::find(stack.begin(), stack.end(), next);
          if (cycle_start != stack.end()) {
            std::vector<std::string> cycle(cycle_start, stack.end());
            cycle.push_back(next);

            // 构建循环字符串
            std::ostringstream oss;
            for (size_t i = 0; i < cycle.size(); ++i) {
              if (i > 0) oss << " -> ";
              oss << cycle[i];
            }

            Span span = spans.count(current) ? spans.at(current) : Span{0, 0};
            diag.error(span, "recursive type definition: " + oss.str())
                .emit_to(diag);
          }
        }
      }
    }

    stack.pop_back();
    in_stack.erase(current);
  }

  void Resolver::check_struct_recursion(const Program &program, error::DiagCtxt &diag) {
    std::unordered_map<std::string, std::vector<std::string>> deps;
    std::unordered_map<std::string, Span> spans;

    // 收集所有类及其依赖
    for (const auto &item_node: program.items) {
      std::visit(overloaded{[&](const Class &cls) {
                              spans[cls.name] = item_node.span;
                              std::vector<std::string> field_deps;
                              for (const auto &field: cls.fields) {
                                collect_class_deps(field.ty, field_deps);
                              }
                              deps[cls.name] = std::move(field_deps);
                            },
                            [](const auto &) { /* 忽略其他类型 */ }},
                 item_node.item);
    }

    // 检测每个类的循环依赖
    std::unordered_set<std::string> visited;
    std::vector<std::string> stack;
    std::unordered_set<std::string> in_stack;

    for (const auto &[name, _]: deps) {
      if (!visited.count(name)) {
        detect_cycle(name, deps, spans, visited, stack, in_stack, diag);
      }
    }
  }
}// namespace cat
