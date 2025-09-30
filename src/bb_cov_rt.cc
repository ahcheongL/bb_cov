#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>

using namespace std;

static const char *cov_output_fn = NULL;

// Used for fast check of covered basic blocks
static char *bb_cov_arr = NULL;

struct FuncBBMap {
  const char *func_name;
  const char **bb_names;
  const unsigned int size;
};

struct FileFuncMap {
  const char *filename;
  struct FuncBBMap *entries;
  const unsigned int size;
};

extern "C" {
// entire bb list generated at compile time
extern struct FileFuncMap __bb_map[];
extern unsigned int __bb_map_size;
extern unsigned int __num_bbs;

// coverage collected at runtime
static map<const char *, map<const char *, map<const char *, bool>>>
    __replay_coverage_info;

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
  (*argv_ptr)[argc] = NULL;

  cout << "[bb_cov] Found " << __num_bbs << " basic blocks to track." << endl;
  cout << "[bb_cov] Coverage output file: " << cov_output_fn << endl;

  // Initialize bb_cov_arr
  bb_cov_arr = (char *)malloc(__num_bbs);
  if (bb_cov_arr == NULL) {
    cerr << "[bb_cov] Failed to allocate memory for coverage array." << endl;
    exit(1);
  }
  memset(bb_cov_arr, 0, __num_bbs);

  // read __bb_map to initialize __replay_coverage_info
  unsigned int file_idx = 0;
  for (; file_idx < __bb_map_size; file_idx++) {
    struct FileFuncMap &file_map = __bb_map[file_idx];
    const char *filename = file_map.filename;
    map<const char *, map<const char *, bool>> &func_map =
        __replay_coverage_info
            .try_emplace(filename, map<const char *, map<const char *, bool>>())
            .first->second;

    unsigned int func_idx = 0;
    for (; func_idx < file_map.size; func_idx++) {
      struct FuncBBMap &func_map_entry = file_map.entries[func_idx];
      const char *func_name = func_map_entry.func_name;
      map<const char *, bool> &bb_map =
          func_map.try_emplace(func_name, map<const char *, bool>())
              .first->second;

      unsigned int bb_idx = 0;
      for (; bb_idx < func_map_entry.size; bb_idx++) {
        const char *bb_name = func_map_entry.bb_names[bb_idx];
        bb_map.try_emplace(bb_name, false);
      }
    }
  }

  return;
}

void __record_bb_cov(const char *file_name, const char *func_name,
                     const char *bb_name, const unsigned int bb_id) {
  if (bb_cov_arr[bb_id] == 1) {
    return;
  }

  bb_cov_arr[bb_id] = 1;

  auto file_cov = __replay_coverage_info.find(file_name);
  auto func_cov = file_cov->second.find(func_name);
  auto bb_cov = func_cov->second.find(bb_name);
  bb_cov->second = true;
  return;
}

void __write_cov(const map<string, map<string, set<string>>> &prev_cov) {
  if (cov_output_fn == NULL) {
    cerr << "[bb_cov] No coverage information collected." << endl;
    return;
  }

  ofstream cov_file_out(cov_output_fn, ios::out);
  if (!cov_file_out.is_open()) {
    cerr << "[bb_cov] Failed to open coverage output file." << endl;
    return;
  }

  for (auto file_iter : __replay_coverage_info) {
    const char *file_name = file_iter.first;
    cov_file_out << "File " << file_name << "\n";

    map<const char *, map<const char *, bool>> &file_cov = file_iter.second;
    // merge with previous coverage info
    const map<string, set<string>> *prev_file_cov = NULL;
    auto search = prev_cov.find(file_name);
    if (search != prev_cov.end()) {
      prev_file_cov = &(search->second);
    }

    for (auto func_iter : file_cov) {
      const char *func_name = func_iter.first;
      map<const char *, bool> &func_cov = func_iter.second;

      const set<string> *prev_func_cov = NULL;
      if (prev_file_cov != NULL) {
        auto search2 = prev_file_cov->find(func_name);
        if (search2 != prev_file_cov->end()) {
          prev_func_cov = &(search2->second);
        }
      }

      stringstream bb_ss;

      bool is_func_covered = false;
      for (auto bb_iter : func_cov) {
        const char *bb_name = bb_iter.first;
        bool is_bb_covered = bb_iter.second;
        if (!is_bb_covered && prev_func_cov != NULL &&
            prev_func_cov->find(bb_name) != prev_func_cov->end()) {
          is_bb_covered = true;
        }
        is_func_covered = is_func_covered || is_bb_covered;

        bb_ss << "B " << bb_name << " " << (is_bb_covered ? "1" : "0") << "\n";
      }

      cov_file_out << "F " << func_name << " " << (is_func_covered ? "1" : "0")
                   << "\n";

      cov_file_out << bb_ss.str();
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
    bool is_covered = false;
    string cur_func = "";
    string line;

    map<string, set<string>> *cur_file_map = NULL;
    set<string> *cur_func_map = NULL;

    while (getline(cov_file_in, line)) {
      if (line.length() == 0) {
        continue;
      }

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

      if (!is_covered) {
        continue;
      }

      if (type == "F") {
        if (cur_file_map == NULL) {
          found_error = true;
          continue;
        }

        cur_func_map =
            &(*cur_file_map).try_emplace(name, set<string>()).first->second;
        continue;
      }

      if (cur_func_map == NULL) {
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

  cout << "[bb_cov] Writing coverage info to files..." << endl;
  __write_cov(prev_cov);

  free(bb_cov_arr);
  bb_cov_arr = NULL;
  return;
}

}  // extern "C"