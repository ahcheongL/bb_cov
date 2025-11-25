#ifndef FUNC_SEQ_PASS_HPP
#define FUNC_SEQ_PASS_HPP

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "bb/bb_map.hpp"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace std;

class FuncSeqPass : public llvm::PassInfoMixin<FuncSeqPass> {
 public:
  llvm::PreservedAnalyses run(llvm::Module                &Module,
                              llvm::ModuleAnalysisManager &MAM);

 private:
  void     instrument_main(llvm::Function &Func);
  uint32_t insert_func_probes();
  void insert_func_probe_one_func(llvm::Function &Func, const string &filename);
  set<llvm::Function *> get_dtor_funcs();

  llvm::Module      *Mod_ptr = NULL;
  llvm::LLVMContext *Ctxt_ptr = NULL;
  llvm::IRBuilder<> *IRB = NULL;

  llvm::Type *voidTy = NULL;
  llvm::Type *int8Ty = NULL;
  llvm::Type *int32Ty = NULL;
  llvm::Type *int8PtrTy = NULL;
  llvm::Type *int32PtrTy = NULL;

  map<string, llvm::GlobalVariable *> new_string_globals = {};
  llvm::GlobalVariable *gen_new_string_constant(const string &name);
};
#endif