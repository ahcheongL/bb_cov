
#include "path/path_cov_pass.hpp"

#include <functional>

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Verifier.h"

llvm::PreservedAnalyses Path_COV_Pass::run(llvm::Module                &Module,
                                           llvm::ModuleAnalysisManager &MAM) {
  Mod_ptr = &Module;
  llvm::LLVMContext &Ctx = Module.getContext();
  Ctxt_ptr = &Ctx;

  voidTy = llvm::Type::getVoidTy(Ctx);
  int8Ty = llvm::Type::getInt8Ty(Ctx);
  int32Ty = llvm::Type::getInt32Ty(Ctx);
  int8PtrTy = llvm::PointerType::get(int8Ty, 0);
  int32PtrTy = llvm::PointerType::get(int32Ty, 0);

  IRB = new llvm::IRBuilder<>(Ctx);

  llvm::Function *main_func = Module.getFunction("main");
  if (main_func == NULL) {
    llvm::errs()
        << "[path_cov] main function not found, skipping instrumentation.\n";
    return llvm::PreservedAnalyses::all();
  }

  hash_val_glob = new llvm::GlobalVariable(
      Module, int32Ty, false, llvm::GlobalValue::ExternalLinkage,
      llvm::ConstantInt::get(int32Ty, 1), "__path_hash_val");

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
    instrument_path_cov(Func);
    num_instrumented_funcs++;
  }

  instrument_main(*main_func);

  llvm::GlobalVariable *num_bbs_global = new llvm::GlobalVariable(
      *Mod_ptr, int32Ty, true, llvm::GlobalValue::ExternalLinkage,
      llvm::ConstantInt::get(int32Ty, bb_id), "__num_bbs");

  llvm::outs() << "[path_cov] Instrumented " << num_instrumented_funcs
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

void Path_COV_Pass::instrument_main(llvm::Function &Func) {
  if (Func.arg_size() != 2) {
    llvm::errs()
        << "[path_cov] path_cov pass requires the main function to have 2 "
           "arguments (argc and argv).\n";
    llvm::errs() << "[path_cov] Coverage instrumentation may not work.\n";
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

void Path_COV_Pass::instrument_path_cov(llvm::Function &Func) {
  const string mangled_func_name = Func.getName().str();
  const string func_name = llvm::demangle(mangled_func_name);

  llvm::FunctionCallee record_bb = Mod_ptr->getOrInsertFunction(
      "__record_path_cov", voidTy, int8PtrTy, int8PtrTy, int8PtrTy, int32Ty);

  for (llvm::BasicBlock &BB : Func) {
    auto               first_inst = BB.getFirstNonPHIOrDbgOrLifetime();
    llvm::Instruction *first_instr =
        llvm::dyn_cast<llvm::Instruction>(first_inst);

    if (llvm::isa<llvm::LandingPadInst>(first_instr)) { continue; }

    IRB->SetInsertPoint(first_instr);

    llvm::Value *hash_val = IRB->CreateLoad(int32Ty, hash_val_glob);

    llvm::Value *val1 =
        IRB->CreateBinOp(llvm::Instruction::BinaryOps::Shl, hash_val,
                         llvm::ConstantInt::get(int32Ty, 6));
    llvm::Value *val2 =
        IRB->CreateBinOp(llvm::Instruction::BinaryOps::LShr, hash_val,
                         llvm::ConstantInt::get(int32Ty, 2));
    unsigned int hash_val_int = hash<unsigned int>{}(bb_id) + 0x9e3779b9;

    llvm::Value *val3 =
        IRB->CreateAdd(llvm::ConstantInt::get(int32Ty, hash_val_int), val1);
    val3 = IRB->CreateAdd(val3, val2);
    hash_val = IRB->CreateXor(hash_val, val3);
    IRB->CreateStore(hash_val, hash_val_glob);
    bb_id++;
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

llvm::GlobalVariable *Path_COV_Pass::gen_new_string_constant(
    const string &name) {
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
                  if (Name == "pathcov") {
                    MPM.addPass(Path_COV_Pass());
                    return true;
                  }
                  return false;
                });
          }};
}