#include "bb/bb_cov_rt.hpp"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "utils/hash.hpp"

static const char *cov_output_fn = nullptr;

// Used for fast check of covered basic blocks
static char *bb_cov_arr = nullptr;

extern "C" {

void __get_output_fn(int *argc_ptr, char ***argv_ptr) {
  const int argc = (*argc_ptr) - 1;
  *argc_ptr = argc;

  if (argc == 0) {
    cout << "[bb_cov] Usage : " << (*argv_ptr)[0][0]
         << " <args ...> <cov_output_fn>\n";
    cout << "[bb_cov] Put <cov_output_fn> as the last argument." << endl;
    exit(1);
  }

  cov_output_fn = (*argv_ptr)[argc];
  (*argv_ptr)[argc] = nullptr;

  cout << "[bb_cov] Found " << __num_bbs << " basic blocks to track." << endl;
  cout << "[bb_cov] Coverage output file: " << cov_output_fn << endl;

  // Initialize bb_cov_arr
  bb_cov_arr = (char *)malloc(__num_bbs);
  if (bb_cov_arr == nullptr) {
    cerr << "[bb_cov] Failed to allocate memory for coverage array." << endl;
    exit(1);
  }
  memset(bb_cov_arr, 0, __num_bbs);
  return;
}

void __record_bb_cov(const char *file_name, const char *func_name,
                     const char *bb_name, const u_int32_t bb_id) {
  if (bb_cov_arr == nullptr) { return; }
  if (bb_cov_arr[bb_id] == 1) { return; }

  bb_cov_arr[bb_id] = 1;

  u_int8_t file_hash = simple_hash(file_name);
  u_int8_t func_hash = simple_hash(func_name);
  u_int8_t bb_hash = simple_hash(bb_name);

  CFileEntry *file_entry = __file_func_map[file_hash];
  // skip nullptr checks for speed, entry pointers should not be nullptr

  while (file_entry != nullptr) {
    if (file_entry->filename == file_name) { break; }
    file_entry = file_entry->next;
  }

  CFuncEntry *func_entry = file_entry->funcs[func_hash];
  while (func_entry != nullptr) {
    if (func_entry->func_name == func_name) { break; }
    func_entry = func_entry->next;
  }

  CBBEntry *bb_entry = func_entry->bbs[bb_hash];
  while (bb_entry != nullptr) {
    if (bb_entry->bb_name == bb_name) { break; }
    bb_entry = bb_entry->next;
  }
  bb_entry->is_covered = 1;
  return;
}

static void __write_cov(const map<string, map<string, set<string>>> &prev_cov) {
  if (cov_output_fn == nullptr) {
    cerr << "[bb_cov] No coverage information collected." << endl;
    return;
  }

  ofstream cov_file_out(cov_output_fn, ios::out);
  if (!cov_file_out.is_open()) {
    cerr << "[bb_cov] Failed to open coverage output file." << endl;
    return;
  }

  const int hash_map_size = sizeof(u_int8_t) * 256;
  for (size_t file_idx = 0; file_idx < hash_map_size; file_idx++) {
    CFileEntry *file_entry = __file_func_map[file_idx];
    while (file_entry != nullptr) {
      const char *file_name = file_entry->filename;
      cov_file_out << "File " << file_name << "\n";

      // to merge with previous coverage info, fetch previous coverage
      const map<string, set<string>> *prev_file_cov = nullptr;
      auto                            search = prev_cov.find(file_name);
      if (search != prev_cov.end()) { prev_file_cov = &(search->second); }

      for (size_t func_idx = 0; func_idx < hash_map_size; func_idx++) {
        CFuncEntry *func_entry = file_entry->funcs[func_idx];
        while (func_entry != nullptr) {
          const char *func_name = func_entry->func_name;
          cov_file_out << "F " << func_name << " ";

          // merge with previous coverage info
          const set<string> *prev_func_cov = nullptr;
          if (prev_file_cov != nullptr) {
            auto search2 = prev_file_cov->find(func_name);
            if (search2 != prev_file_cov->end()) {
              prev_func_cov = &(search2->second);
            }
          }

          stringstream bb_ss;

          bool is_func_covered = false;
          for (size_t bb_idx = 0; bb_idx < hash_map_size; bb_idx++) {
            CBBEntry *bb_entry = func_entry->bbs[bb_idx];
            while (bb_entry != nullptr) {
              const char *bb_name = bb_entry->bb_name;
              bool        is_bb_covered = (bb_entry->is_covered != 0);
              if (!is_bb_covered && prev_func_cov != nullptr &&
                  prev_func_cov->find(bb_name) != prev_func_cov->end()) {
                is_bb_covered = true;
              }
              is_func_covered = is_func_covered || is_bb_covered;

              bb_ss << "B " << bb_name << " " << (is_bb_covered ? "1" : "0")
                    << "\n";

              bb_entry = bb_entry->next;
            }
          }

          cov_file_out << (is_func_covered ? "1" : "0") << "\n";

          cov_file_out << bb_ss.str();

          func_entry = func_entry->next;
        }
      }

      file_entry = file_entry->next;
    }
  }

  cov_file_out.close();
  return;
}

void __cov_fini() {
  // previous coverage collected from existing coverage files
  map<string, map<string, set<string>>> prev_cov;

  bool found_error = false;

  ifstream cov_file_in(cov_output_fn, ios::in);
  if (cov_file_in.is_open()) {
    cout << "[bb_cov] Found existing coverage file, reading ...\n";
    string type;
    string name;
    bool   is_covered = false;
    string cur_func = "";
    string line;

    map<string, set<string>> *cur_file_map = nullptr;
    set<string>              *cur_func_map = nullptr;

    while (getline(cov_file_in, line)) {
      if (line.length() == 0) { continue; }

      auto pos1 = line.find(" ");

      if (pos1 == string::npos) {
        found_error = true;
        break;
      }

      auto pos2 = line.find_last_of(" ");

      if (pos2 == string::npos) {
        found_error = true;
        break;
      }

      type = line.substr(0, pos1);
      name = line.substr(pos1 + 1, pos2 - pos1 - 1);
      is_covered = line.substr(pos2 + 1) == "1";

      if (type == "File") {
        cur_file_map = &prev_cov.try_emplace(name, map<string, set<string>>())
                            .first->second;
        continue;
      }

      if (!is_covered) { continue; }

      if (type == "F") {
        if (cur_file_map == nullptr) {
          found_error = true;
          continue;
        }

        cur_func_map =
            &(*cur_file_map).try_emplace(name, set<string>()).first->second;
        continue;
      }

      if (cur_func_map == nullptr) {
        found_error = true;
        continue;
      }

      cur_func_map->insert(name);
    }
    cov_file_in.close();

    if (found_error) {
      cout << "Error reading existing coverage file, coverage may be not "
              "accurate"
           << endl;
    }
  }

  if (bb_cov_arr == nullptr) {
    cout << "[bb_cov] No coverage information collected." << endl;
    return;
  }

  cout << "[bb_cov] Writing coverage info to files..." << endl;
  __write_cov(prev_cov);

  free(bb_cov_arr);
  bb_cov_arr = nullptr;
  return;
}

}  // extern "C"