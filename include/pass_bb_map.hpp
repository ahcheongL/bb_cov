#include <llvm/IR/GlobalVariable.h>

#include <string>

struct GBBEntry {
  llvm::GlobalVariable *bb_gvar;
  struct GBBEntry      *next;
};

struct GFuncEntry {
  struct GBBEntry      *bbs[sizeof(unsigned char) * 256];
  llvm::GlobalVariable *func_gvar;
  struct GFuncEntry    *next;
};

struct GFileEntry {
  struct GFuncEntry    *funcs[sizeof(unsigned char) * 256];
  llvm::GlobalVariable *file_gvar;
  struct GFileEntry    *next;
};

GFuncEntry *insert_FileFuncEntry(GFileEntry          **file_map,
                                 const std::string    &filename,
                                 llvm::GlobalVariable *file_gvar,
                                 const std::string    &func_name,
                                 llvm::GlobalVariable *func_gvar);

GFileEntry *insert_FileEntry(GFileEntry **file_map, const std::string &filename,
                             llvm::GlobalVariable *file_gvar);

GBBEntry *insert_BBEntry(GFuncEntry *func_entry, const std::string &bb_name,
                         llvm::GlobalVariable *bb_gvar);

void free_bb_map(GFileEntry **file_map);