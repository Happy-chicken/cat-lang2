#include "analysis_ctx.h"
#include <llvm-20/llvm/IR/CFG.h>
#include <llvm-20/llvm/IR/InstrTypes.h>
#include <llvm-20/llvm/IR/Instructions.h>

namespace cat::opt::ana {

AnalysisCtxt::AnalysisCtxt(const llvm::Module &module) {
  for (const auto &func : module) {
    auto name = func.getName().str();
    if (name.starts_with("llvm."))
      continue;
    auto fdata = std::make_unique<FunctionAnalysisData>();
    cfgs[name] = build_cfg(func, *fdata);
    func_data[name] = std::move(fdata);
  }
}

static bool is_valid_var(const llvm::Value *v) {
  if (!v->hasName())
    return false;
  auto name = v->getName();
  if (name.empty() || name.contains('.') || name.contains("tmp") ||
      name.starts_with("list") || name.starts_with("bounds."))
    return false;
  return true;
}

static const llvm::Value *trace_to_alloca(const llvm::Value *v) {
  while (v) {
    if (llvm::isa<llvm::AllocaInst>(v))
      return v;
    if (auto *gep = llvm::dyn_cast<llvm::GetElementPtrInst>(v)) {
      v = gep->getPointerOperand();
    } else if (auto *cast = llvm::dyn_cast<llvm::CastInst>(v)) {
      v = cast->getOperand(0);
    } else {
      break;
    }
  }
  return nullptr;
}

static bool is_valid_expr(const llvm::Instruction &inst) {
  if (llvm::isa<llvm::AllocaInst>(&inst))
    return false;
  if (llvm::isa<llvm::StoreInst>(&inst))
    return false;
  if (inst.isTerminator())
    return false;
  if (llvm::isa<llvm::PHINode>(&inst))
    return false;
  if (llvm::isa<llvm::CallInst>(&inst))
    return false;
  if (llvm::isa<llvm::InvokeInst>(&inst))
    return false;
  if (inst.getType()->isVoidTy())
    return false;
  return inst.getNumOperands() > 0;
}

CFG AnalysisCtxt::build_cfg(const llvm::Function &func,
                            FunctionAnalysisData &fdata) {
  CFG cfg;

  for (const auto &bb : func) {
    BlockInfo bi;
    bi.id = static_cast<uint32_t>(cfg.blocks.size());
    bi.bb = &bb;
    extract_block_def_use(bb, bi.def, bi.use);
    cfg.blocks.push_back(std::move(bi));
  }

  fdata.block_expressions.resize(cfg.blocks.size());
  fdata.block_defs.resize(cfg.blocks.size());

  for (auto &bi : cfg.blocks) {
    auto block_id = bi.id;
    for (const auto &inst : *bi.bb) {
      if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
        if (is_valid_var(alloca)) {
          fdata.alloca_names.insert(alloca);
          fdata.def2alloca[alloca] = alloca;
          fdata.block_defs[block_id].insert(alloca);
        }
        continue;
      }

      if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
        auto *ptr = store->getPointerOperand();
        if (auto *alloca = trace_to_alloca(ptr)) {
          if (is_valid_var(alloca)) {
            fdata.def2alloca[store] = alloca;
            fdata.block_defs[block_id].insert(store);
          }
        }
      }

      if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
        if (auto *alloca = trace_to_alloca(load->getPointerOperand())) {
          if (is_valid_var(alloca))
            fdata.load2alloca[load] = alloca;
        }
      }

      if (is_valid_expr(inst)) {
        fdata.block_expressions[block_id].insert(&inst);
        fdata.all_expressions.insert(&inst);
      }
    }
  }

  for (auto &bi : cfg.blocks)
    bi.succ = get_successor_indices(*bi.bb, cfg.blocks);

  BlockInfo exit_block;
  exit_block.id = static_cast<uint32_t>(cfg.blocks.size());
  cfg.exit = exit_block.id;
  cfg.blocks.push_back(std::move(exit_block));

  for (auto &b : cfg.blocks) {
    if (b.succ.empty() && b.id != cfg.exit)
      b.succ.push_back(cfg.exit);
  }

  return cfg;
}

void AnalysisCtxt::extract_block_def_use(const llvm::BasicBlock &bb,
                                         ValueSet &def, ValueSet &use) {
  for (const auto &inst : bb) {
    if (llvm::isa<llvm::AllocaInst>(&inst)) {
      if (is_valid_var(&inst))
        def.insert(&inst);
      continue;
    }

    if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
      auto *ptr = store->getPointerOperand();
      if (is_valid_var(ptr))
        def.insert(ptr);
      for (auto &op : inst.operands()) {
        auto *v = op.get();
        if (v != ptr && is_valid_var(v))
          use.insert(v);
      }
      continue;
    }

    if (llvm::isa<llvm::CallInst>(&inst) ||
        llvm::isa<llvm::InvokeInst>(&inst)) {
      for (auto &op : inst.operands()) {
        auto *v = op.get();
        if (is_valid_var(v))
          use.insert(v);
      }
      continue;
    }

    for (auto &op : inst.operands()) {
      auto *v = op.get();
      if (is_valid_var(v))
        use.insert(v);
    }
  }
}

vector<uint32_t>
AnalysisCtxt::get_successor_indices(const llvm::BasicBlock &bb,
                                    const vector<BlockInfo> &blocks) {
  vector<uint32_t> succs;
  auto *term = bb.getTerminator();
  if (!term)
    return succs;
  for (unsigned i = 0; i < term->getNumSuccessors(); ++i) {
    auto *succ_bb = term->getSuccessor(i);
    for (auto &b : blocks) {
      if (b.bb == succ_bb) {
        succs.push_back(b.id);
        break;
      }
    }
  }
  return succs;
}

} // namespace cat::opt::ana
