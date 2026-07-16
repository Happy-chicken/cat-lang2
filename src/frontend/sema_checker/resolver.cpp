#include "resolver.h"
#include <variant>

namespace cat {
template <class... Ts> struct overloaded : Ts... {
  using Ts::operator()...;
};
template <class... Ts> overloaded(Ts...) -> overloaded<Ts...>;
bool Resolver::run(Program &program, semantics::SemaCtxt &ctx,
                   error::DiagCtxt &diag) {
  for (const auto &item_node : program.items) {
    std::visit(
        overloaded{
            [&](Class &cls) { declare_type_item(item_node, ctx, diag); },
            [&](Trait &tr) { declare_trait_item(item_node, ctx, diag); },
            [&](Impl &imp) {
              declare_impl_methods(item_node, ctx, diag);
              validate_impl_target(imp, item_node.span, ctx, diag);
            },
            [&](FunctionDef &fn) { declare_value_item(item_node, ctx, diag); },
            [&](GlobalVar &gv) { declare_global_var(item_node, ctx, diag); },
            [](const auto &) {}},
        item_node.item);
  }
  return true;
}
} // namespace cat