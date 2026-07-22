#include <gtest/gtest.h>
#include "../type_checker/type.h"

using namespace cat::semantics;

TEST(SemType, PrimitiveBasics) {
    auto i = Type::prim(PrimType::Int);
    EXPECT_TRUE(i.is_numeric());
    EXPECT_TRUE(i.is_integer());
    EXPECT_FALSE(i.is_bool());
    EXPECT_FALSE(i.is_void());
    EXPECT_EQ(i.to_string(), "int");

    auto v = Type::prim(PrimType::Void);
    EXPECT_TRUE(v.is_void());
    EXPECT_FALSE(v.is_numeric());
}

TEST(SemType, BoolType) {
    auto b = Type::prim(PrimType::Bool);
    EXPECT_TRUE(b.is_bool());
    EXPECT_FALSE(b.is_numeric());
    EXPECT_EQ(b.to_string(), "bool");
}

TEST(SemType, ErrorType) {
    auto e = Type::error();
    EXPECT_TRUE(e.is_error());
    EXPECT_FALSE(e.is_numeric());
    EXPECT_EQ(e.to_string(), "{error}");
}

TEST(SemType, PtrClone) {
    auto t = Type::ptr(Type::prim(PrimType::Int));
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "ptr<int>");
}

TEST(SemType, RefClone) {
    auto t = Type::ref(Type::prim(PrimType::Int));
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "ref<int>");
}

TEST(SemType, OwnClone) {
    auto t = Type::own(Type::prim(PrimType::Int));
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "own<int>");
}

TEST(SemType, ListClone) {
    auto t = Type::list(Type::prim(PrimType::Int));
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "list<int>");
}

TEST(SemType, FuncType) {
    std::vector<Type> params;
    params.push_back(Type::prim(PrimType::Int));
    params.push_back(Type::prim(PrimType::Float));
    auto t = Type::func(std::move(params), Type::prim(PrimType::Bool));
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "(int, float) -> bool");
}

TEST(SemType, ClassType) {
    auto t = Type::class_("Foo");
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "Foo");
}

TEST(SemType, NestedPtr) {
    auto t = Type::ptr(Type::ptr(Type::prim(PrimType::Int)));
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "ptr<ptr<int>>");
}

TEST(SemType, NestedRef) {
    auto t = Type::ref(Type::list(Type::prim(PrimType::Int)));
    auto cloned = t.clone();
    EXPECT_EQ(cloned.to_string(), "ref<list<int>>");
}

TEST(SemType, VarType) {
    auto v = Type::var(42);
    EXPECT_EQ(v.to_string(), "?42");
    auto cloned = v.clone();
    EXPECT_EQ(cloned.to_string(), "?42");
}

TEST(SemType, TraitObject) {
    auto t = Type::trait("Printable");
    EXPECT_EQ(t.to_string(), "dyn Printable");
}
