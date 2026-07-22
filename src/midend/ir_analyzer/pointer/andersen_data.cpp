#include "andersen_data.h"

namespace cat::opt::ana {

void AndersenGraph::build(const llvm::Function &func) {
  for (const auto &bb : func) {
    for (const auto &inst : bb) {
      if (auto *alloca = llvm::dyn_cast<llvm::AllocaInst>(&inst)) {
        if (has_name(alloca)) {
          add_node(alloca);
        }
      }

      if (auto *store = llvm::dyn_cast<llvm::StoreInst>(&inst)) {
        auto *val = store->getValueOperand();
        auto *ptr = store->getPointerOperand();
        auto *base_ptr = trace_to_base(ptr);
        auto *base_val = trace_to_base(val);

        if (base_ptr && base_val && has_name(base_ptr) && has_name(base_val)) {
          if (is_ptr_val(val)) {
            add_copy(val, base_ptr);
            add_addr_of(base_val, base_ptr);
          } else {
            add_addr_of(val, base_ptr);
          }
        }
      }

      if (auto *load = llvm::dyn_cast<llvm::LoadInst>(&inst)) {
        if (!is_ptr_val(&inst)) continue;
        auto *ptr = load->getPointerOperand();
        auto *base_ptr = trace_to_base(ptr);

        if (base_ptr && has_name(base_ptr) && has_name(&inst)) {
          add_load(base_ptr, &inst);
        }
      }
    }
  }
}

} // namespace cat::opt::ana
