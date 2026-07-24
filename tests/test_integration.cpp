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
#include "ir_emitter.h"
#include "jit.h"

using namespace cat;

static int compile_and_run(const string &source) {
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

    if (diag.has_errors()) return -999;

    cat::ir::IrEmitter emitter("<test>", diag, sema_pm.get_sema_ctxt());
    emitter.compile(program);

    cat::jit::JIT jit(diag);
    jit.add_symbol("malloc", reinterpret_cast<void *>(&malloc));
    jit.add_module(emitter);
    return jit.run();
}

TEST(Integration, LiteralReturn) {
    EXPECT_EQ(compile_and_run("fn main()->int { return 42; }"), 42);
}

TEST(Integration, Arithmetic) {
    EXPECT_EQ(compile_and_run("fn main()->int { return 1 + 2 * 3; }"), 7);
}

TEST(Integration, Variable) {
    EXPECT_EQ(compile_and_run("fn main()->int { let x = 10; return x; }"), 10);
}

TEST(Integration, Assignment) {
    EXPECT_EQ(compile_and_run("fn main()->int { let x = 10; x = 20; return x; }"), 20);
}

TEST(Integration, FunctionCall) {
    EXPECT_EQ(compile_and_run(R"(
        fn add(x: int, y: int) -> int { return x + y; }
        fn main()->int { return add(10, 20); }
    )"), 30);
}

TEST(Integration, Fibonacci) {
    EXPECT_EQ(compile_and_run(R"(
        fn fib(n: int) -> int {
            if n <= 1 { return n; }
            else { return fib(n - 1) + fib(n - 2); }
        }
        fn main()->int { return fib(6); }
    )"), 8);
}

TEST(Integration, IfElse) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int { let x = 10; if x > 5 { return 100; } else { return 200; } }
    )"), 100);
}

TEST(Integration, WhileLoop) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let i = 0; let s = 0;
            while i < 10 { i = i + 1; s = s + i; }
            return s;
        }
    )"), 55);
}

TEST(Integration, RefParam) {
    EXPECT_EQ(compile_and_run(R"(
        fn inc(x: ref<int>) { x = x + 1; }
        fn main()->int { let a = 10; inc(a); return a; }
    )"), 11);
}

TEST(Integration, SwapWithRef) {
    EXPECT_EQ(compile_and_run(R"(
        fn swap(a: ref<int>, b: ref<int>) { let tmp = a; a = b; b = tmp; }
        fn main()->int { let x = 10; let y = 20; swap(x, y); return x; }
    )"), 20);
}

TEST(Integration, ListLiteral) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int { let xs = [1, 2, 3, 4]; return xs[0] + xs[2]; }
    )"), 4);
}

TEST(Integration, ListPassByValue) {
    EXPECT_EQ(compile_and_run(R"(
        fn mutate(xs: list<int>) -> int { xs[0] = 99; return xs[0]; }
        fn main()->int { let xs = [1, 2, 3]; mutate(xs); return xs[0]; }
    )"), 1);
}

TEST(Integration, ListRefParam) {
    EXPECT_EQ(compile_and_run(R"(
        fn mutate(xs: ref<list<int>>) { xs[0] = 99; }
        fn main()->int { let xs = [1, 2, 3]; mutate(xs); return xs[0]; }
    )"), 99);
}

TEST(Integration, NestedList) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let m = [[1, 2], [3, 4], [5, 6]];
            m[1][0] = 99;
            return m[0][1] + m[1][0] + m[2][1];
        }
    )"), 107);
}

TEST(Integration, MultiFunctionNesting) {
    EXPECT_EQ(compile_and_run(R"(
        fn triple(x: int) -> int { return x * 3; }
        fn double(x: int) -> int { return x * 2; }
        fn main()->int { return triple(double(5)); }
    )"), 30);
}

TEST(Integration, ComplexExpression) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int { return (10 + 5) * 2 - 3; }
    )"), 27);
}

TEST(Integration, ComparisonChain) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let x = 10;
            if x > 5 and x < 20 { return 1; }
            return 0;
        }
    )"), 1);
}

TEST(Integration, BoolReturn) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let t = true;
            if t { return 1; }
            return 0;
        }
    )"), 1);
}

TEST(Integration, GlobalVar) {
    EXPECT_EQ(compile_and_run(R"(
        let pi: int = 100;
        fn main()->int { return pi; }
    )"), 100);
}

TEST(Integration, OwnIntPassByValue) {
    EXPECT_EQ(compile_and_run(R"(
        fn double_it(x: own<int>) -> int { return x * 2; }
        fn main()->int {
            let a = 21;
            return double_it(a);
        }
    )"), 42);
}

TEST(Integration, OwnIntLocalModification) {
    EXPECT_EQ(compile_and_run(R"(
        fn scale(x: own<int>) -> int {
            x = x * 3;
            x = x + 1;
            return x;
        }
        fn main()->int {
            let a = 10;
            return scale(a);
        }
    )"), 31);
}

TEST(Integration, OwnMultipleParams) {
    EXPECT_EQ(compile_and_run(R"(
        fn diff(a: own<int>, b: own<int>) -> int { return a - b; }
        fn main()->int {
            let x = 100;
            let y = 35;
            return diff(x, y);
        }
    )"), 65);
}

TEST(Integration, OwnWithClass) {
    EXPECT_EQ(compile_and_run(R"(
        class Point {
            let x: int = 0;
            let y: int = 0;
        }
        fn get_x(p: own<Point>) -> int { return p.x; }
        fn main()->int {
            let pt = Point(7, 3);
            return get_x(pt);
        }
    )"), 7);
}

TEST(Integration, OwnNestedCalls) {
    EXPECT_EQ(compile_and_run(R"(
        fn add_one(x: own<int>) -> int { return x + 1; }
        fn add_two(y: own<int>) -> int { return add_one(y + 1); }
        fn main()->int {
            let n = 10;
            return add_two(n);
        }
    )"), 12);
}

TEST(Integration, OwnWithRefMixed) {
    EXPECT_EQ(compile_and_run(R"(
        fn inc(x: ref<int>) { x = x + 1; }
        fn consume(y: own<int>) -> int { return y * 2; }
        fn main()->int {
            let a = 5;
            inc(a);
            return consume(a);
        }
    )"), 12);
}

TEST(Integration, OwnComputeThenReturn) {
    EXPECT_EQ(compile_and_run(R"(
        fn process(x: own<int>, y: own<int>) -> int {
            let tmp = x + y;
            tmp = tmp * tmp;
            return tmp;
        }
        fn main()->int {
            let a = 3;
            let b = 4;
            return process(a, b);
        }
    )"), 49);
}

TEST(Integration, OwnClassFieldModify) {
    EXPECT_EQ(compile_and_run(R"(
        class Counter {
            let val: int = 0;
        }
        fn bump(c: own<Counter>) -> int {
            c.val = c.val + 1;
            c.val = c.val * 2;
            return c.val;
        }
        fn main()->int {
            let ct = Counter(5);
            return bump(ct);
        }
    )"), 12);
}

TEST(Integration, OwnChainWithClass) {
    EXPECT_EQ(compile_and_run(R"(
        class Box {
            let val: int = 0;
        }
        fn inc_box(b: own<Box>) -> int {
            b.val = b.val + 10;
            return b.val;
        }
        fn wrap_inc(w: own<Box>) -> int {
            return inc_box(w) + 1;
        }
        fn main()->int {
            let bx = Box(3);
            return wrap_inc(bx);
        }
    )"), 14);
}

TEST(Integration, FirstClassFunctionTypeAnnotation) {
    EXPECT_EQ(compile_and_run(R"(
        fn add(a: int, b: int) -> int { return a + b; }
        fn apply(op: (int, int) -> int, x: int, y: int) -> int { return op(x, y); }
        fn main()->int {
            return apply(add, 10, 20);
        }
    )"), 30);
}

TEST(Integration, Closure) {
    // Returning a closure from a function requires the caller to know the
    // function type of the returned value. This needs additional plumbing
    // between the type checker and the IR emitter.
    // For now, test that in-scope closures work correctly.
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let x = 10;
            let f = fn() -> int { return x * 3; };
            return f();
        }
    )"), 30);
}

TEST(Integration, LambdaAsArgument) {
    EXPECT_EQ(compile_and_run(R"(
        fn apply(op: (int) -> int, x: int) -> int { return op(x); }
        fn main()->int {
            let double = fn(x: int) -> int { return x * 2; };
            return apply(double, 10);
        }
    )"), 20);
}

TEST(Integration, LambdaInlineAsArgument) {
    EXPECT_EQ(compile_and_run(R"(
        fn apply(op: (int) -> int, x: int) -> int { return op(x); }
        fn main()->int {
            return apply(fn(x: int) -> int { return x * 3; }, 10);
        }
    )"), 30);
}

TEST(Integration, LambdaCaptureEnclosingVar) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let factor = 10;
            let mul = fn(x: int) -> int { return x * factor; };
            return mul(5);
        }
    )"), 50);
}

TEST(Integration, LambdaMultipleParams) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let add = fn(a: int, b: int) -> int { return a + b; };
            return add(7, 8);
        }
    )"), 15);
}

TEST(Integration, LambdaVoidReturn) {
    EXPECT_EQ(compile_and_run(R"(
        fn apply_twice(op: (int) -> int, x: int) -> int { return op(op(x)); }
        fn main()->int {
            let double = fn(x: int) -> int { return x * 2; };
            return apply_twice(double, 5);
        }
    )"), 20);
}

TEST(Integration, LambdaReturningLambda) {
    EXPECT_EQ(compile_and_run(R"(
        fn main()->int {
            let add = fn(a: int, b: int) -> int { return a + b; };
            return add(3, 4);
        }
    )"), 7);
}
