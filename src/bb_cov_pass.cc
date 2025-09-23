
#include "bb_cov_pass.hpp"

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Verifier.h"

llvm::PreservedAnalyses BB_COV_Pass::run(llvm::Module &Module,
                                         llvm::ModuleAnalysisManager &MAM) {
  Mod_ptr = &Module;
  llvm::LLVMContext &Ctx = Module.getContext();

  llvm::Type *voidTy = llvm::Type::getVoidTy(Ctx);
  llvm::Type *int8Ty = llvm::Type::getInt8Ty(Ctx);
  llvm::Type *int32Ty = llvm::Type::getInt32Ty(Ctx);
  llvm::Type *int64Ty = llvm::Type::getInt64Ty(Ctx);
  llvm::Type *int8PtrTy = llvm::PointerType::get(int8Ty, 0);
  llvm::Type *int32PtrTy = llvm::PointerType::get(int32Ty, 0);

  record_bb = Module.getOrInsertFunction("__record_bb_cov", voidTy, int8PtrTy,
                                         int8PtrTy, int8PtrTy);

  cov_fini = Module.getOrInsertFunction("__cov_fini", voidTy);

  IRB = new llvm::IRBuilder<>(Ctx);

  for (llvm::Function &Func : Module.functions()) {
    const string mangled_func_name = Func.getName().str();
    const string func_name = llvm::demangle(mangled_func_name);
    // const string func_name = mangled_func_name;

    if (Func.isIntrinsic()) {
      continue;
    }

    if (func_name.find("_GLOBAL__sub_I_") != string::npos) {
      continue;
    }

    if (func_name.find("__cxx_global_var_init") != string::npos) {
      continue;
    }

    if (func_name == "main") {
      set<llvm::ReturnInst *> ret_inst_set;
      for (auto &BB : Func) {
        for (auto &I : BB) {
          if (llvm::ReturnInst *ret_inst =
                  llvm::dyn_cast<llvm::ReturnInst>(&I)) {
            ret_inst_set.insert(ret_inst);
          }
        }
      }

      for (auto ret_inst : ret_inst_set) {
        IRB->SetInsertPoint(ret_inst);
        IRB->CreateCall(cov_fini, {});
      }
    }

    auto subp = Func.getSubprogram();
    if (subp == NULL) {
      continue;
    }

    const llvm::StringRef dirname = subp->getDirectory();
    std::string filename = subp->getFilename().str();

    if (dirname != "") {
      filename = string(dirname) + "/" + filename;
    }

    llvm::errs() << "Instrumenting " << func_name << " in " << filename << "\n";

    if (filename.find("/usr") != string::npos) {
      continue;
    }

    // normal functions under test
    instrument_bb_cov(Func, filename);
  }

  for (auto iter : file_bb_map) {
    string filename = iter.first;

    string cov_filename = filename + ".cov";
    ofstream cov_file(cov_filename);

    for (auto iter2 : iter.second) {
      const set<string> &bb_set = iter2.second;
      cov_file << "F " << iter2.first << " " << false << "\n";
      for (auto &bb_name : bb_set) {
        cov_file << "b " << bb_name << " " << false << "\n";
      }
    }

    cov_file.close();
  }

  delete IRB;

  string out;
  llvm::raw_string_ostream output(out);
  bool has_error = llvm::verifyModule(*Mod_ptr, &output);

  if (has_error > 0) {
    llvm::errs() << "IR errors : \n";
    llvm::errs() << out;
    return llvm::PreservedAnalyses::all();
  }

  return llvm::PreservedAnalyses::all();
}

void BB_COV_Pass::instrument_bb_cov(llvm::Function &Func,
                                    const string &filename) {
  const string mangled_func_name = Func.getName().str();
  const string func_name = llvm::demangle(mangled_func_name);

  if (file_bb_map.find(filename) == file_bb_map.end()) {
    file_bb_map.insert({filename, {}});
  }

  if (file_bb_map[filename].find(func_name) == file_bb_map[filename].end()) {
    file_bb_map[filename].insert({func_name, {}});
  }

  llvm::Constant *filename_const =
      gen_new_string_constant(filename, IRB, Mod_ptr);
  llvm::Constant *func_name_const =
      gen_new_string_constant(func_name, IRB, Mod_ptr);

  set<string> &cur_bb_set = file_bb_map[filename][func_name];

  llvm::errs() << "Instrumenting " << func_name << " in " << filename << "\n";

  map<string, int> bb_name_count = {};

  for (llvm::BasicBlock &BB : Func) {
    auto first_inst = BB.getFirstNonPHIOrDbgOrLifetime();
    llvm::Instruction *first_instr =
        llvm::dyn_cast<llvm::Instruction>(first_inst);

    if (llvm::isa<llvm::LandingPadInst>(first_instr)) {
      continue;
    }

    unsigned begin_line_num = -1;
    unsigned end_line_num = 0;

    for (llvm::Instruction &IN : BB) {
      const llvm::DebugLoc &debugInfo = IN.getDebugLoc();
      if (!debugInfo) {
        continue;
      }

      const unsigned line_num = debugInfo.getLine();
      if (line_num == 0) {
        continue;
      }

      if (line_num < begin_line_num) {
        begin_line_num = line_num;
      }
      if (line_num > end_line_num) {
        end_line_num = line_num;
      }
    }

    // string BB_name = BB.getName().str();
    string BB_name;
    if (begin_line_num == -1) {
      BB_name = func_name + "_bb_" + to_string(bb_name_count["bb"]++);
    } else {
      BB_name = func_name + "_" + to_string(begin_line_num) + ":" +
                to_string(end_line_num);

      if (bb_name_count.find(BB_name) != bb_name_count.end()) {
        BB_name = BB_name + "_" + to_string(bb_name_count[BB_name]++);
      }
    }

    cur_bb_set.insert(BB_name);

    IRB->SetInsertPoint(first_instr);
    llvm::Constant *bb_name_const =
        gen_new_string_constant(BB_name, IRB, Mod_ptr);
    IRB->CreateCall(record_bb,
                    {filename_const, func_name_const, bb_name_const});
  }

  // Insert cov fini
  for (llvm::BasicBlock &BB : Func) {
    for (llvm::Instruction &IN : BB) {
      if (!llvm::isa<llvm::CallInst>(IN)) {
        continue;
      }

      llvm::CallInst *call_inst = llvm::dyn_cast<llvm::CallInst>(&IN);
      llvm::Function *called_func = call_inst->getCalledFunction();
      if (called_func == NULL) {
        continue;
      }

      string called_func_name = called_func->getName().str();
      if (called_func_name != "exit") {
        continue;
      }
      IRB->SetInsertPoint(call_inst);
      IRB->CreateCall(cov_fini, {});
    }
  }
  return;
}

static llvm::Constant *gen_new_string_constant(string name,
                                               llvm::IRBuilder<> *IRB,
                                               llvm::Module *Mod_ptr) {
  auto search = new_string_globals.find(name);

  if (search == new_string_globals.end()) {
    llvm::Constant *new_global = IRB->CreateGlobalString(name, "", 0, Mod_ptr);
    new_string_globals.insert(make_pair(name, new_global));
    return new_global;
  }

  return search->second;
}

extern "C" ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION,  // Plugin API version
          "BBCovPassPlugin",        // Plugin name
          LLVM_VERSION_STRING,      // LLVM version
          [](llvm::PassBuilder &PB) {
            // Register module-level pass
            PB.registerPipelineParsingCallback(
                [](llvm::StringRef Name, llvm::ModulePassManager &MPM,
                   llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "bbcov") {
                    MPM.addPass(BB_COV_Pass());
                    return true;
                  }
                  return false;
                });
          }};
}