#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "diag.h"
#include "file.h"
#include "resolver.h"
#include "sema_checker.h"
#include "flow_checker.h"
#include "frontend/type_checker/type_checker.h"
#include "frontend/sema_checker/pass_manager.h"
#include "sema_ctx.h"

using namespace cat;

static auto run_sema(const string &source, bool expect_ok = true) {
    cat::File file("<test>", source);
    cat::error::DiagCtxt diag;
    diag.add_file(std::move(file));
    cat::Lexer lexer(source);
    cat::Parser parser(lexer, diag);
    auto program = parser.parse_program();

    cat::semantics::PassManager sema_pm;
    sema_pm.add_pass(std::make_unique<cat::Resolver>());
    sema_pm.add_pass(std::make_unique<cat::SemaChecker>());
    sema_pm.add_pass(std::make_unique<cat::FlowChecker>());
    sema_pm.add_pass(std::make_unique<cat::semantics::TypeChecker>());
    sema_pm.run(program, diag);

    return !diag.has_errors();
}

TEST(Sema, ValidFunction) {
    EXPECT_TRUE(run_sema(R"(
        def add(x: int, y: int) -> int { return x + y; }
        def main() -> int { return add(1, 2); }
    )"));
}

TEST(Sema, ValidRefParam) {
    EXPECT_TRUE(run_sema(R"(
        def inc(x: ref<int>) { x = x + 1; }
        def main() -> int { let a = 10; inc(a); return a; }
    )"));
}

TEST(Sema, UndefinedVariable) {
    EXPECT_FALSE(run_sema(R"(
        def main() -> int { return x; }
    )"));
}

TEST(Sema, ArgCountMismatch) {
    EXPECT_FALSE(run_sema(R"(
        def add(x: int, y: int) -> int { return x + y; }
        def main() -> int { return add(1); }
    )"));
}

TEST(Sema, TypeMismatch) {
    EXPECT_FALSE(run_sema(R"(
        def main() -> int { return true; }
    )"));
}

TEST(Sema, RefToRefError) {
    EXPECT_FALSE(run_sema(R"(
        def bad(p: ref<ref<int>>) -> int { return 0; }
        def main() -> int { return 0; }
    )"));
}

TEST(Sema, OwnToOwnError) {
    EXPECT_FALSE(run_sema(R"(
        def bad(p: own<own<int>>) {}
        def main() -> int { return 0; }
    )"));
}

TEST(Sema, RefToOwnError) {
    EXPECT_FALSE(run_sema(R"(
        def bad(p: ref<own<int>>) {}
        def main() -> int { return 0; }
    )"));
}

TEST(Sema, OwnToRefError) {
    EXPECT_FALSE(run_sema(R"(
        def bad(p: own<ref<int>>) {}
        def main() -> int { return 0; }
    )"));
}

TEST(Sema, ValidList) {
    EXPECT_TRUE(run_sema(R"(
        def main() -> int { let xs = [1, 2, 3]; return xs[0] + xs[2]; }
    )"));
}

TEST(Sema, ValidWhile) {
    EXPECT_TRUE(run_sema(R"(
        def main() -> int {
            let i = 0; let s = 0;
            while i < 10 { i = i + 1; s = s + i; }
            return s;
        }
    )"));
}

TEST(Sema, BreakOutsideLoop) {
    EXPECT_FALSE(run_sema(R"(
        def main() -> int { break; return 0; }
    )"));
}

TEST(Sema, RefTypeRequiresInit) {
    EXPECT_FALSE(run_sema(R"(
        def main()->int { let x: ref<int>; return 0; }
    )"));
}

TEST(Sema, OwnTypeRequiresInit) {
    EXPECT_FALSE(run_sema(R"(
        def main()->int { let x: own<int>; return 0; }
    )"));
}

TEST(Sema, UseAfterMove) {
    EXPECT_FALSE(run_sema(R"(
        def consume(x: own<int>) { let tmp = x; }
        def main()->int { let a = 1; consume(a); return a; }
    )"));
}

TEST(Sema, OwnParamCanBeUsedInsideFn) {
    EXPECT_TRUE(run_sema(R"(
        def take(x: own<int>) {
            let tmp = x;
            x = tmp + 1;
        }
        def main()->int {
            let a = 1;
            take(a);
            return 0;
        }
    )"));
}

TEST(Sema, UseAfterMoveInCaller) {
    EXPECT_FALSE(run_sema(R"(
        def consume(x: own<int>) { let tmp = x; }
        def main()->int { let a = 1; consume(a); return a; }
    )"));
}

TEST(Sema, RefParamNoMove) {
    EXPECT_TRUE(run_sema(R"(
        def inc(x: ref<int>) { x = x + 1; }
        def main()->int { let a = 1; inc(a); return a; }
    )"));
}

TEST(Sema, MoveInIfBranch) {
    EXPECT_FALSE(run_sema(R"(
        def consume(x: own<int>) { let tmp = x; }
        def main()->int {
            let a = 1;
            if a > 0 { consume(a); }
            return a;
        }
    )"));
}

TEST(Sema, MoveInIfElse) {
    EXPECT_FALSE(run_sema(R"(
        def consume(x: own<int>) { let tmp = x; }
        def main()->int {
            let a = 1;
            if a > 0 { consume(a); }
            else { consume(a); }
            return a;
        }
    )"));
}
