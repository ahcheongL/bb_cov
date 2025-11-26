#include "bb/bb_cov_rt.hpp"

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>

#include "utils/hash.hpp"

static char *cov_output_fn = nullptr;

// Used for fast check of covered basic blocks
static char *bb_cov_arr = nullptr;

namespace fs = filesystem;

#define FORKSRV_READ_FD 198
#define FORKSRV_WRITE_FD (FORKSRV_READ_FD + 1)

extern "C" {

void show_progress(size_t current, size_t total,
                   chrono::steady_clock::time_point start_time) {
  double progress = (double)current / total;
  int    barWidth = 40;

  // Print progress bar
  cout << "[";
  int pos = barWidth * progress;
  for (int i = 0; i < barWidth; ++i) {
    if (i < pos)
      cout << "=";
    else if (i == pos)
      cout << ">";
    else
      cout << " ";
  }
  cout << "] ";

  // Print percentage
  cout << fixed << setprecision(1) << progress * 100.0 << "% ";

  // Calculate estimated remaining time
  auto   now = chrono::steady_clock::now();
  double elapsed = chrono::duration<double>(now - start_time).count();
  double eta = (elapsed / progress) - elapsed;  // estimated remaining
  if (current == 0) eta = 0;                    // avoid division by zero

  cout << "ETA: " << fixed << setprecision(1) << eta << "s\r";
  cout.flush();
}

void __handle_init(int32_t *argc_ptr, char **argv) {
  int32_t argc = *argc_ptr;

  if (argc < 2) {
    cout << "[bb_cov] Usage : " << argv[0]
         << " <args ...> [cov_output_fn] [inputs_dir] [cov_output_dir] \n";
    cout << "  <args ...> : arguments for the target program\n";
    cout << "  if @@ placeholder is NOT in <args ...>, it is considered as a "
            "normal execution with coverage instrumentation, and generates "
            "coverage report output file to [cov_output_fn].\n\n";
    cout << "  if @@ placeholder is in <args ...>, it is considered as "
            "replaying all inputs in <inputs_dir> and generating coverage "
            "reports to [cov_output_dir].\n";
    exit(1);
  }

  u_int32_t placeholder_idx = -1;
  for (u_int32_t idx = 1; idx < (u_int32_t)argc; idx++) {
    if (strncmp(argv[idx], "@@", 3) == 0) {
      placeholder_idx = idx;
      break;
    }
  }

  // Initialize bb_cov_arr
  bb_cov_arr = (char *)malloc(__num_bbs);
  if (bb_cov_arr == nullptr) {
    cerr << "[bb_cov] Failed to allocate memory for coverage array." << endl;
    exit(1);
  }
  memset(bb_cov_arr, 0, __num_bbs);

  if (placeholder_idx == -1) {
    // normal execution with one input file
    int32_t new_argc = argc - 1;
    cov_output_fn = argv[new_argc];
    argv[new_argc] = nullptr;
    *argc_ptr = new_argc;
    cout << "[bb_cov] Found " << __num_bbs << " basic blocks to track." << endl;
    cout << "[bb_cov] Coverage output file: " << cov_output_fn << endl;
    return;
  }

  cout << "[bb_cov] Replaying all inputs in directory mode." << endl;

  *argc_ptr = argc - 2;
  const char *inputs_dir = argv[argc - 2];
  const char *outputs_dir = argv[argc - 1];
  argv[argc - 2] = nullptr;
  argv[argc - 1] = nullptr;

  fs::path dir_path(inputs_dir);

  if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
    cerr << "[bb_cov] Inputs directory does not exist or is not a directory."
         << endl;
    exit(1);
  }

  fs::path out_dir_path(outputs_dir);
  if (!fs::exists(out_dir_path)) { fs::create_directory(out_dir_path); }

  auto           dir_iter = fs::directory_iterator(dir_path);
  const uint32_t num_inputs = distance(dir_iter, fs::directory_iterator{});
  cout << "Found " << num_inputs << " inputs to process." << endl;

  dir_iter = fs::directory_iterator(dir_path);  // reset iterator

  uint32_t input_idx = 0;
  auto     start_time = chrono::steady_clock::now();

  for (const auto &entry : dir_iter) {
    string basename = entry.path().filename().string();

    if (!entry.is_regular_file()) { continue; }

    basename = entry.path().filename().string();
    if (basename.rfind("id:", 0) != 0) {  // starts with "id:"
      continue;
    }

    pid_t pid = fork();

    if (pid < 0) {
      cerr << "[bb_cov] Fork failed." << endl;
      exit(1);
    }

    if (pid == 0) {
      // child process

      // devnull
      int devnull_fd = open("/dev/null", O_RDWR);
      dup2(devnull_fd, STDOUT_FILENO);
      dup2(devnull_fd, STDERR_FILENO);
      close(devnull_fd);

      string input_path = entry.path().string();
      size_t path_size = input_path.length() + 1;
      char  *input_path_cstr = new char[path_size];
      strncpy(input_path_cstr, input_path.c_str(), path_size);
      argv[placeholder_idx] = input_path_cstr;  // memory leak ...

      size_t pos = basename.find(',');
      if (pos != string::npos) { basename = basename.substr(0, pos); }
      string output_path = string(outputs_dir) + "/" + basename;
      path_size = output_path.length() + 1;
      cov_output_fn = new char[path_size];
      strncpy(cov_output_fn, output_path.c_str(),
              path_size);  // memory leak ...

      return;
    }

    int32_t status = 0;
    waitpid(pid, &status, 0);

    input_idx++;
    show_progress(input_idx, num_inputs, start_time);
  }

  cout << "\n[bb_cov] All " << input_idx << " inputs processed." << endl;
  exit(0);
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
