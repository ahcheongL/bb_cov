#ifndef BB_COV_PASS_HPP
#define BB_COV_PASS_HPP

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <map>
#include <string>

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/PassManager.h"


class Path_COV_Pass : public llvm::PassInfoMixin<Path_COV_Pass> {
 public:
  llvm::PreservedAnalyses run(llvm::Module                &Module,
                              llvm::ModuleAnalysisManager &MAM);

 private:
  void instrument_main(llvm::Function &Func);
  void instrument_path_cov(llvm::Function &Func);

  llvm::Module      *Mod_ptr = NULL;
  llvm::LLVMContext *Ctxt_ptr = NULL;
  llvm::IRBuilder<> *IRB = NULL;

  llvm::Type *voidTy;
  llvm::Type *int8Ty;
  llvm::Type *int32Ty;
  llvm::Type *int8PtrTy;
  llvm::Type *int32PtrTy;

  llvm::GlobalVariable *hash_val_glob = NULL;

  unsigned int bb_id = 1;

  std::map<std::string, llvm::GlobalVariable *> new_string_globals = {};
  llvm::GlobalVariable *gen_new_string_constant(const std::string &name);
};
#endif