#include "builtin_registry.h"
namespace cat::runtime {

void BuiltinRegistry::register_type(const std::string &tag,
                                    std::vector<BuiltinMethodDesc> methods) {
  for (auto &m : methods) {
    methods_[{tag, m.name}] = std::move(m);
  }
}

std::optional<std::reference_wrapper<const BuiltinMethodDesc>>
BuiltinRegistry::lookup(const std::string &tag,
                        const std::string &method) const {
  auto it = methods_.find({tag, method});
  if (it == methods_.end())
    return std::nullopt;
  return std::cref(it->second);
}

bool BuiltinRegistry::is_method_declared(const std::string &tag,
                                         const std::string &method) const {
  return methods_.find({tag, method}) != methods_.end();
}

void BuiltinRegistry::register_func(BuiltinFuncDesc desc) {
  funcs_[desc.name] = std::move(desc);
}

std::optional<std::reference_wrapper<const BuiltinFuncDesc>>
BuiltinRegistry::lookup_standalone(const std::string &name) const {
  auto it = funcs_.find(name);
  if (it == funcs_.end())
    return std::nullopt;
  return std::cref(it->second);
}

bool BuiltinRegistry::is_standalone_declared(const std::string &name) const {
  return funcs_.find(name) != funcs_.end();
}

void BuiltinRegistry::init_defaults() {
  extern void register_list_builtins(BuiltinRegistry &);
  register_list_builtins(*this);
  extern void register_io_builtins(BuiltinRegistry &);
  register_io_builtins(*this);
}

} // namespace cat::runtime
