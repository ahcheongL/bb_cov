
#include "bb/bb_cov_pass.hpp"

#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/Verifier.h"
#include "utils/hash.hpp"

static llvm::cl::opt<bool> is_verbose_mode(
    "verbose", llvm::cl::desc("enable verbose output"), llvm::cl::init(false));

llvm::PreservedAnalyses BB_COV_Pass::run(llvm::Module                &Module,
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
        << "[bb_cov] main function not found, skipping instrumentation.\n";
    return llvm::PreservedAnalyses::all();
  }

  uint32_t num_instrumented_funcs = insert_bb_probes();

  instrument_main(*main_func);

  init_bb_map_rt();

  llvm::GlobalVariable *num_bbs_global = new llvm::GlobalVariable(
      *Mod_ptr, int32Ty, true, llvm::GlobalValue::ExternalLinkage,
      llvm::ConstantInt::get(int32Ty, bb_id), "__num_bbs");

  if (is_verbose_mode) {
    llvm::outs() << "[bb_cov] Instrumented " << num_instrumented_funcs
                 << " functions.\n";
  }

  delete IRB;
  free_bb_map(file_bb_map);

  std::string              out;
  llvm::raw_string_ostream output(out);
  bool                     has_error = llvm::verifyModule(*Mod_ptr, &output);

  if (has_error) {
    llvm::errs() << "IR errors : \n";
    llvm::errs() << out;
    // Mod_ptr->print(llvm::errs(), nullptr);
    // llvm::errs() << "\n";
  }

  return llvm::PreservedAnalyses::all();
}

void BB_COV_Pass::instrument_main(llvm::Function &Func) {
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

uint32_t BB_COV_Pass::insert_bb_probes() {
  uint32_t num_instrumented_funcs = 0;

  std::set<llvm::Function *> dtor_funcs = get_dtor_funcs();
  const uint32_t             num_dtor_funcs = dtor_funcs.size();
  if ((num_dtor_funcs > 0) && is_verbose_mode) {
    llvm::outs() << "[bb_cov] Found " << dtor_funcs.size()
                 << " dtor functions.\n";
  }

  uint32_t num_func = 0;
  uint32_t num_no_subprogram = 0;

  for (llvm::Function &Func : Mod_ptr->functions()) {
    if (dtor_funcs.find(&Func) != dtor_funcs.end()) {
      if (is_verbose_mode) {
        llvm::outs() << "[bb_cov] Skipping dtor function: " << Func.getName()
                     << "\n";
      }
      continue;
    }

    if (is_probe_func(Func)) { continue; }

    const std::string mangled_func_name = Func.getName().str();
    const std::string func_name = llvm::demangle(mangled_func_name);

    if (Func.isIntrinsic()) { continue; }
    if (Func.isDeclaration()) { continue; }

    if (func_name.find("_GLOBAL__sub_I_") != std::string::npos) { continue; }
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
    std::string           filename = subp->getFilename().str();

    if (dirname != "") { filename = std::string(dirname) + "/" + filename; }

    if (filename.find("/usr") != std::string::npos) { continue; }
    // normal functions under test
    insert_bb_probe_one_func(Func, filename);
    num_instrumented_funcs++;
  }

  if ((num_no_subprogram / (float)num_func) > 0.7) {
    llvm::errs()
        << "[bb_cov] Warning: " << num_no_subprogram << " / " << num_func
        << " (" << (num_no_subprogram / (float)num_func) * 100
        << "%) of functions have no debug info. "
           "The program may not be instrumented with debug information.\n";
  }

  return num_instrumented_funcs;
}

void BB_COV_Pass::insert_bb_probe_one_func(llvm::Function    &Func,
                                           const std::string &filename) {
  const std::string mangled_func_name = Func.getName().str();
  const std::string func_name = llvm::demangle(mangled_func_name);

  llvm::GlobalVariable *filename_const = gen_new_string_constant(filename);
  llvm::GlobalVariable *func_name_const = gen_new_string_constant(func_name);

  GFuncEntry *func_entry = insert_FileFuncEntry(
      file_bb_map, filename, filename_const, func_name, func_name_const);

  std::map<std::string, uint32_t> bb_name_count = {};

  llvm::FunctionCallee record_bb = Mod_ptr->getOrInsertFunction(
      "__record_bb_cov", voidTy, int8PtrTy, int8PtrTy, int8PtrTy, int32Ty);

  for (llvm::BasicBlock &BB : Func) {
    auto               first_inst = BB.getFirstNonPHIOrDbgOrLifetime();
    llvm::Instruction *first_instr =
        llvm::dyn_cast<llvm::Instruction>(first_inst);

    if (llvm::isa<llvm::LandingPadInst>(first_instr)) { continue; }
    if (is_probe_BB(BB)) { continue; }

    uint32_t begin_line_num = -1;
    uint32_t end_line_num = 0;

    for (llvm::Instruction &IN : BB) {
      const llvm::DebugLoc &debugInfo = IN.getDebugLoc();
      if (!debugInfo) { continue; }

      const uint32_t line_num = debugInfo.getLine();
      if (line_num == 0) { continue; }

      if (line_num < begin_line_num) { begin_line_num = line_num; }
      if (line_num > end_line_num) { end_line_num = line_num; }
    }

    std::string BB_name = "";
    if (begin_line_num == -1) {
      BB_name = "bb_" + std::to_string(bb_name_count["bb"]++);
    } else {
      BB_name =
          std::to_string(begin_line_num) + ":" + std::to_string(end_line_num);

      if (bb_name_count.find(BB_name) != bb_name_count.end()) {
        uint32_t index = bb_name_count[BB_name];
        bb_name_count[BB_name] = index + 1;
        BB_name = BB_name + "_" + std::to_string(index);
      } else {
        bb_name_count[BB_name] = 1;
      }
    }

    IRB->SetInsertPoint(first_instr);
    llvm::GlobalVariable *bb_name_const = gen_new_string_constant(BB_name);
    IRB->CreateCall(record_bb, {filename_const, func_name_const, bb_name_const,
                                llvm::ConstantInt::get(int32Ty, bb_id)});
    bb_id++;

    insert_BBEntry(func_entry, BB_name, bb_name_const);
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

llvm::GlobalVariable *BB_COV_Pass::gen_cfile_entry(GFileEntry *file_entry) {
  const uint32_t     hash_map_size = sizeof(unsigned char) * 256;
  llvm::PointerType *cfileEntryPtrTy = llvm::PointerType::get(cfileEntryTy, 0);
  llvm::Constant    *cfileEntry_null =
      llvm::ConstantPointerNull::get(cfileEntryPtrTy);

  llvm::PointerType *cfuncEntryPtrTy = llvm::PointerType::get(cfuncEntryTy, 0);
  llvm::Constant    *cfuncEntry_null =
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
    if (prev_file_entry != NULL) { prev_val = prev_file_entry; }

    llvm::Constant *new_cfile_val = llvm::ConstantStruct::get(
        cfileEntryTy, {cfuncs_val, file_entry->file_gvar, prev_val});

    llvm::GlobalVariable *new_cfile_entry = new llvm::GlobalVariable(
        *Mod_ptr, cfileEntryTy, false, llvm::GlobalValue::PrivateLinkage,
        new_cfile_val);
    prev_file_entry = new_cfile_entry;
  }
  return prev_file_entry;
}

llvm::GlobalVariable *BB_COV_Pass::gen_cfunc_entry(GFuncEntry *func_entry) {
  const uint32_t hash_map_size = sizeof(unsigned char) * 256;

  llvm::PointerType *cfuncEntryPtrTy = llvm::PointerType::get(cfuncEntryTy, 0);
  llvm::Constant    *cfuncEntry_null =
      llvm::ConstantPointerNull::get(cfuncEntryPtrTy);

  llvm::PointerType *cbbEntryPtrTy = llvm::PointerType::get(cbbEntryTy, 0);
  llvm::Constant *cbbEntry_null = llvm::ConstantPointerNull::get(cbbEntryPtrTy);

  llvm::ArrayType *cbbEntryPtrArrayTy =
      llvm::ArrayType::get(cbbEntryPtrTy, hash_map_size);

  std::vector<GFuncEntry *> cur_func_entries = {};
  while (func_entry != NULL) {
    cur_func_entries.push_back(func_entry);
    func_entry = func_entry->next;
  }
  reverse(cur_func_entries.begin(), cur_func_entries.end());

  llvm::GlobalVariable *prev_func_entry = NULL;
  for (GFuncEntry *func_entry : cur_func_entries) {
    std::vector<llvm::Constant *> cbb_entries = {};
    for (size_t bb_idx = 0; bb_idx < hash_map_size; bb_idx++) {
      GBBEntry *bb_entry = func_entry->bbs[bb_idx];
      if (bb_entry == NULL) {
        cbb_entries.push_back(cbbEntry_null);
        continue;
      }
      llvm::GlobalVariable *new_cbb_entry = gen_cbb_entry(bb_entry);
      cbb_entries.push_back(new_cbb_entry);
    }

    llvm::Constant *cbb_val =
        llvm::ConstantArray::get(cbbEntryPtrArrayTy, cbb_entries);

    llvm::Constant *prev_val = cfuncEntry_null;
    if (prev_func_entry != NULL) { prev_val = prev_func_entry; }
    llvm::Constant *new_cfunc_val = llvm::ConstantStruct::get(
        cfuncEntryTy, {cbb_val, func_entry->func_gvar, prev_val});
    llvm::GlobalVariable *new_cfunc_entry = new llvm::GlobalVariable(
        *Mod_ptr, cfuncEntryTy, false, llvm::GlobalValue::PrivateLinkage,
        new_cfunc_val);
    prev_func_entry = new_cfunc_entry;
  }

  return prev_func_entry;
}

llvm::GlobalVariable *BB_COV_Pass::gen_cbb_entry(GBBEntry *bb_entry) {
  llvm::PointerType *cbbEntryPtrTy = llvm::PointerType::get(cbbEntryTy, 0);
  llvm::Constant *cbbEntry_null = llvm::ConstantPointerNull::get(cbbEntryPtrTy);
  llvm::Constant *zero_val = llvm::ConstantInt::get(int8Ty, 0);

  std::vector<GBBEntry *> cur_bb_entries = {};
  while (bb_entry != NULL) {
    cur_bb_entries.push_back(bb_entry);
    bb_entry = bb_entry->next;
  }

  std::reverse(cur_bb_entries.begin(), cur_bb_entries.end());
  llvm::GlobalVariable *prev_bb_entry = NULL;
  for (GBBEntry *bb_entry : cur_bb_entries) {
    llvm::Constant *prev_val = cbbEntry_null;
    if (prev_bb_entry != NULL) { prev_val = prev_bb_entry; }
    llvm::Constant *new_cbb_val = llvm::ConstantStruct::get(
        cbbEntryTy, {bb_entry->bb_gvar, prev_val, zero_val});
    llvm::GlobalVariable *new_cbb_entry = new llvm::GlobalVariable(
        *Mod_ptr, cbbEntryTy, false, llvm::GlobalValue::PrivateLinkage,
        new_cbb_val);
    prev_bb_entry = new_cbb_entry;
  }

  return prev_bb_entry;
}

void BB_COV_Pass::init_bb_map_rt() {
  llvm::LLVMContext &Ctx = *Ctxt_ptr;

  llvm::Type *int8PtrPtrTy = llvm::PointerType::get(int8PtrTy, 0);

  const uint32_t hash_map_size = sizeof(unsigned char) * 256;

  cbbEntryTy = llvm::StructType::create(Ctx, "struct.CBBEntry");
  llvm::PointerType *cbbEntryPtrTy = llvm::PointerType::get(cbbEntryTy, 0);

  cbbEntryTy->setBody({int8PtrTy, cbbEntryPtrTy, int8Ty});

  cfuncEntryTy = llvm::StructType::create(Ctx, "struct.CFuncEntry");
  llvm::PointerType *cfuncEntryPtrTy = llvm::PointerType::get(cfuncEntryTy, 0);

  cfuncEntryTy->setBody({llvm::ArrayType::get(cbbEntryPtrTy, hash_map_size),
                         int8PtrTy, cfuncEntryPtrTy});

  cfileEntryTy = llvm::StructType::create(Ctx, "struct.CFileEntry");
  llvm::PointerType *cfileEntryPtrTy = llvm::PointerType::get(cfileEntryTy, 0);

  cfileEntryTy->setBody({llvm::ArrayType::get(cfuncEntryPtrTy, hash_map_size),
                         int8PtrTy, cfileEntryPtrTy});

  llvm::Constant *null_cfile_entry =
      llvm::ConstantPointerNull::get(cfileEntryPtrTy);

  std::vector<llvm::Constant *> file_map_entries = {};

  for (uint32_t file_idx = 0; file_idx < hash_map_size; file_idx++) {
    GFileEntry *file_entry = file_bb_map[file_idx];
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

llvm::GlobalVariable *BB_COV_Pass::gen_new_string_constant(
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

std::set<llvm::Function *> BB_COV_Pass::get_dtor_funcs() {
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

bool BB_COV_Pass::is_probe_func(llvm::Function &Func) {
  if (!Func.hasFnAttribute("annotate")) { return false; }

  llvm::StringRef val = Func.getFnAttribute("annotate").getValueAsString();
  if (val == "probe_function") { return true; }
  return false;
}

bool BB_COV_Pass::is_probe_BB(llvm::BasicBlock &BB) {
  llvm::MDNode *Node = BB.getTerminator()->getMetadata("is_probe");
  return (Node != nullptr);
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