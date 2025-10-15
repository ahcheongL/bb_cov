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

class BB_COV_Pass : public llvm::PassInfoMixin<BB_COV_Pass> {
 public:
  llvm::PreservedAnalyses run(llvm::Module                &Module,
                              llvm::ModuleAnalysisManager &MAM);

 private:
  void instrument_main(llvm::Function &Func);
  void instrument_bb_cov(llvm::Function &Func, const string &filename);
  void init_bb_map_rt();

  llvm::Module      *Mod_ptr = NULL;
  llvm::LLVMContext *Ctxt_ptr = NULL;
  llvm::IRBuilder<> *IRB = NULL;

  llvm::Type *voidTy = NULL;
  llvm::Type *int8Ty = NULL;
  llvm::Type *int32Ty = NULL;
  llvm::Type *int8PtrTy = NULL;
  llvm::Type *int32PtrTy = NULL;

  llvm::StructType *cfileEntryTy = NULL;
  llvm::StructType *cfuncEntryTy = NULL;
  llvm::StructType *cbbEntryTy = NULL;

  unsigned int bb_id = 0;

  // Entire basic block map
  // file -> func -> set(bb)
  struct GFileEntry *file_bb_map[sizeof(unsigned char) * 256]{};

  llvm::GlobalVariable *gen_cfile_entry(GFileEntry *file_entry);
  llvm::GlobalVariable *gen_cfunc_entry(GFuncEntry *func_entry);
  llvm::GlobalVariable *gen_cbb_entry(GBBEntry *bb_entry);

  map<string, llvm::GlobalVariable *> new_string_globals = {};
  llvm::GlobalVariable *gen_new_string_constant(const string &name);
};
#endif