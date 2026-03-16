#ifndef FUNC_COV_PASS_HPP
#define FUNC_COV_PASS_HPP

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <map>
#include <set>
#include <string>

#include "func/func_map.hpp"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"

class FUNC_COV_Pass : public llvm::PassInfoMixin<FUNC_COV_Pass> {
public:
  llvm::PreservedAnalyses run(llvm::Module &Module,
                              llvm::ModuleAnalysisManager &MAM);

private:
  void instrument_main(llvm::Function &Func);

  void insert_func_probes();
  void insert_func_probe_one_func(llvm::Function &Func,
                                  const std::string &filename);

  void init_func_map_rt();

  std::set<llvm::Function *> get_dtor_funcs();

  bool is_probe_func(llvm::Function &Func);

  llvm::Module *Mod_ptr = NULL;
  llvm::LLVMContext *Ctxt_ptr = NULL;
  llvm::IRBuilder<> *IRB = NULL;

  llvm::Type *voidTy = NULL;
  llvm::Type *int8Ty = NULL;
  llvm::Type *int32Ty = NULL;
  llvm::Type *int8PtrTy = NULL;
  llvm::Type *int32PtrTy = NULL;

  llvm::StructType *cfileEntryTy = NULL;
  llvm::StructType *cfuncEntryTy = NULL;

  uint32_t func_id = 0;

  // Entire func map
  // file -> set(func)
  struct GFileEntry *file_func_map[sizeof(unsigned char) * 256]{};

  llvm::GlobalVariable *gen_cfile_entry(GFileEntry *file_entry);
  llvm::GlobalVariable *gen_cfunc_entry(GFuncEntry *func_entry);

  std::map<std::string, llvm::GlobalVariable *> new_string_globals = {};
  llvm::GlobalVariable *gen_new_string_constant(const std::string &name);
};
#endif