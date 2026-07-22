#include <gtest/gtest.h>
#include "type.h"

using namespace cat::ast;

TEST(AstType, PrimitiveEquality) {
    EXPECT_EQ(type_int(), type_int());
    EXPECT_EQ(type_float(), type_float());
    EXPECT_NE(type_int(), type_float());
}

TEST(AstType, ClonePreservesEquality) {
    auto t = type_ptr(type_int());
    auto cloned = t.clone();
    EXPECT_EQ(t, cloned);
}

TEST(AstType, RefCloneEquality) {
    auto t = type_ref(type_int());
    auto cloned = t.clone();
    EXPECT_EQ(t, cloned);
    EXPECT_NE(t, type_int());
}

TEST(AstType, OwnCloneEquality) {
    auto t = type_own(type_int());
    auto cloned = t.clone();
    EXPECT_EQ(t, cloned);
    EXPECT_NE(t, type_int());
}

TEST(AstType, PrimitiveToString) {
    EXPECT_EQ(type_int().to_string(), "int");
    EXPECT_EQ(type_float().to_string(), "float");
    EXPECT_EQ(type_bool().to_string(), "bool");
    EXPECT_EQ(type_char().to_string(), "char");
    EXPECT_EQ(type_void().to_string(), "void");
}

TEST(AstType, NestedToString) {
    EXPECT_EQ(type_ptr(type_int()).to_string(), "ptr<int>");
    EXPECT_EQ(type_ref(type_int()).to_string(), "ref<int>");
    EXPECT_EQ(type_own(type_int()).to_string(), "own<int>");
    EXPECT_EQ(type_list(type_int()).to_string(), "list<int>");
    EXPECT_EQ(type_class("Foo").to_string(), "Foo");
    EXPECT_EQ(type_ptr(type_ptr(type_int())).to_string(), "ptr<ptr<int>>");
    EXPECT_EQ(type_ref(type_list(type_int())).to_string(), "ref<list<int>>");
    EXPECT_EQ(type_own(type_ptr(type_int())).to_string(), "own<ptr<int>>");
}

TEST(AstType, ComplexClone) {
    auto t = type_ptr(type_ref(type_int()));
    auto cloned = t.clone();
    EXPECT_EQ(t, cloned);
    auto t2 = type_ptr(type_own(type_int()));
    EXPECT_NE(t, t2);
}
