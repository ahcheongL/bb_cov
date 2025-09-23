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

// #include "llvm/ADT/Statistic.h"
// #include "llvm/Analysis/CallGraph.h"
// #include "llvm/Analysis/ValueTracking.h"
// #include "llvm/Config/llvm-config.h"
// #include "llvm/Demangle/Demangle.h"
// #include "llvm/IR/Constant.h"
// #include "llvm/IR/DebugInfo.h"
// #include "llvm/IR/GlobalValue.h"
// #include "llvm/IR/IRBuilder.h"
// #include "llvm/IR/Instruction.h"
// #include "llvm/IR/LegacyPassManager.h"
// #include "llvm/IR/Module.h"
// #include "llvm/IR/Verifier.h"
// #include "llvm/Support/Debug.h"
// #include "llvm/Support/raw_ostream.h"
// #include "llvm/Transforms/Utils/BasicBlockUtils.h"

#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/Module.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"
#include "llvm/Support/raw_ostream.h"

using namespace std;

class BB_COV_Pass : public llvm::PassInfoMixin<BB_COV_Pass> {
 public:
  llvm::PreservedAnalyses run(llvm::Module &Module,
                              llvm::ModuleAnalysisManager &MAM);

 private:
  bool instrument_module();
  void instrument_bb_cov(llvm::Function &Func, const string &filename);

  llvm::Module *Mod_ptr = NULL;
  llvm::FunctionCallee record_bb;
  llvm::FunctionCallee cov_fini;

  vector<llvm::Function *> func_list = {};

  // To measure coverage
  map<string, map<string, set<string>>> file_bb_map;

  llvm::IRBuilder<> *IRB = NULL;
  map<std::string, llvm::Constant *> new_string_globals = {};
};

static map<std::string, llvm::Constant *> new_string_globals;
static llvm::Constant *gen_new_string_constant(string, llvm::IRBuilder<> *,
                                               llvm::Module *Mod);
#endif