#include "func/func_cov_pass.hpp"

#include "utils/hash.hpp"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Verifier.h"
#include "llvm/Passes/PassBuilder.h"
#include "llvm/Passes/PassPlugin.h"

#include "llvm/Support/CommandLine.h"

static llvm::cl::opt<bool>
    is_verbose_mode("verbose", llvm::cl::desc("enable verbose output"),
                    llvm::cl::init(false));

llvm::PreservedAnalyses FUNC_COV_Pass::run(llvm::Module &Module,
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
        << "[func_cov] main function not found, skipping instrumentation.\n";
    return llvm::PreservedAnalyses::all();
  }

  insert_func_probes();

  instrument_main(*main_func);

  init_func_map_rt();

  llvm::GlobalVariable *num_funcs_global = new llvm::GlobalVariable(
      *Mod_ptr, int32Ty, true, llvm::GlobalValue::ExternalLinkage,
      llvm::ConstantInt::get(int32Ty, func_id), "__num_funcs");

  if (is_verbose_mode) {
    llvm::outs() << "[func_cov] Instrumented " << func_id << " functions.\n";
  }

  delete IRB;
  free_func_map(file_func_map);

  std::string out;
  llvm::raw_string_ostream output(out);
  bool has_error = llvm::verifyModule(*Mod_ptr, &output);

  if (has_error) {
    llvm::errs() << "IR errors : \n";
    llvm::errs() << out;
    // Mod_ptr->print(llvm::errs(), nullptr);
    // llvm::errs() << "\n";
  }

  return llvm::PreservedAnalyses::all();
}

void FUNC_COV_Pass::instrument_main(llvm::Function &Func) {
  llvm::Type *int8PtrPtrTy = llvm::PointerType::get(int8PtrTy, 0);

  llvm::Function *main_func = &Func;

  if (main_func->arg_size() != 2) {
    llvm::FunctionType *new_main_func_type =
        llvm::FunctionType::get(int32Ty, {int32Ty, int8PtrPtrTy}, false);
    llvm::Function *new_main = llvm::Function::Create(
        new_main_func_type, main_func->getLinkage(), "main_new", Mod_ptr);
    new_main->takeName(main_func);

    auto insert_pt = new_main->begin();
    new_main->splice(insert_pt, main_func);

    main_func->replaceAllUsesWith(new_main);
    main_func->eraseFromParent();

    main_func = new_main;
  }

  auto first_inst = main_func->getEntryBlock().getFirstNonPHIOrDbgOrLifetime();
  IRB->SetInsertPoint(first_inst);

  llvm::Value *const argc = main_func->getArg(0);
  llvm::Value *const argv = main_func->getArg(1);

  llvm::AllocaInst *const argc_ptr = IRB->CreateAlloca(int32Ty);

  llvm::Instruction *const new_argc = IRB->CreateLoad(int32Ty, argc_ptr);

  argc->replaceAllUsesWith(new_argc);

  IRB->SetInsertPoint(new_argc);

  IRB->CreateStore(argc, argc_ptr);

  llvm::FunctionCallee get_output_fn = Mod_ptr->getOrInsertFunction(
      "__handle_init", voidTy, int32PtrTy, int8PtrPtrTy);

  IRB->CreateCall(get_output_fn, {argc_ptr, argv});
  std::set<llvm::ReturnInst *> ret_inst_set;
  for (auto &BB : *main_func) {
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

void FUNC_COV_Pass::insert_func_probes() {
  std::set<llvm::Function *> dtor_funcs = get_dtor_funcs();
  const uint32_t num_dtor_funcs = dtor_funcs.size();
  if ((num_dtor_funcs > 0) && is_verbose_mode) {
    llvm::outs() << "[func_cov] Found " << dtor_funcs.size()
                 << " dtor functions.\n";
  }

  uint32_t num_func = 0;
  uint32_t num_no_subprogram = 0;

  for (llvm::Function &Func : Mod_ptr->functions()) {
    if (dtor_funcs.find(&Func) != dtor_funcs.end()) {
      if (is_verbose_mode) {
        llvm::outs() << "[func_cov] Skipping dtor function: " << Func.getName()
                     << "\n";
      }
      continue;
    }

    if (is_probe_func(Func)) {
      continue;
    }

    const std::string mangled_func_name = Func.getName().str();
    const std::string func_name = llvm::demangle(mangled_func_name);

    if (Func.isIntrinsic()) {
      continue;
    }
    if (Func.isDeclaration()) {
      continue;
    }

    if (func_name.find("_GLOBAL__sub_I_") != std::string::npos) {
      continue;
    }
    if (func_name.find("__cxx_global_var_init") != std::string::npos) {
      continue;
    }

    num_func++;

    auto subp = Func.getSubprogram();
    if (subp == NULL) {
      num_no_subprogram++;
      continue;
    }

    const llvm::StringRef dirname = subp->getDirectory();
    std::string filename = subp->getFilename().str();

    if (dirname != "") {
      filename = std::string(dirname) + "/" + filename;
    }

    if (filename.find("/usr") != std::string::npos) {
      continue;
    }

    // normal functions under test
    insert_func_probe_one_func(Func, filename);
  }

  if ((num_no_subprogram / (float)num_func) > 0.7) {
    llvm::errs()
        << "[bb_cov] Warning: " << num_no_subprogram << " / " << num_func
        << " (" << (num_no_subprogram / (float)num_func) * 100
        << "%) of functions have no debug info. "
           "The program may not be instrumented with debug information.\n";
  }

  return;
}

void FUNC_COV_Pass::insert_func_probe_one_func(llvm::Function &Func,
                                               const std::string &filename) {
  std::string func_name = llvm::demangle(Func.getName().str());
  if (func_name.find(".") != std::string::npos) {
    func_name = func_name.substr(0, func_name.find("."));
  }

  llvm::GlobalVariable *filename_const = gen_new_string_constant(filename);
  llvm::GlobalVariable *func_name_const = gen_new_string_constant(func_name);

  GFuncEntry *func_entry = insert_FileFuncEntry(
      file_func_map, filename, filename_const, func_name, func_name_const);

  llvm::FunctionCallee record_func = Mod_ptr->getOrInsertFunction(
      "__record_func_cov", voidTy, int8PtrTy, int8PtrTy, int32Ty);

  llvm::BasicBlock &entry_bb = Func.getEntryBlock();

  IRB->SetInsertPoint(entry_bb.getFirstNonPHIOrDbgOrLifetime());

  IRB->CreateCall(record_func, {filename_const, func_name_const,
                                llvm::ConstantInt::get(int32Ty, func_id)});

  func_id++;

  llvm::FunctionCallee cov_fini =
      Mod_ptr->getOrInsertFunction("__cov_fini", voidTy);

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

      std::string called_func_name = called_func->getName().str();
      if (called_func_name != "exit") {
        continue;
      }
      IRB->SetInsertPoint(call_inst);
      IRB->CreateCall(cov_fini, {});
    }
  }
  return;
}

llvm::GlobalVariable *FUNC_COV_Pass::gen_cfile_entry(GFileEntry *file_entry) {
  const uint32_t hash_map_size = sizeof(unsigned char) * 256;
  llvm::PointerType *cfileEntryPtrTy = llvm::PointerType::get(cfileEntryTy, 0);
  llvm::Constant *cfileEntry_null =
      llvm::ConstantPointerNull::get(cfileEntryPtrTy);

  llvm::PointerType *cfuncEntryPtrTy = llvm::PointerType::get(cfuncEntryTy, 0);
  llvm::Constant *cfuncEntry_null =
      llvm::ConstantPointerNull::get(cfuncEntryPtrTy);

  llvm::ArrayType *cfuncEntryPtrArrayTy =
      llvm::ArrayType::get(cfuncEntryPtrTy, hash_map_size);

  std::vector<GFileEntry *> cur_file_entries = {};
  while (file_entry != NULL) {
    cur_file_entries.push_back(file_entry);
    file_entry = file_entry->next;
  }

  std::reverse(cur_file_entries.begin(), cur_file_entries.end());

  llvm::GlobalVariable *prev_file_entry = NULL;
  for (GFileEntry *file_entry : cur_file_entries) {
    std::vector<llvm::Constant *> cfunc_entries = {};

    for (size_t func_idx = 0; func_idx < hash_map_size; func_idx++) {
      GFuncEntry *func_entry = file_entry->funcs[func_idx];
      if (func_entry == NULL) {
        cfunc_entries.push_back(cfuncEntry_null);
        continue;
      }
      llvm::GlobalVariable *new_cfunc_entry = gen_cfunc_entry(func_entry);
      cfunc_entries.push_back(new_cfunc_entry);
    }

    llvm::Constant *cfuncs_val =
        llvm::ConstantArray::get(cfuncEntryPtrArrayTy, cfunc_entries);

    llvm::Constant *prev_val = cfileEntry_null;
    if (prev_file_entry != NULL) {
      prev_val = prev_file_entry;
    }

    llvm::Constant *new_cfile_val = llvm::ConstantStruct::get(
        cfileEntryTy, {cfuncs_val, file_entry->file_gvar, prev_val});

    llvm::GlobalVariable *new_cfile_entry = new llvm::GlobalVariable(
        *Mod_ptr, cfileEntryTy, false, llvm::GlobalValue::PrivateLinkage,
        new_cfile_val);
    prev_file_entry = new_cfile_entry;
  }
  return prev_file_entry;
}

llvm::GlobalVariable *FUNC_COV_Pass::gen_cfunc_entry(GFuncEntry *func_entry) {
  const uint32_t hash_map_size = sizeof(unsigned char) * 256;

  llvm::PointerType *cfuncEntryPtrTy = llvm::PointerType::get(cfuncEntryTy, 0);
  llvm::Constant *cfuncEntry_null =
      llvm::ConstantPointerNull::get(cfuncEntryPtrTy);

  std::vector<GFuncEntry *> cur_func_entries = {};
  while (func_entry != NULL) {
    cur_func_entries.push_back(func_entry);
    func_entry = func_entry->next;
  }
  reverse(cur_func_entries.begin(), cur_func_entries.end());

  llvm::GlobalVariable *prev_func_entry = NULL;
  for (GFuncEntry *func_entry : cur_func_entries) {
    llvm::Constant *prev_val = cfuncEntry_null;
    if (prev_func_entry != NULL) {
      prev_val = prev_func_entry;
    }
    llvm::Constant *new_cfunc_val = llvm::ConstantStruct::get(
        cfuncEntryTy,
        {func_entry->func_gvar, prev_val, llvm::ConstantInt::get(int8Ty, 0)});
    llvm::GlobalVariable *new_cfunc_entry = new llvm::GlobalVariable(
        *Mod_ptr, cfuncEntryTy, false, llvm::GlobalValue::PrivateLinkage,
        new_cfunc_val);
    prev_func_entry = new_cfunc_entry;
  }

  return prev_func_entry;
}

void FUNC_COV_Pass::init_func_map_rt() {
  llvm::LLVMContext &Ctx = *Ctxt_ptr;

  llvm::Type *int8PtrPtrTy = llvm::PointerType::get(int8PtrTy, 0);

  const uint32_t hash_map_size = sizeof(unsigned char) * 256;

  cfuncEntryTy = llvm::StructType::create(Ctx, "struct.CFuncEntry");
  llvm::PointerType *cfuncEntryPtrTy = llvm::PointerType::get(cfuncEntryTy, 0);

  cfuncEntryTy->setBody({int8PtrTy, cfuncEntryPtrTy, int8Ty});

  cfileEntryTy = llvm::StructType::create(Ctx, "struct.CFileEntry");
  llvm::PointerType *cfileEntryPtrTy = llvm::PointerType::get(cfileEntryTy, 0);

  cfileEntryTy->setBody({llvm::ArrayType::get(cfuncEntryPtrTy, hash_map_size),
                         int8PtrTy, cfileEntryPtrTy});

  llvm::Constant *null_cfile_entry =
      llvm::ConstantPointerNull::get(cfileEntryPtrTy);

  std::vector<llvm::Constant *> file_map_entries = {};

  for (uint32_t file_idx = 0; file_idx < hash_map_size; file_idx++) {
    GFileEntry *file_entry = file_func_map[file_idx];
    if (file_entry == NULL) {
      file_map_entries.push_back(null_cfile_entry);
      continue;
    }

    llvm::GlobalVariable *new_cfile_entry = gen_cfile_entry(file_entry);
    file_map_entries.push_back(new_cfile_entry);
  }

  llvm::ArrayType *cfileptr_array_ty =
      llvm::ArrayType::get(cfileEntryPtrTy, hash_map_size);
  llvm::Constant *cfileptr_arr_val =
      llvm::ConstantArray::get(cfileptr_array_ty, file_map_entries);

  llvm::GlobalVariable *__file_func_map_global = new llvm::GlobalVariable(
      *Mod_ptr, cfileptr_array_ty, false, llvm::GlobalValue::ExternalLinkage,
      cfileptr_arr_val, "__file_func_map");

  return;
}

llvm::GlobalVariable *
FUNC_COV_Pass::gen_new_string_constant(const std::string &name) {
  auto search = new_string_globals.find(name);

  if (search == new_string_globals.end()) {
    llvm::GlobalVariable *new_global =
        IRB->CreateGlobalString(name, "", 0, Mod_ptr);
    new_string_globals.insert(make_pair(name, new_global));
    return new_global;
  }

  return search->second;
}

std::set<llvm::Function *> FUNC_COV_Pass::get_dtor_funcs() {
  std::set<llvm::Function *> dtor_funcs = {};

  llvm::GlobalVariable *global_dtors =
      Mod_ptr->getGlobalVariable("llvm.global_dtors");

  if (global_dtors == nullptr) {
    return dtor_funcs;
  }

  // 2. get llvm.global_dtors' initializer
  llvm::Constant *initializer = global_dtors->getInitializer();

  llvm::ConstantArray *const_arr =
      llvm::dyn_cast<llvm::ConstantArray>(initializer);

  if (const_arr == nullptr) {
    return dtor_funcs;
  }

  for (llvm::Use &op : const_arr->operands()) {
    llvm::ConstantStruct *op_val = llvm::dyn_cast<llvm::ConstantStruct>(op);
    if (op_val == nullptr) {
      continue;
    }

    llvm::Constant *dtor_func = op_val->getOperand(1);

    llvm::Function *func = llvm::dyn_cast<llvm::Function>(dtor_func);
    if (func == nullptr) {
      continue;
    }

    dtor_funcs.insert(func);
  }

  return dtor_funcs;
}

bool FUNC_COV_Pass::is_probe_func(llvm::Function &Func) {
  if (!Func.hasFnAttribute("annotate")) {
    return false;
  }

  llvm::StringRef val = Func.getFnAttribute("annotate").getValueAsString();
  if (val == "probe_function") {
    return true;
  }
  return false;
}

extern "C" ::llvm::PassPluginLibraryInfo llvmGetPassPluginInfo() {
  return {LLVM_PLUGIN_API_VERSION, // Plugin API version
          "FuncCovPassPlugin",     // Plugin name
          LLVM_VERSION_STRING,     // LLVM version
          [](llvm::PassBuilder &PB) {
            // Register module-level pass
            PB.registerPipelineParsingCallback(
                [](llvm::StringRef Name, llvm::ModulePassManager &MPM,
                   llvm::ArrayRef<llvm::PassBuilder::PipelineElement>) {
                  if (Name == "funccov") {
                    MPM.addPass(FUNC_COV_Pass());
                    return true;
                  }
                  return false;
                });
          }};
}