#include <gtest/gtest.h>
#include "parser.h"
#include "lexer.h"
#include "diag.h"
#include "file.h"
#include "resolver.h"
#include "sema_checker.h"
#include "flow_checker.h"
#include "borrow_checker.h"
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
    sema_pm.add_pass(std::make_unique<cat::BorrowChecker>());
    sema_pm.add_pass(std::make_unique<cat::semantics::TypeChecker>());
    sema_pm.run(program, diag);

    return !diag.has_errors();
}

TEST(Sema, ValidFunction) {
    EXPECT_TRUE(run_sema(R"(
        fn add(x: int, y: int) -> int { return x + y; }
        fn main() -> int { return add(1, 2); }
    )"));
}

TEST(Sema, ValidRefParam) {
    EXPECT_TRUE(run_sema(R"(
        fn inc(x: ref<int>) { x = x + 1; }
        fn main() -> int { let a = 10; inc(a); return a; }
    )"));
}

TEST(Sema, UndefinedVariable) {
    EXPECT_FALSE(run_sema(R"(
        fn main() -> int { return x; }
    )"));
}

TEST(Sema, ArgCountMismatch) {
    EXPECT_FALSE(run_sema(R"(
        fn add(x: int, y: int) -> int { return x + y; }
        fn main() -> int { return add(1); }
    )"));
}

TEST(Sema, TypeMismatch) {
    EXPECT_FALSE(run_sema(R"(
        fn main() -> int { return true; }
    )"));
}

TEST(Sema, RefToRefError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: ref<ref<int>>) -> int { return 0; }
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, RefToCRefError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: ref<cref<int>>) -> int { return 0; }
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, CRefToCRefError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: cref<cref<int>>) -> int { return 0; }
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, CRefToRefError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: cref<ref<int>>) -> int { return 0; }
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, OwnToOwnError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: own<own<int>>) {}
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, RefToOwnError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: ref<own<int>>) {}
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, CRefToOwnError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: cref<own<int>>) {}
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, OwnToRefError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: own<ref<int>>) {}
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, OwnToCRefError) {
    EXPECT_FALSE(run_sema(R"(
        fn bad(p: own<cref<int>>) {}
        fn main() -> int { return 0; }
    )"));
}

TEST(Sema, ValidList) {
    EXPECT_TRUE(run_sema(R"(
        fn main() -> int { let xs = [1, 2, 3]; return xs[0] + xs[2]; }
    )"));
}

TEST(Sema, ValidWhile) {
    EXPECT_TRUE(run_sema(R"(
        fn main() -> int {
            let i = 0; let s = 0;
            while i < 10 { i = i + 1; s = s + i; }
            return s;
        }
    )"));
}

TEST(Sema, BreakOutsideLoop) {
    EXPECT_FALSE(run_sema(R"(
        fn main() -> int { break; return 0; }
    )"));
}

TEST(Sema, RefTypeRequiresInit) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int { let x: ref<int>; return 0; }
    )"));
}

TEST(Sema, OwnTypeRequiresInit) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int { let x: own<int>; return 0; }
    )"));
}

TEST(Sema, UseAfterMove) {
    EXPECT_FALSE(run_sema(R"(
        fn consume(x: own<int>) { let tmp = x; }
        fn main()->int { let a = 1; consume(a); return a; }
    )"));
}

TEST(Sema, OwnParamCanBeUsedInsideFn) {
    EXPECT_TRUE(run_sema(R"(
        fn take(x: own<int>) {
            let tmp = x;
            x = tmp + 1;
        }
        fn main()->int {
            let a = 1;
            take(a);
            return 0;
        }
    )"));
}

TEST(Sema, UseAfterMoveInCaller) {
    EXPECT_FALSE(run_sema(R"(
        fn consume(x: own<int>) { let tmp = x; }
        fn main()->int { let a = 1; consume(a); return a; }
    )"));
}

TEST(Sema, RefParamNoMove) {
    EXPECT_TRUE(run_sema(R"(
        fn inc(x: ref<int>) { x = x + 1; }
        fn main()->int { let a = 1; inc(a); return a; }
    )"));
}

TEST(Sema, MoveInIfBranch) {
    EXPECT_FALSE(run_sema(R"(
        fn consume(x: own<int>) { let tmp = x; }
        fn main()->int {
            let a = 1;
            if a > 0 { consume(a); }
            return a;
        }
    )"));
}

TEST(Sema, MoveInIfElse) {
    EXPECT_FALSE(run_sema(R"(
        fn consume(x: own<int>) { let tmp = x; }
        fn main()->int {
            let a = 1;
            if a > 0 { consume(a); }
             return a;
        }
    )"));
}

TEST(Sema, CrefParam) {
    EXPECT_TRUE(run_sema(R"(
        fn read(x: cref<int>) -> int { return x; }
        fn main()->int { let a = 5; return read(a) + a; }
    )"));
}

TEST(Sema, CrefTypeRequiresInit) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int { let a: cref<int>; return 0; }
    )"));
}

TEST(Sema, StructMoveUntyped) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int {
            let xs = [1, 2, 3];
            let ys = xs;
            return xs[0];
        }
    )"));
}

TEST(Sema, StructMoveTyped) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int {
            let xs = [1, 2, 3];
            let ys: list<int> = xs;
            return xs[0];
        }
    )"));
}

TEST(Sema, CloneDoesNotConsumeSource) {
    EXPECT_TRUE(run_sema(R"(
        fn main()->int {
            let xs = [1, 2, 3];
            let ys = xs.clone();
            return xs[0];
        }
    )"));
}

TEST(Sema, ScalarCopyDoesNotMove) {
    EXPECT_TRUE(run_sema(R"(
        fn main()->int {
            let a = 5;
            let b = a;
            return a + b;
        }
    )"));
}

TEST(Sema, DoubleRefBorrowError) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int {
            let a = 5;
            let r1: ref<int> = a;
            let r2: ref<int> = a;
            return 0;
        }
    )"));
}

TEST(Sema, ReadWhileMutBorrowedError) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int {
            let a = 5;
            let r: ref<int> = a;
            return a;
        }
    )"));
}

TEST(Sema, DoubleCrefBorrowOk) {
    EXPECT_TRUE(run_sema(R"(
        fn main()->int {
            let a = 5;
            let c1: cref<int> = a;
            let c2: cref<int> = a;
            return c1 + c2;
        }
    )"));
}

TEST(Sema, WriteWhileImmutBorrowedError) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int {
            let a = 5;
            let c: cref<int> = a;
            a = 10;
            return c;
        }
    )"));
}

TEST(Sema, OwnAfterRefError) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int {
            let a = 5;
            let r: ref<int> = a;
            let o: own<int> = a;
            return 0;
        }
    )"));
}

TEST(Sema, RefAfterOwnError) {
    EXPECT_FALSE(run_sema(R"(
        fn main()->int {
            let a = 5;
            let o: own<int> = a;
            let r: ref<int> = a;
            return 0;
        }
    )"));
}

TEST(Sema, ScopeBasedBorrowRelease) {
    EXPECT_TRUE(run_sema(R"(
        fn main()->int {
            let a = 5;
            {
                let r: ref<int> = a;
                r = 10;
            }
            return a;
        }
    )"));
}

TEST(Sema, StructParamDoesNotConsume) {
    EXPECT_TRUE(run_sema(R"(
        fn process(xs: list<int>) -> int { return xs[0]; }
        fn main()->int {
            let xs = [1, 2, 3];
            process(xs);
            process(xs);
            return xs[0];
        }
    )"));
}

TEST(Sema, OwnParamConsumes) {
    EXPECT_FALSE(run_sema(R"(
        fn take(x: own<int>) -> int { return x; }
        fn main()->int {
            let a = 5;
            take(a);
            return a;
        }
    )"));
}
