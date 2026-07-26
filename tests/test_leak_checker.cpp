#include "leak_checker.h"

#include <gtest/gtest.h>

#include <llvm/IR/BasicBlock.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/Dominators.h>
#include <llvm/IR/Function.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Type.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/raw_ostream.h>

#include <memory>
#include <sstream>

using namespace cat;

namespace {

class LeakCheckerTest : public ::testing::Test {
protected:
  llvm::LLVMContext Ctx;
  std::unique_ptr<llvm::Module> M;
  llvm::Function *test_f = nullptr;
  llvm::Function *malloc_f = nullptr;
  llvm::Function *free_f = nullptr;
  llvm::Function *calloc_f = nullptr;
  llvm::Function *external_f = nullptr;

  void SetUp() override {
    M = std::make_unique<llvm::Module>("test", Ctx);

    auto *I8Ptr = llvm::PointerType::get(Ctx, 0);
    auto *I32Ty = llvm::Type::getInt32Ty(Ctx);
    auto *I64Ty = llvm::Type::getInt64Ty(Ctx);

    // Declare external malloc / free.
    auto *MallocFT = llvm::FunctionType::get(I8Ptr, {I64Ty}, false);
    malloc_f = llvm::Function::Create(MallocFT, llvm::Function::ExternalLinkage,
                                      "malloc", M.get());

    auto *CallocFT = llvm::FunctionType::get(I8Ptr, {I64Ty, I64Ty}, false);
    calloc_f = llvm::Function::Create(CallocFT, llvm::Function::ExternalLinkage,
                                      "calloc", M.get());

    auto *FreeFT = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx),
                                           {I8Ptr}, false);
    free_f = llvm::Function::Create(FreeFT, llvm::Function::ExternalLinkage,
                                    "free", M.get());

    // An external function used for escape testing.
    auto *ExtFT = llvm::FunctionType::get(llvm::Type::getVoidTy(Ctx),
                                          {I8Ptr}, false);
    external_f = llvm::Function::Create(ExtFT, llvm::Function::ExternalLinkage,
                                        "ext_consume", M.get());
  }

  llvm::Function *create_test_fn(llvm::Type *RetTy,
                                 std::vector<llvm::Type *> ParamTys = {}) {
    auto *FT = llvm::FunctionType::get(RetTy, ParamTys, false);
    test_f = llvm::Function::Create(FT, llvm::Function::ExternalLinkage,
                                    "testfn", M.get());
    return test_f;
  }

  unsigned count_reports_definite(const std::vector<opt::LeakReport> &reports) {
    unsigned n = 0;
    for (auto &r : reports)
      if (r.is_definite)
        ++n;
    return n;
  }

  unsigned count_reports_potential(const std::vector<opt::LeakReport> &reports) {
    unsigned n = 0;
    for (auto &r : reports)
      if (!r.is_definite)
        ++n;
    return n;
  }
};

// 1. Normal free: malloc then free — no report.
TEST_F(LeakCheckerTest, NormalFree) {
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);
  auto *Ptr = B.CreateCall(malloc_f, {Size});
  B.CreateCall(free_f, {Ptr});
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);
  LC.run();

  auto &reports = LC.get_reports();
  EXPECT_EQ(reports.size(), 0u);
}

// 2. Straight-line leak: malloc then return without free — definite report.
TEST_F(LeakCheckerTest, StraightLineLeak) {
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);
  auto *Ptr = B.CreateCall(malloc_f, {Size});
  (void)Ptr;
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);
  LC.run();

  auto &reports = LC.get_reports();
  EXPECT_GE(count_reports_definite(reports), 1u);
}

// 3. If-else: one branch frees, the other does not — may-leak.
TEST_F(LeakCheckerTest, IfElseOneBranchFrees) {
  auto *BoolTy = llvm::Type::getInt1Ty(Ctx);
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx), {BoolTy});
  auto *Cond = F->getArg(0);

  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  auto *ThenBB = llvm::BasicBlock::Create(Ctx, "then", F);
  auto *ElseBB = llvm::BasicBlock::Create(Ctx, "else", F);
  auto *MergeBB = llvm::BasicBlock::Create(Ctx, "merge", F);

  llvm::IRBuilder<> B(Entry);
  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);

  // malloc in entry, free only in then.
  auto *Ptr = B.CreateCall(malloc_f, {Size});
  B.CreateCondBr(Cond, ThenBB, ElseBB);

  B.SetInsertPoint(ThenBB);
  B.CreateCall(free_f, {Ptr});
  B.CreateBr(MergeBB);

  B.SetInsertPoint(ElseBB);
  B.CreateBr(MergeBB);

  B.SetInsertPoint(MergeBB);
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);
  LC.run();

  auto &reports = LC.get_reports();
  // Must-leak should NOT fire (one path frees); may-leak expected.
  EXPECT_EQ(count_reports_definite(reports), 0u);
  EXPECT_GE(count_reports_potential(reports), 1u);
}

// 4. If-else: both branches free — no report.
TEST_F(LeakCheckerTest, IfElseBothFree) {
  auto *BoolTy = llvm::Type::getInt1Ty(Ctx);
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx), {BoolTy});
  auto *Cond = F->getArg(0);

  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  auto *ThenBB = llvm::BasicBlock::Create(Ctx, "then", F);
  auto *ElseBB = llvm::BasicBlock::Create(Ctx, "else", F);
  auto *MergeBB = llvm::BasicBlock::Create(Ctx, "merge", F);

  llvm::IRBuilder<> B(Entry);
  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);

  auto *Ptr = B.CreateCall(malloc_f, {Size});
  B.CreateCondBr(Cond, ThenBB, ElseBB);

  B.SetInsertPoint(ThenBB);
  B.CreateCall(free_f, {Ptr});
  B.CreateBr(MergeBB);

  B.SetInsertPoint(ElseBB);
  B.CreateCall(free_f, {Ptr});
  B.CreateBr(MergeBB);

  B.SetInsertPoint(MergeBB);
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);
  LC.run();

  auto &reports = LC.get_reports();
  EXPECT_EQ(reports.size(), 0u);
}

// 5. Return ownership: malloc then return the pointer — escaped, no leak.
TEST_F(LeakCheckerTest, ReturnOwnership) {
  auto *I8Ptr = llvm::PointerType::get(Ctx, 0);
  auto *F = create_test_fn(I8Ptr);

  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);
  auto *Ptr = B.CreateCall(malloc_f, {Size});
  B.CreateRet(Ptr);

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);
  LC.run();

  auto &reports = LC.get_reports();
  EXPECT_EQ(reports.size(), 0u);
}

// 6. Pass to external function (conservative mode) — escaped, no leak.
TEST_F(LeakCheckerTest, PassToExternalConservative) {
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);
  auto *Ptr = B.CreateCall(malloc_f, {Size});
  B.CreateCall(external_f, {Ptr});
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);  // conservative mode
  LC.run();

  auto &reports = LC.get_reports();
  EXPECT_EQ(reports.size(), 0u);
}

// 7. Pass to external function (strict mode) — allocated persists, may report.
TEST_F(LeakCheckerTest, PassToExternalStrict) {
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);
  auto *Ptr = B.CreateCall(malloc_f, {Size});
  B.CreateCall(external_f, {Ptr});
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, true);  // strict mode
  LC.run();

  auto &reports = LC.get_reports();
  // In strict mode, external calls are NOT escapes → still Allocated at exit.
  EXPECT_GE(count_reports_definite(reports), 1u);
}

// 8. Double free: free twice — the second free is on Freed state.
//    For now the dataflow just transitions to DoubleFreed without reporting.
//    We check that no false leak is reported.
TEST_F(LeakCheckerTest, DoubleFree) {
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);
  auto *Ptr = B.CreateCall(malloc_f, {Size});
  B.CreateCall(free_f, {Ptr});
  B.CreateCall(free_f, {Ptr});
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);
  LC.run();

  auto &reports = LC.get_reports();
  EXPECT_EQ(count_reports_definite(reports), 0u);
}

// 9. Dump output: verify that dump_reports produces expected output.
TEST_F(LeakCheckerTest, DumpOutput) {
  auto *F = create_test_fn(llvm::Type::getVoidTy(Ctx));
  auto *Entry = llvm::BasicBlock::Create(Ctx, "entry", F);
  llvm::IRBuilder<> B(Entry);

  auto *I64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Size = llvm::ConstantInt::get(I64Ty, 8);
  B.CreateCall(malloc_f, {Size});
  B.CreateRetVoid();

  ASSERT_FALSE(llvm::verifyFunction(*F, &llvm::errs()));

  llvm::DominatorTree DT(*F);
  opt::LeakChecker LC(*F, DT, false);
  LC.run();

  auto &reports = LC.get_reports();
  EXPECT_GE(reports.size(), 1u);

  // Capture dump output.
  std::string buf;
  llvm::raw_string_ostream OS(buf);
  LC.dump_reports(OS);
  OS.flush();

  EXPECT_NE(buf.find("DEFINITE LEAK"), std::string::npos);
  EXPECT_NE(buf.find("malloc"), std::string::npos);
  EXPECT_NE(buf.find("testfn"), std::string::npos);
  EXPECT_NE(buf.find("Path"), std::string::npos);
}

}  // namespace
