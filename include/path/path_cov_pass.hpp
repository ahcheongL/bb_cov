#ifndef BB_COV_PASS_HPP
#define BB_COV_PASS_HPP

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace std;

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

  map<string, llvm::GlobalVariable *> new_string_globals = {};
  llvm::GlobalVariable *gen_new_string_constant(const string &name);
};
#endif