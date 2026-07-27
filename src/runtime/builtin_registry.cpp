#include "builtin_registry.h"
#include "io_builtin.h"
#include "types/list_builtin.h"

namespace cat::runtime {

auto IrGenCtxtRef::declare_runtime(std::string_view name, llvm::Type *ret,
                                   std::vector<llvm::Type *> param_tys,
                                   bool var_arg) const -> llvm::Function * {
  if (auto *f = module.getFunction(name))
    return f;
  auto *ft = llvm::FunctionType::get(ret, param_tys, var_arg);
  return llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name,
                                &module);
}

void BuiltinRegistry::register_type(std::string_view tag,
                                    std::vector<BuiltinMethodDesc> methods) {
  auto tag_str = std::string{tag};
  for (auto &m : methods)
    methods_[{tag_str, m.name}] = std::move(m);
}

auto BuiltinRegistry::lookup(std::string_view tag, std::string_view method) const
    -> std::optional<std::reference_wrapper<const BuiltinMethodDesc>> {
  auto it = methods_.find({std::string{tag}, std::string{method}});
  if (it == methods_.end())
    return std::nullopt;
  return std::cref(it->second);
}

auto BuiltinRegistry::is_method_declared(std::string_view tag,
                                         std::string_view method) const
    -> bool {
  return methods_.find({std::string{tag}, std::string{method}}) !=
         methods_.end();
}

void BuiltinRegistry::register_func(BuiltinFuncDesc desc) {
  funcs_[desc.name] = std::move(desc);
}

auto BuiltinRegistry::lookup_standalone(std::string_view name) const
    -> std::optional<std::reference_wrapper<const BuiltinFuncDesc>> {
  auto it = funcs_.find(std::string{name});
  if (it == funcs_.end())
    return std::nullopt;
  return std::cref(it->second);
}

auto BuiltinRegistry::is_standalone_declared(std::string_view name) const
    -> bool {
  return funcs_.find(std::string{name}) != funcs_.end();
}

void BuiltinRegistry::init_defaults() {
  register_list_builtins(*this);
  register_io_builtins(*this);
}

} // namespace cat::runtime
