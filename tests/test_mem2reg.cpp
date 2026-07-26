#include "mem2reg.h"

#include <gtest/gtest.h>

#include <llvm/Analysis/DominanceFrontier.h>
#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>

#include <string>

using namespace cat;

namespace {

class Mem2RegTest : public ::testing::Test {
protected:
  llvm::LLVMContext Ctx;
  std::unique_ptr<llvm::Module> M;
  llvm::IRBuilder<> *Builder = nullptr;

  void SetUp() override {
    M = std::make_unique<llvm::Module>("test", Ctx);
  }

  llvm::Function *createFunction(llvm::Type *RetTy,
                                 std::vector<llvm::Type *> ParamTys = {}) {
    auto *FT = llvm::FunctionType::get(RetTy, ParamTys, false);
    return llvm::Function::Create(FT, llvm::Function::ExternalLinkage, "testfn",
                                  M.get());
  }

  void runMem2Reg(llvm::Function &F) {
    llvm::DominatorTree DT(F);
    llvm::DominanceFrontier DF;
    DF.analyze(DT);
    opt::PromoteMem2Reg pass(F, DT, DF);
    pass.run();
  }

  unsigned countAllocas(llvm::Function &F) {
    unsigned n = 0;
    for (auto &BB : F)
      for (auto &I : BB)
        if (llvm::isa<llvm::AllocaInst>(&I))
          ++n;
    return n;
  }

  unsigned countPhis(llvm::Function &F) {
    unsigned n = 0;
    for (auto &BB : F)
      for (auto &I : BB)
        if (llvm::isa<llvm::PHINode>(&I))
          ++n;
    return n;
  }

  unsigned countLoads(llvm::Function &F) {
    unsigned n = 0;
    for (auto &BB : F)
      for (auto &I : BB)
        if (llvm::isa<llvm::LoadInst>(&I))
          ++n;
    return n;
  }

  unsigned countStores(llvm::Function &F) {
    unsigned n = 0;
    for (auto &BB : F)
      for (auto &I : BB)
        if (llvm::isa<llvm::StoreInst>(&I))
          ++n;
    return n;
  }
};

TEST_F(Mem2RegTest, SingleStore) {
  auto *F = createFunction(llvm::Type::getInt32Ty(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  Builder = new llvm::IRBuilder<>(Entry);

  auto *Alloca = Builder->CreateAlloca(llvm::Type::getInt32Ty(Ctx));
  Builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 42),
                       Alloca);
  auto *Load = Builder->CreateLoad(llvm::Type::getInt32Ty(Ctx), Alloca);
  Builder->CreateRet(Load);

  ASSERT_EQ(countAllocas(*F), 1u);
  ASSERT_EQ(countLoads(*F), 1u);
  ASSERT_EQ(countStores(*F), 1u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 0u);
  EXPECT_EQ(countLoads(*F), 0u);
  EXPECT_EQ(countStores(*F), 0u);
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

TEST_F(Mem2RegTest, SingleBlockMultipleStores) {
  auto *F = createFunction(llvm::Type::getInt32Ty(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  Builder = new llvm::IRBuilder<>(Entry);

  auto *Alloca = Builder->CreateAlloca(llvm::Type::getInt32Ty(Ctx));
  Builder->CreateStore(llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0),
                       Alloca);
  auto *Load1 = Builder->CreateLoad(llvm::Type::getInt32Ty(Ctx), Alloca);
  auto *Add = Builder->CreateAdd(
      Load1, llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 1));
  Builder->CreateStore(Add, Alloca);
  auto *Load2 = Builder->CreateLoad(llvm::Type::getInt32Ty(Ctx), Alloca);
  Builder->CreateRet(Load2);

  ASSERT_EQ(countAllocas(*F), 1u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 0u);
  EXPECT_EQ(countLoads(*F), 0u);
  EXPECT_EQ(countStores(*F), 0u);
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

TEST_F(Mem2RegTest, IfElseMerge) {
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *BoolTy = llvm::Type::getInt1Ty(Ctx);
  auto *F = createFunction(Int32Ty, {BoolTy});
  auto *Cond = F->getArg(0);

  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  auto *ThenBB = llvm::BasicBlock::Create(Ctx, "then", F);
  auto *ElseBB = llvm::BasicBlock::Create(Ctx, "else", F);
  auto *MergeBB = llvm::BasicBlock::Create(Ctx, "merge", F);

  Builder = new llvm::IRBuilder<>(Entry);
  auto *Alloca = Builder->CreateAlloca(Int32Ty);
  Builder->CreateCondBr(Cond, ThenBB, ElseBB);

  Builder->SetInsertPoint(ThenBB);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 10), Alloca);
  Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(ElseBB);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 20), Alloca);
  Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(MergeBB);
  auto *Load = Builder->CreateLoad(Int32Ty, Alloca);
  Builder->CreateRet(Load);

  ASSERT_EQ(countAllocas(*F), 1u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 0u);
  EXPECT_EQ(countLoads(*F), 0u);
  EXPECT_EQ(countStores(*F), 0u);
  EXPECT_EQ(countPhis(*F), 1u);
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

TEST_F(Mem2RegTest, LoopAccumulator) {
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *BoolTy = llvm::Type::getInt1Ty(Ctx);
  auto *F = createFunction(Int32Ty, {Int32Ty});
  auto *N = F->getArg(0);

  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  auto *LoopHeader = llvm::BasicBlock::Create(Ctx, "loop", F);
  auto *LoopBody = llvm::BasicBlock::Create(Ctx, "body", F);
  auto *ExitBB = llvm::BasicBlock::Create(Ctx, "exit", F);

  Builder = new llvm::IRBuilder<>(Entry);
  auto *Acc = Builder->CreateAlloca(Int32Ty);
  auto *I = Builder->CreateAlloca(Int32Ty);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 0), Acc);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 0), I);
  Builder->CreateBr(LoopHeader);

  Builder->SetInsertPoint(LoopHeader);
  auto *IVal = Builder->CreateLoad(Int32Ty, I);
  auto *Cond = Builder->CreateICmpSLT(IVal, N);
  Builder->CreateCondBr(Cond, LoopBody, ExitBB);

  Builder->SetInsertPoint(LoopBody);
  auto *AccVal = Builder->CreateLoad(Int32Ty, Acc);
  auto *NewAcc = Builder->CreateAdd(AccVal, IVal);
  Builder->CreateStore(NewAcc, Acc);
  auto *NextI = Builder->CreateAdd(
      IVal, llvm::ConstantInt::get(Int32Ty, 1));
  Builder->CreateStore(NextI, I);
  Builder->CreateBr(LoopHeader);

  Builder->SetInsertPoint(ExitBB);
  auto *RetVal = Builder->CreateLoad(Int32Ty, Acc);
  Builder->CreateRet(RetVal);

  ASSERT_EQ(countAllocas(*F), 2u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 0u);
  EXPECT_EQ(countLoads(*F), 0u);
  EXPECT_EQ(countStores(*F), 0u);
  EXPECT_GE(countPhis(*F), 2u);
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

TEST_F(Mem2RegTest, NestedIf) {
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *BoolTy = llvm::Type::getInt1Ty(Ctx);
  auto *F = createFunction(Int32Ty, {BoolTy, BoolTy});
  auto *C1 = F->getArg(0);
  auto *C2 = F->getArg(1);

  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  auto *OuterThen = llvm::BasicBlock::Create(Ctx, "othen", F);
  auto *InnerThen = llvm::BasicBlock::Create(Ctx, "ithen", F);
  auto *InnerElse = llvm::BasicBlock::Create(Ctx, "ielse", F);
  auto *InnerMerge = llvm::BasicBlock::Create(Ctx, "imerge", F);
  auto *OuterElse = llvm::BasicBlock::Create(Ctx, "oelse", F);
  auto *MergeBB = llvm::BasicBlock::Create(Ctx, "merge", F);

  Builder = new llvm::IRBuilder<>(Entry);
  auto *Alloca = Builder->CreateAlloca(Int32Ty);
  Builder->CreateCondBr(C1, OuterThen, OuterElse);

  Builder->SetInsertPoint(OuterThen);
  Builder->CreateCondBr(C2, InnerThen, InnerElse);

  Builder->SetInsertPoint(InnerThen);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 1), Alloca);
  Builder->CreateBr(InnerMerge);

  Builder->SetInsertPoint(InnerElse);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 2), Alloca);
  Builder->CreateBr(InnerMerge);

  Builder->SetInsertPoint(InnerMerge);
  Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(OuterElse);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 3), Alloca);
  Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(MergeBB);
  auto *Load = Builder->CreateLoad(Int32Ty, Alloca);
  Builder->CreateRet(Load);

  ASSERT_EQ(countAllocas(*F), 1u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 0u);
  EXPECT_EQ(countLoads(*F), 0u);
  EXPECT_EQ(countStores(*F), 0u);
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

TEST_F(Mem2RegTest, UndefValuePath) {
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *BoolTy = llvm::Type::getInt1Ty(Ctx);
  auto *F = createFunction(Int32Ty, {BoolTy});
  auto *Cond = F->getArg(0);

  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  auto *ThenBB = llvm::BasicBlock::Create(Ctx, "then", F);
  auto *MergeBB = llvm::BasicBlock::Create(Ctx, "merge", F);

  Builder = new llvm::IRBuilder<>(Entry);
  auto *Alloca = Builder->CreateAlloca(Int32Ty);
  Builder->CreateCondBr(Cond, ThenBB, MergeBB);

  Builder->SetInsertPoint(ThenBB);
  Builder->CreateStore(llvm::ConstantInt::get(Int32Ty, 100), Alloca);
  Builder->CreateBr(MergeBB);

  Builder->SetInsertPoint(MergeBB);
  auto *Load = Builder->CreateLoad(Int32Ty, Alloca);
  Builder->CreateRet(Load);

  ASSERT_EQ(countAllocas(*F), 1u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 0u);
  EXPECT_EQ(countLoads(*F), 0u);
  EXPECT_EQ(countStores(*F), 0u);
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

TEST_F(Mem2RegTest, NotPromotableArray) {
  auto *F = createFunction(llvm::Type::getInt32Ty(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  Builder = new llvm::IRBuilder<>(Entry);

  auto *ArrTy = llvm::ArrayType::get(llvm::Type::getInt32Ty(Ctx), 4);
  auto *Alloca = Builder->CreateAlloca(ArrTy);
  Builder->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(Ctx), 0));
  (void)Alloca;

  ASSERT_EQ(countAllocas(*F), 1u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 1u); // array alloca should NOT be promoted
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

TEST_F(Mem2RegTest, NotPromotableAddressTaken) {
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *F = createFunction(
      llvm::Type::getVoidTy(Ctx),
      {llvm::PointerType::get(Ctx, 0)});
  auto *DestPtr = F->getArg(0);
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  Builder = new llvm::IRBuilder<>(Entry);

  auto *Alloca = Builder->CreateAlloca(Int32Ty);
  Builder->CreateStore(Alloca, DestPtr);
  Builder->CreateRetVoid();

  ASSERT_EQ(countAllocas(*F), 1u);

  runMem2Reg(*F);

  EXPECT_EQ(countAllocas(*F), 1u);
  EXPECT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  delete Builder;
}

}  // namespace
