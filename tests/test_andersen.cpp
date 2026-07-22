#include <gtest/gtest.h>

#include "andersen_solver.h"
#include "diag.h"
#include "file.h"
#include "flow_checker.h"
#include "frontend/sema_checker/pass_manager.h"
#include "frontend/type_checker/type_checker.h"
#include "ir_emitter.h"
#include "lexer.h"
#include "parser.h"
#include "resolver.h"
#include "sema_checker.h"

using namespace cat;

struct AndersenFixture : ::testing::Test {
  error::DiagCtxt diag;
  uptr<ir::IrEmitter> emitter;
  const llvm::Module *module = nullptr;

  void compile(const std::string &source) {
    File file("<test>", source);
    diag.add_file(std::move(file));
    Lexer lexer(source);
    Parser parser(lexer, diag);
    auto program = parser.parse_program();

    semantics::PassManager sema_pm;
    sema_pm.add_pass(std::make_unique<Resolver>());
    sema_pm.add_pass(std::make_unique<SemaChecker>());
    sema_pm.add_pass(std::make_unique<FlowChecker>());
    sema_pm.add_pass(std::make_unique<semantics::TypeChecker>());
    sema_pm.run(program, diag);

    ASSERT_FALSE(diag.has_errors()) << "semantic errors in test source";

    emitter = std::make_unique<ir::IrEmitter>("<test>", diag, sema_pm.get_sema_ctxt());
    emitter->compile(program);
    module = &emitter->get_module();
  }

  const llvm::Function *get_fn(const std::string &name) const {
    for (const auto &fn : *module) {
      if (fn.getName() == name && !fn.isDeclaration()) return &fn;
    }
    return nullptr;
  }

  std::string get_points_to(const llvm::Value *v, const opt::ana::AndersenSolver &solver) {
    const auto &pt = solver.points_to(v);
    if (pt.empty()) return "{}";
    std::string s;
    bool first = true;
    for (const auto *p : pt) {
      if (!first) s += ", ";
      s += p->getName().str();
      first = false;
    }
    return "{" + s + "}";
  }
};

TEST_F(AndersenFixture, RefParamPointsTo) {
  compile(R"(
    def inc(x: ref<int>) {
      x = x + 1;
    }
    def main()->int {
      let a = 10;
      inc(a);
      return a;
    }
  )");

  auto *inc_fn = get_fn("inc");
  ASSERT_NE(inc_fn, nullptr);

  auto andersen = opt::ana::compute_andersen(*inc_fn);
  const auto &pts_map = andersen->get_all_pts();

  size_t nonempty = 0;
  const llvm::Value *ref_param_node = nullptr;
  for (const auto &[v, pts] : pts_map) {
    if (!pts.empty()) {
      ++nonempty;
      ref_param_node = v;
    }
  }
  EXPECT_GE(nonempty, 1u) << "inc should have at least 1 node with points-to information";
  EXPECT_NE(ref_param_node, nullptr);
  EXPECT_FALSE(andersen->points_to(ref_param_node).empty());
}

TEST_F(AndersenFixture, SwapRefParams) {
  compile(R"(
    def swap(a: ref<int>, b: ref<int>) {
      let tmp = a;
      a = b;
      b = tmp;
    }
    def main()->int {
      let x = 10;
      let y = 20;
      swap(x, y);
      return x;
    }
  )");

  auto *swap_fn = get_fn("swap");
  ASSERT_NE(swap_fn, nullptr);

  auto andersen = opt::ana::compute_andersen(*swap_fn);
  const auto &pts_map = andersen->get_all_pts();

  size_t nonempty = 0;
  for (const auto &[v, pts] : pts_map) {
    if (!pts.empty()) ++nonempty;
  }
  EXPECT_GE(nonempty, 2u) << "swap should have at least 2 nodes with points-to information";
}

TEST_F(AndersenFixture, ClassFieldStore) {
  compile(R"(
    class Node {
      let val: int = 0;
      let next: int = 0;
    }
    def main()->int {
      let n = Node(42, 7);
      return n.val;
    }
  )");

  auto *main_fn = get_fn("main");
  ASSERT_NE(main_fn, nullptr);

  auto andersen = opt::ana::compute_andersen(*main_fn);
  ASSERT_NE(andersen, nullptr);

  bool has_n = false;
  for (const auto &[v, pts] : andersen->get_all_pts()) {
    if (v->hasName() && (v->getName().starts_with("n") || v->getName().contains("n"))) {
      has_n = true;
      break;
    }
  }
  EXPECT_TRUE(has_n);
}

TEST_F(AndersenFixture, NoAliasUnrelatedVars) {
  compile(R"(
    def main()->int {
      let a = 1;
      let b = 2;
      let c = a + b;
      return c;
    }
  )");

  auto *main_fn = get_fn("main");
  ASSERT_NE(main_fn, nullptr);

  auto andersen = opt::ana::compute_andersen(*main_fn);
  ASSERT_NE(andersen, nullptr);

  for (const auto &[v, pts] : andersen->get_all_pts()) {
    if (v->hasName()) {
      EXPECT_EQ(pts.size(), 0u) << v->getName().str() << " should not point to anything";
    }
  }
}

TEST_F(AndersenFixture, MultipleRefCalls) {
  compile(R"(
    def set_val(p: ref<int>, v: int) {
      p = v;
    }
    def main()->int {
      let a = 0;
      let b = 0;
      set_val(a, 10);
      set_val(b, 20);
      return a + b;
    }
  )");

  auto *set_val_fn = get_fn("set_val");
  ASSERT_NE(set_val_fn, nullptr);

  auto andersen = opt::ana::compute_andersen(*set_val_fn);
  const auto &pts_map = andersen->get_all_pts();

  size_t nonempty = 0;
  for (const auto &[v, pts] : pts_map) {
    if (!pts.empty()) ++nonempty;
  }
  EXPECT_GE(nonempty, 1u) << "set_val should have at least 1 node with points-to information";
}

TEST_F(AndersenFixture, NestedRefFunctions) {
  compile(R"(
    def inc(x: ref<int>) {
      x = x + 1;
    }
    def double_inc(y: ref<int>) {
      inc(y);
      inc(y);
    }
    def main()->int {
      let a = 5;
      double_inc(a);
      return a;
    }
  )");

  auto *inc_fn = get_fn("inc");
  auto *double_inc_fn = get_fn("double_inc");
  ASSERT_NE(inc_fn, nullptr);
  ASSERT_NE(double_inc_fn, nullptr);

  auto inc_pts = opt::ana::compute_andersen(*inc_fn);
  size_t inc_nonempty = 0;
  for (const auto &[v, pts] : inc_pts->get_all_pts()) {
    if (!pts.empty()) ++inc_nonempty;
  }
  EXPECT_GE(inc_nonempty, 1u) << "inc should have points-to information";

  auto dinc_pts = opt::ana::compute_andersen(*double_inc_fn);
  size_t dinc_nonempty = 0;
  for (const auto &[v, pts] : dinc_pts->get_all_pts()) {
    if (!pts.empty()) ++dinc_nonempty;
  }
  EXPECT_GE(dinc_nonempty, 1u) << "double_inc should have points-to information";
}
