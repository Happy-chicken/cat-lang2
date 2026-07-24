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

void BuiltinRegistry::init_defaults() {
  extern void register_list_builtins(BuiltinRegistry &);
  register_list_builtins(*this);
}

} // namespace cat::runtime
