
#include "func/func_seq_pass.hpp"

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Verifier.h"
#include "utils/hash.hpp"

llvm::PreservedAnalyses FuncSeqPass::run(llvm::Module                &Module,
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
        << "[func_seq] main function not found, skipping instrumentation.\n";
    return llvm::PreservedAnalyses::all();
  }

  uint32_t num_instrumented_funcs = insert_func_probes();

  instrument_main(*main_func);

  llvm::outs() << "[func_seq] Instrumented " << num_instrumented_funcs
               << " functions.\n";

  delete IRB;

  std::string              out;
  llvm::raw_string_ostream output(out);
  bool                     has_error = llvm::verifyModule(*Mod_ptr, &output);

  if (has_error > 0) {
    llvm::outs() << "IR errors : \n";
    llvm::outs() << out;
    // Mod_ptr->print(llvm::outs(), nullptr);
    // llvm::outs() << "\n";
  }

  return llvm::PreservedAnalyses::all();
}

void FuncSeqPass::instrument_main(llvm::Function &Func) {
  if (Func.arg_size() != 2) {
    llvm::errs()
        << "[func_seq] func_seq pass requires the main function to have 2 "
           "arguments (argc and argv).\n";
    llvm::errs() << "[func_seq] Coverage instrumentation may not work.\n";
    return;
  }

  auto first_inst = Func.getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
  IRB->SetInsertPoint(first_inst);

  llvm::Value *const argc = Func.getArg(0);
  llvm::Value *const argv = Func.getArg(1);

  llvm::Type *int8PtrPtrTy = llvm::PointerType::get(int8PtrTy, 0);

  llvm::AllocaInst *const argc_ptr = IRB->CreateAlloca(int32Ty);

  llvm::Instruction *const new_argc = IRB->CreateLoad(int32Ty, argc_ptr);

  argc->replaceAllUsesWith(new_argc);

  IRB->SetInsertPoint(new_argc);

  IRB->CreateStore(argc, argc_ptr);

  llvm::FunctionCallee get_output_fn = Mod_ptr->getOrInsertFunction(
      "__handle_init", voidTy, int32PtrTy, int8PtrPtrTy);

  IRB->CreateCall(get_output_fn, {argc_ptr, argv});
  std::set<llvm::ReturnInst *> ret_inst_set;
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

uint32_t FuncSeqPass::insert_func_probes() {
  uint32_t num_instrumented_funcs = 0;

  std::set<llvm::Function *> dtor_funcs = get_dtor_funcs();
  const uint32_t             num_dtor_funcs = dtor_funcs.size();
  if (num_dtor_funcs > 0) {
    llvm::outs() << "[func_seq] Found " << dtor_funcs.size()
                 << " dtor functions.\n";
  }

  const uint32_t num_func = Mod_ptr->getFunctionList().size();
  uint32_t       num_no_subprogram = 0;

  for (llvm::Function &Func : Mod_ptr->functions()) {
    if (dtor_funcs.find(&Func) != dtor_funcs.end()) {
      llvm::outs() << "[func_seq] Skipping dtor function: " << Func.getName()
                   << "\n";
      continue;
    }

    const std::string mangled_func_name = Func.getName().str();
    const std::string func_name = llvm::demangle(mangled_func_name);

    if (Func.isIntrinsic()) { continue; }

    if (func_name.find("_GLOBAL__sub_I_") != std::string::npos) { continue; }
    if (func_name.find("__cxx_global_var_init") != std::string::npos) {
      continue;
    }

    auto subp = Func.getSubprogram();
    if (subp == NULL) {
      num_no_subprogram++;
      continue;
    }

    const llvm::StringRef dirname = subp->getDirectory();
    if (dirname.find("/usr") != std::string::npos) { continue; }

    const std::string filename = subp->getFilename().str();

    // normal functions under test
    insert_func_probe_one_func(Func, filename);
    num_instrumented_funcs++;
  }

  if (num_no_subprogram / (float)num_func > 0.5) {
    llvm::outs()
        << "[func_seq] Warning: More than 50% of functions have no debug info. "
           "The program may not be instrumented with debug information.\n";
  }

  return num_instrumented_funcs;
}

void FuncSeqPass::insert_func_probe_one_func(llvm::Function    &Func,
                                             const std::string &filename) {
  const std::string mangled_func_name = Func.getName().str();
  const std::string func_name = llvm::demangle(mangled_func_name);

  llvm::GlobalVariable *filename_const = gen_new_string_constant(filename);
  llvm::GlobalVariable *func_name_const = gen_new_string_constant(func_name);

  llvm::FunctionCallee record_func_entry = Mod_ptr->getOrInsertFunction(
      "__record_func_entry", voidTy, int8PtrTy, int8PtrTy);

  llvm::FunctionCallee record_func_ret = Mod_ptr->getOrInsertFunction(
      "__record_func_ret", voidTy, int8PtrTy, int8PtrTy);

  // Insert function entry probe
  llvm::BasicBlock &entry_bb = Func.getEntryBlock();
  auto              first_inst = entry_bb.getFirstNonPHIOrDbgOrLifetime();
  IRB->SetInsertPoint(first_inst);
  IRB->CreateCall(record_func_entry, {filename_const, func_name_const});

  // Insert function return probes
  std::vector<llvm::ReturnInst *> ret_insts = {};
  for (llvm::BasicBlock &BB : Func) {
    for (llvm::Instruction &IN : BB) {
      if (llvm::ReturnInst *ret_inst = llvm::dyn_cast<llvm::ReturnInst>(&IN)) {
        ret_insts.push_back(ret_inst);
      }
    }
  }

  for (llvm::ReturnInst *ret_inst : ret_insts) {
    IRB->SetInsertPoint(ret_inst);
    IRB->CreateCall(record_func_ret, {filename_const, func_name_const});
  }

  // Insert libc function call probes
  std::vector<llvm::CallInst *> call_insts = {};
  for (llvm::BasicBlock &BB : Func) {
    for (llvm::Instruction &IN : BB) {
      llvm::CallInst *call_inst = llvm::dyn_cast<llvm::CallInst>(&IN);
      if (call_inst == NULL) { continue; }

      llvm::Function *called_func = call_inst->getCalledFunction();
      if (called_func == NULL) { continue; }

      std::string called_func_name = called_func->getName().str();
      if (called_func_name == "fread") {
        llvm::outs() << "Found fread call in function: " << func_name << "\n";
      }

      if (!called_func->isDeclaration()) { continue; }

      if (called_func->isIntrinsic()) {
        llvm::outs() << "Skipping intrinsic call: " << called_func_name << "\n";
        continue;
      }

      call_insts.push_back(call_inst);
    }
  }

  llvm::FunctionCallee record_func_external =
      Mod_ptr->getOrInsertFunction("__record_func_external", voidTy, int8PtrTy);

  for (llvm::CallInst *call_inst : call_insts) {
    IRB->SetInsertPoint(call_inst);
    llvm::Function *called_func = call_inst->getCalledFunction();
    llvm::StringRef called_func_name_ref = called_func->getName();

    std::string called_func_name = called_func_name_ref.str();

    if (called_func_name == "fread") {
      llvm::outs() << "Inserting external probe for fread call in function: "
                   << func_name << "\n";
    }

    if (called_func_name.find("__record_func") != std::string::npos) {
      continue;
    }

    llvm::GlobalVariable *func_name_const =
        gen_new_string_constant(called_func_name);

    IRB->CreateCall(record_func_external, {func_name_const});
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

      std::string called_func_name = called_func->getName().str();
      if (called_func_name != "exit") { continue; }
      IRB->SetInsertPoint(call_inst);
      IRB->CreateCall(cov_fini, {});
    }
  }
  return;
}

llvm::GlobalVariable *FuncSeqPass::gen_new_string_constant(
    const std::string &name) {
  auto search = new_string_globals.find(name);

  if (search == new_string_globals.end()) {
    llvm::GlobalVariable *new_global =
        IRB->CreateGlobalString(name, "", 0, Mod_ptr);
    new_string_globals.insert(make_pair(name, new_global));
    return new_global;
  }

  return search->second;
}

std::set<llvm::Function *> FuncSeqPass::get_dtor_funcs() {
  std::set<llvm::Function *> dtor_funcs = {};

  llvm::GlobalVariable *global_dtors =
      Mod_ptr->getGlobalVariable("llvm.global_dtors");

  if (global_dtors == nullptr) { return dtor_funcs; }

  // 2. get llvm.global_dtors' initializer
  llvm::Constant *initializer = global_dtors->getInitializer();

  llvm::ConstantArray *const_arr =
      llvm::dyn_cast<llvm::ConstantArray>(initializer);

  if (const_arr == nullptr) { return dtor_funcs; }

  for (llvm::Use &op : const_arr->operands()) {
    llvm::ConstantStruct *op_val = llvm::dyn_cast<llvm::ConstantStruct>(op);

    if (op_val == nullptr) { continue; }

    llvm::Constant *dtor_func = op_val->getOperand(1);

    llvm::Function *func = llvm::dyn_cast<llvm::Function>(dtor_func);
    if (func == nullptr) { continue; }

    dtor_funcs.insert(func);
  }

  return dtor_funcs;
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
                  if (Name == "funcseq") {
                    MPM.addPass(FuncSeqPass());
                    return true;
                  }
                  return false;
                });
          }};
}