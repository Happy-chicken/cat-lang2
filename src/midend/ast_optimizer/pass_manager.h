#pragma once

#include "ast_pass.h"
#include "item.h"
#include <concepts>
#include <functional>
#include <memory>
#include <type_traits>
#include <vector>

namespace cat::opt::ast {

template <typename T>
concept HasRun = requires(T &t, Program &p) {
  { t.run(p) } -> std::same_as<void>;
};

class PassManager {
public:
  template <typename Pass, typename... Args>
    requires std::is_base_of_v<ASTPass<Pass>, Pass> ||
             requires(Pass &&p, Program &prog) {
               { p.run(prog) } -> std::same_as<void>;
             }
  void add_pass(Args &&...args) {
    auto pass = std::make_shared<Pass>(std::forward<Args>(args)...);
    passes.push_back([pass](Program &program) { pass->run(program); });
  }

  void run(Program &program) {
    for (auto &pass : passes) {
      pass(program);
    }
  }

  size_t size() const noexcept { return passes.size(); }

private:
  vector<std::function<void(Program &)>> passes;
};

} // namespace cat::opt::ast
