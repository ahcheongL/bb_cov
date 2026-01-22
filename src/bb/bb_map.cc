#include "bb/bb_map.hpp"

#include <string.h>

#include "utils/hash.hpp"

#pragma clang attribute push(__attribute__((annotate("probe_function"))), \
                             apply_to = function)

GFuncEntry *insert_FileFuncEntry(GFileEntry          **file_map,
                                 const std::string    &filename,
                                 llvm::GlobalVariable *file_gvar,
                                 const std::string    &func_name,
                                 llvm::GlobalVariable *func_gvar) {
  GFileEntry *file_entry = insert_FileEntry(file_map, filename, file_gvar);

  uint8_t func_name_hash = simple_hash(func_name);

  if (file_entry->funcs[func_name_hash] == nullptr) {
    GFuncEntry *new_entry = new GFuncEntry();
    memset(new_entry, 0, sizeof(GFuncEntry));
    new_entry->func_gvar = func_gvar;
    file_entry->funcs[func_name_hash] = new_entry;
    return new_entry;
  }

  GFuncEntry *cur = file_entry->funcs[func_name_hash];
  GFuncEntry *prev = NULL;
  while (cur != NULL) {
    if (cur->func_gvar == func_gvar) { return cur; }
    prev = cur;
    cur = cur->next;
  }

  GFuncEntry *new_entry = new GFuncEntry();
  memset(new_entry, 0, sizeof(GFuncEntry));
  new_entry->func_gvar = func_gvar;
  prev->next = new_entry;

  return new_entry;
}

GFileEntry *insert_FileEntry(GFileEntry **file_map, const std::string &filename,
                             llvm::GlobalVariable *file_gvar) {
  uint8_t filename_hash = simple_hash(filename);

  if (file_map[filename_hash] == nullptr) {
    GFileEntry *new_entry = new GFileEntry();

    memset(new_entry, 0, sizeof(GFileEntry));
    new_entry->file_gvar = file_gvar;
    file_map[filename_hash] = new_entry;
    return new_entry;
  }

  GFileEntry *cur = file_map[filename_hash];
  GFileEntry *prev = NULL;
  while (cur != NULL) {
    if (cur->file_gvar == file_gvar) { return cur; }
    prev = cur;
    cur = cur->next;
  }

  GFileEntry *new_entry = new GFileEntry();
  memset(new_entry, 0, sizeof(GFileEntry));
  new_entry->file_gvar = file_gvar;
  prev->next = new_entry;
  return new_entry;
}

GBBEntry *insert_BBEntry(GFuncEntry *func_entry, const std::string &bb_name,
                         llvm::GlobalVariable *bb_gvar) {
  uint8_t bb_name_hash = simple_hash(bb_name);

  if (func_entry->bbs[bb_name_hash] == nullptr) {
    GBBEntry *new_entry = new GBBEntry();
    memset(new_entry, 0, sizeof(GBBEntry));
    new_entry->bb_gvar = bb_gvar;
    func_entry->bbs[bb_name_hash] = new_entry;
    return new_entry;
  }

  GBBEntry *cur = func_entry->bbs[bb_name_hash];
  GBBEntry *prev = NULL;
  while (cur != NULL) {
    if (cur->bb_gvar == bb_gvar) { return cur; }
    prev = cur;
    cur = cur->next;
  }

  GBBEntry *new_entry = new GBBEntry();
  memset(new_entry, 0, sizeof(GBBEntry));
  new_entry->bb_gvar = bb_gvar;
  prev->next = new_entry;

  return new_entry;
}

void free_bb_map(GFileEntry **file_map) {
  const uint32_t hash_map_size = sizeof(uint8_t) * 256;

  for (size_t idx = 0; idx < hash_map_size; idx++) {
    GFileEntry *file_entry = file_map[idx];
    if (file_entry == NULL) { continue; }

    while (file_entry != NULL) {
      for (size_t func_idx = 0; func_idx < hash_map_size; func_idx++) {
        GFuncEntry *func_entry = file_entry->funcs[func_idx];
        if (func_entry == NULL) { continue; }

        while (func_entry != NULL) {
          for (size_t bb_idx = 0; bb_idx < hash_map_size; bb_idx++) {
            GBBEntry *bb_entry = func_entry->bbs[bb_idx];
            if (bb_entry == NULL) { continue; }

            while (bb_entry != NULL) {
              GBBEntry *next_bb_entry = bb_entry->next;
              delete bb_entry;
              bb_entry = next_bb_entry;
            }
          }

          GFuncEntry *next_func_entry = func_entry->next;
          delete func_entry;
          func_entry = next_func_entry;
        }
      }

      GFileEntry *next_file_entry = file_entry->next;
      delete file_entry;
      file_entry = next_file_entry;
    }
  }

  memset(file_map, 0, sizeof(GFileEntry *) * hash_map_size);
  return;
}

#pragma clang attribute pop