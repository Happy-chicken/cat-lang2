#include "domtree.h"
#include <gtest/gtest.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>

using namespace cat;
using namespace std;

namespace {

class DomTreeTest : public ::testing::Test {
protected:
  llvm::LLVMContext Ctx;
  unique_ptr<llvm::Module> M;

  void SetUp() override {
    M = make_unique<llvm::Module>("test", Ctx);
  }
};

TEST_F(DomTreeTest, Dominator) {
  auto *BoolTy = llvm::Type::getInt1Ty(Ctx);
  llvm::FunctionType *FT =
      llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx), {BoolTy, BoolTy}, false);
  auto *F = llvm::Function::Create(FT, llvm::Function::ExternalLinkage,
                                   "testfn", M.get());
  auto *Cond0 = F->getArg(0);
  auto *Cond1 = F->getArg(1);

  auto *B0 = llvm::BasicBlock::Create(Ctx, "b0", F);
  auto *B1 = llvm::BasicBlock::Create(Ctx, "b1", F);
  auto *B2 = llvm::BasicBlock::Create(Ctx, "b2", F);
  auto *B3 = llvm::BasicBlock::Create(Ctx, "b3", F);
  auto *B4 = llvm::BasicBlock::Create(Ctx, "b4", F);
  auto *B5 = llvm::BasicBlock::Create(Ctx, "b5", F);
  auto *B6 = llvm::BasicBlock::Create(Ctx, "b6", F);
  auto *B7 = llvm::BasicBlock::Create(Ctx, "b7", F);

  llvm::IRBuilder<> B(B0);
  B.CreateCondBr(Cond0, B1, B4);

  B.SetInsertPoint(B1);
  B.CreateCondBr(Cond1, B2, B3);

  B.SetInsertPoint(B2);
  B.CreateBr(B5);

  B.SetInsertPoint(B3);
  B.CreateBr(B5);

  B.SetInsertPoint(B4);
  B.CreateBr(B6);

  B.SetInsertPoint(B5);
  B.CreateBr(B6);

  B.SetInsertPoint(B6);
  B.CreateBr(B7);

  B.SetInsertPoint(B7);
  B.CreateRetVoid();

  opt::ana::DomTree DT(*F);

  auto &c0 = DT.children(0);
  set<uint32_t> s0(c0.begin(), c0.end());
  EXPECT_EQ(s0, set<uint32_t>({1, 4, 6}));

  auto &c1 = DT.children(1);
  set<uint32_t> s1(c1.begin(), c1.end());
  EXPECT_EQ(s1, set<uint32_t>({2, 3, 5}));
}

} // namespace
