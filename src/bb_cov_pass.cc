
#include "bb_cov_pass.hpp"

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Verifier.h"

llvm::PreservedAnalyses BB_COV_Pass::run(llvm::Module                &Module,
                                         llvm::ModuleAnalysisManager &MAM) {
  Mod_ptr = &Module;
  llvm::LLVMContext &Ctx = Module.getContext();

  voidTy = llvm::Type::getVoidTy(Ctx);
  int8Ty = llvm::Type::getInt8Ty(Ctx);
  int32Ty = llvm::Type::getInt32Ty(Ctx);
  int64Ty = llvm::Type::getInt64Ty(Ctx);
  int8PtrTy = llvm::PointerType::get(int8Ty, 0);
  int32PtrTy = llvm::PointerType::get(int32Ty, 0);

  IRB = new llvm::IRBuilder<>(Ctx);

  llvm::Function *main_func = Module.getFunction("main");
  if (main_func == NULL) {
    llvm::errs()
        << "[bb_cov] main function not found, skipping instrumentation.\n";
    return llvm::PreservedAnalyses::all();
  }

  unsigned int num_instrumented_funcs = 0;

  for (llvm::Function &Func : Module.functions()) {
    const string mangled_func_name = Func.getName().str();
    const string func_name = llvm::demangle(mangled_func_name);

    if (Func.isIntrinsic()) { continue; }

    if (func_name.find("_GLOBAL__sub_I_") != string::npos) { continue; }

    if (func_name.find("__cxx_global_var_init") != string::npos) { continue; }

    auto subp = Func.getSubprogram();
    if (subp == NULL) { continue; }

    const llvm::StringRef dirname = subp->getDirectory();
    std::string           filename = subp->getFilename().str();

    if (dirname != "") { filename = string(dirname) + "/" + filename; }

    if (filename.find("/usr") != string::npos) { continue; }

    // normal functions under test
    instrument_bb_cov(Func, filename);
    num_instrumented_funcs++;
  }

  instrument_main(*main_func);

  init_bb_map_rt();

  llvm::GlobalVariable *num_bbs_global = new llvm::GlobalVariable(
      *Mod_ptr, int32Ty, true, llvm::GlobalValue::ExternalLinkage,
      llvm::ConstantInt::get(int32Ty, bb_id), "__num_bbs");

  llvm::outs() << "[bb_cov] Instrumented " << num_instrumented_funcs
               << " functions.\n";

  delete IRB;

  string                   out;
  llvm::raw_string_ostream output(out);
  bool                     has_error = llvm::verifyModule(*Mod_ptr, &output);

  if (has_error > 0) {
    llvm::errs() << "IR errors : \n";
    llvm::errs() << out;
    // Mod_ptr->print(llvm::errs(), nullptr);
    // llvm::errs() << "\n";
  }

  return llvm::PreservedAnalyses::all();
}

void BB_COV_Pass::instrument_main(llvm::Function &Func) {
  if (Func.arg_size() != 2) {
    llvm::errs() << "[bb_cov] bb_cov pass requires the main function to have 2 "
                    "arguments (argc and argv).\n";
    llvm::errs() << "[bb_cov] Coverage instrumentation may not work.\n";
    return;
  }

  auto first_inst = Func.getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
  IRB->SetInsertPoint(first_inst);

  llvm::Value *const argc = Func.getArg(0);
  llvm::Value *const argv = Func.getArg(1);

  llvm::Type *int8PtrPtrTy = llvm::PointerType::get(int8PtrTy, 0);

  llvm::AllocaInst *const argc_ptr = IRB->CreateAlloca(int32Ty);
  llvm::AllocaInst *const argv_ptr = IRB->CreateAlloca(int8PtrPtrTy);

  llvm::Instruction *const new_argc = IRB->CreateLoad(int32Ty, argc_ptr);
  llvm::Instruction *const new_argv = IRB->CreateLoad(int8PtrPtrTy, argv_ptr);

  argc->replaceAllUsesWith(new_argc);
  argv->replaceAllUsesWith(new_argv);

  IRB->SetInsertPoint(new_argc);

  IRB->CreateStore(argc, argc_ptr);
  IRB->CreateStore(argv, argv_ptr);

  llvm::Type *int8PtrPtrPtrTy = llvm::PointerType::get(int8PtrPtrTy, 0);

  llvm::FunctionCallee get_output_fn = Mod_ptr->getOrInsertFunction(
      "__get_output_fn", voidTy, int32PtrTy, int8PtrPtrPtrTy);

  IRB->CreateCall(get_output_fn, {argc_ptr, argv_ptr});

  set<llvm::ReturnInst *> ret_inst_set;
  for (auto &BB : Func) {
    for (auto &I : BB) {
      if (llvm::ReturnInst *ret_inst = llvm::dyn_cast<llvm::ReturnInst>(&I)) {
        ret_inst_set.insert(ret_inst);
      }
    }
  }

  llvm::FunctionCallee cov_fini =
      Mod_ptr->getOrInsertFunction("__cov_fini", voidTy);

  for (auto ret_inst : ret_inst_set) {
    IRB->SetInsertPoint(ret_inst);
    IRB->CreateCall(cov_fini, {});
  }
  return;
}

void BB_COV_Pass::instrument_bb_cov(llvm::Function &Func,
                                    const string   &filename) {
  const string mangled_func_name = Func.getName().str();
  const string func_name = llvm::demangle(mangled_func_name);

  llvm::GlobalVariable *filename_const = gen_new_string_constant(filename);
  llvm::GlobalVariable *func_name_const = gen_new_string_constant(func_name);

  map<llvm::GlobalVariable *, set<llvm::GlobalVariable *>> &func_map =
      file_bb_map
          .try_emplace(
              filename_const,
              map<llvm::GlobalVariable *, set<llvm::GlobalVariable *>>())
          .first->second;

  set<llvm::GlobalVariable *> &bb_set =
      func_map.try_emplace(func_name_const, set<llvm::GlobalVariable *>())
          .first->second;

  map<string, int> bb_name_count = {};

  llvm::FunctionCallee record_bb = Mod_ptr->getOrInsertFunction(
      "__record_bb_cov", voidTy, int8PtrTy, int8PtrTy, int8PtrTy, int32Ty);

  for (llvm::BasicBlock &BB : Func) {
    auto               first_inst = BB.getFirstNonPHIOrDbgOrLifetime();
    llvm::Instruction *first_instr =
        llvm::dyn_cast<llvm::Instruction>(first_inst);

    if (llvm::isa<llvm::LandingPadInst>(first_instr)) { continue; }

    unsigned begin_line_num = -1;
    unsigned end_line_num = 0;

    for (llvm::Instruction &IN : BB) {
      const llvm::DebugLoc &debugInfo = IN.getDebugLoc();
      if (!debugInfo) { continue; }

      const unsigned line_num = debugInfo.getLine();
      if (line_num == 0) { continue; }

      if (line_num < begin_line_num) { begin_line_num = line_num; }
      if (line_num > end_line_num) { end_line_num = line_num; }
    }

    string BB_name = "";
    if (begin_line_num == -1) {
      BB_name = func_name + "_bb_" + to_string(bb_name_count["bb"]++);
    } else {
      BB_name = func_name + "_" + to_string(begin_line_num) + ":" +
                to_string(end_line_num);

      if (bb_name_count.find(BB_name) != bb_name_count.end()) {
        BB_name = BB_name + "_" + to_string(bb_name_count[BB_name]++);
      }
    }

    IRB->SetInsertPoint(first_instr);
    llvm::GlobalVariable *bb_name_const = gen_new_string_constant(BB_name);
    IRB->CreateCall(record_bb, {filename_const, func_name_const, bb_name_const,
                                llvm::ConstantInt::get(int32Ty, bb_id)});
    bb_id++;

    bb_set.insert(bb_name_const);
  }

  llvm::FunctionCallee cov_fini =
      Mod_ptr->getOrInsertFunction("__cov_fini", voidTy);

  // Insert cov fini
  for (llvm::BasicBlock &BB : Func) {
    for (llvm::Instruction &IN : BB) {
      if (!llvm::isa<llvm::CallInst>(IN)) { continue; }

      llvm::CallInst *call_inst = llvm::dyn_cast<llvm::CallInst>(&IN);
      llvm::Function *called_func = call_inst->getCalledFunction();
      if (called_func == NULL) { continue; }

      string called_func_name = called_func->getName().str();
      if (called_func_name != "exit") { continue; }
      IRB->SetInsertPoint(call_inst);
      IRB->CreateCall(cov_fini, {});
    }
  }
  return;
}

void BB_COV_Pass::init_bb_map_rt() {
  llvm::LLVMContext &Ctx = Mod_ptr->getContext();

  llvm::Type       *int8PtrPtrTy = llvm::PointerType::get(int8PtrTy, 0);
  llvm::StructType *FuncBBMapTy =
      llvm::StructType::get(int8PtrTy, int8PtrPtrTy, int32Ty);
  llvm::StructType *FileFuncMapTy = llvm::StructType::get(
      int8PtrTy, llvm::PointerType::get(FuncBBMapTy, 0), int32Ty);

  llvm::Constant *zero = llvm::ConstantInt::get(int32Ty, 0);
  llvm::SmallVector<llvm::Constant *, 2> zero_indices = {zero, zero};

  vector<llvm::Constant *> file_map_entries = {};
  for (auto &iter1 : file_bb_map) {
    llvm::GlobalVariable *filename_const = iter1.first;
    map<llvm::GlobalVariable *, set<llvm::GlobalVariable *>> &func_map =
        iter1.second;

    vector<llvm::Constant *> func_map_entries = {};
    for (auto &iter2 : func_map) {
      llvm::GlobalVariable        *func_name_const = iter2.first;
      set<llvm::GlobalVariable *> &bb_set = iter2.second;

      vector<llvm::Constant *> bb_name_entries = {};
      for (auto bb_name_const : bb_set) {
        bb_name_entries.push_back(bb_name_const);
      }

      const size_t bb_count = bb_name_entries.size();

      llvm::ArrayType *bb_name_array_ty =
          llvm::ArrayType::get(int8PtrTy, bb_count);
      llvm::Constant *bb_name_array_const =
          llvm::ConstantArray::get(bb_name_array_ty, bb_name_entries);

      llvm::GlobalVariable *bb_name_array_global = new llvm::GlobalVariable(
          *Mod_ptr, bb_name_array_ty, true, llvm::GlobalValue::PrivateLinkage,
          bb_name_array_const);

      llvm::Constant *func_map_entry = llvm::ConstantStruct::get(
          FuncBBMapTy, {func_name_const,
                        llvm::ConstantExpr::getPointerCast(bb_name_array_global,
                                                           int8PtrPtrTy),
                        llvm::ConstantInt::get(int32Ty, bb_count)});

      func_map_entries.push_back(func_map_entry);
    }

    const size_t     func_count = func_map_entries.size();
    llvm::ArrayType *func_map_array_ty =
        llvm::ArrayType::get(FuncBBMapTy, func_count);
    llvm::Constant *func_map_array_const =
        llvm::ConstantArray::get(func_map_array_ty, func_map_entries);
    llvm::GlobalVariable *func_map_array_global = new llvm::GlobalVariable(
        *Mod_ptr, func_map_array_ty, true, llvm::GlobalValue::PrivateLinkage,
        func_map_array_const);

    llvm::Constant *file_map_entry = llvm::ConstantStruct::get(
        FileFuncMapTy,
        {filename_const,
         llvm::ConstantExpr::getPointerCast(
             func_map_array_global, llvm::PointerType::get(FuncBBMapTy, 0)),
         llvm::ConstantInt::get(int32Ty, func_count)});
    file_map_entries.push_back(file_map_entry);
  }

  const size_t     file_count = file_map_entries.size();
  llvm::ArrayType *file_map_array_ty =
      llvm::ArrayType::get(FileFuncMapTy, file_count);
  llvm::Constant *file_map_array_const =
      llvm::ConstantArray::get(file_map_array_ty, file_map_entries);

  llvm::GlobalVariable *bb_map_global = new llvm::GlobalVariable(
      *Mod_ptr, file_map_array_ty, true, llvm::GlobalValue::ExternalLinkage,
      file_map_array_const, "__bb_map");

  llvm::GlobalVariable *bb_map_size_global = new llvm::GlobalVariable(
      *Mod_ptr, int32Ty, true, llvm::GlobalValue::ExternalLinkage,
      llvm::ConstantInt::get(int32Ty, file_count), "__bb_map_size");

  return;
}

llvm::GlobalVariable *BB_COV_Pass::gen_new_string_constant(const string &name) {
  auto search = new_string_globals.find(name);

  if (search == new_string_globals.end()) {
    llvm::GlobalVariable *new_global =
        IRB->CreateGlobalString(name, "", 0, Mod_ptr);
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