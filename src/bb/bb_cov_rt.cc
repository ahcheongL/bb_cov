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
#include <mutex>
#include <sstream>

#include "utils/hash.hpp"

static const char *cov_output_fn = nullptr;

// Used for fast check of covered basic blocks
static char *bb_cov_arr = nullptr;

namespace fs = std::filesystem;

static std::mutex cov_mutex;

static std::map<std::string, std::map<std::string, std::set<std::string>>>
    prev_cov;

extern "C" {

void show_progress(size_t current, size_t total,
                   std::chrono::steady_clock::time_point start_time) {
  double progress = (double)current / total;
  int    barWidth = 40;

  // Print progress bar
  std::cout << "[";
  int pos = barWidth * progress;
  for (int i = 0; i < barWidth; ++i) {
    if (i < pos)
      std::cout << "=";
    else if (i == pos)
      std::cout << ">";
    else
      std::cout << " ";
  }
  std::cout << "] ";

  // Print percentage
  std::cout << std::fixed << std::setprecision(1) << progress * 100.0 << "% ";

  // Calculate estimated remaining time
  auto   now = std::chrono::steady_clock::now();
  double elapsed = std::chrono::duration<double>(now - start_time).count();
  double eta = (elapsed / progress) - elapsed;  // estimated remaining
  if (current == 0) eta = 0;                    // avoid division by zero

  std::cout << "ETA: " << std::fixed << std::setprecision(1) << eta << "s\r";
  std::cout.flush();
}

void __handle_init(int32_t *argc_ptr, char **argv) {
  const char *env_output_fn = getenv(OUTPUT_FN);

  if (env_output_fn != nullptr) {
    cov_output_fn = env_output_fn;

    std::cout << "[bb_cov] Found environment variable " << OUTPUT_FN
              << ", setting coverage output file to " << cov_output_fn
              << std::endl;

    // Initialize bb_cov_arr
    bb_cov_arr = (char *)malloc(__num_bbs);
    if (bb_cov_arr == nullptr) {
      std::cerr << "[bb_cov] Failed to allocate memory for coverage array."
                << std::endl;
      exit(1);
    }
    memset(bb_cov_arr, 0, __num_bbs);

    std::cout << "[bb_cov] Found " << __num_bbs << " basic blocks to track."
              << std::endl;
    return;
  }

  int32_t argc = *argc_ptr;

  if (argc < 2) {
    std::cout << "[bb_cov] Usage : " << argv[0]
              << " <args ...> [cov_output_fn] [inputs_dir] [cov_output_dir] \n";
    std::cout << "  <args ...> : arguments for the target program\n";
    std::cout
        << "  if @@ placeholder is NOT in <args ...>, it is considered as a "
           "normal execution with coverage instrumentation, and generates "
           "coverage report output file to [cov_output_fn].\n\n";
    std::cout << "  if @@ placeholder is in <args ...>, it is considered as "
                 "replaying all inputs in <inputs_dir> and generating coverage "
                 "reports to [cov_output_dir].\n";
    exit(1);
  }

  uint32_t placeholder_idx = -1;
  for (uint32_t idx = 1; idx < (uint32_t)argc; idx++) {
    if (strncmp(argv[idx], "@@", 3) == 0) {
      placeholder_idx = idx;
      break;
    }
  }

  // Initialize bb_cov_arr
  bb_cov_arr = (char *)malloc(__num_bbs);
  if (bb_cov_arr == nullptr) {
    std::cerr << "[bb_cov] Failed to allocate memory for coverage array."
              << std::endl;
    exit(1);
  }
  memset(bb_cov_arr, 0, __num_bbs);

  if (placeholder_idx == -1) {
    // normal execution with one input file
    int32_t new_argc = argc - 1;
    cov_output_fn = argv[new_argc];
    argv[new_argc] = nullptr;
    *argc_ptr = new_argc;
    std::cout << "[bb_cov] Found " << __num_bbs << " basic blocks to track."
              << std::endl;
    std::cout << "[bb_cov] Coverage output file: " << cov_output_fn
              << std::endl;
#ifdef WRITE_COV_PER_BB
    __cov_read_prev_cov();
#endif
    return;
  }

  std::cout << "[bb_cov] Replaying all inputs in directory mode." << std::endl;

  *argc_ptr = argc - 2;
  const char *inputs_dir = argv[argc - 2];
  const char *outputs_dir = argv[argc - 1];
  argv[argc - 2] = nullptr;
  argv[argc - 1] = nullptr;

  fs::path dir_path(inputs_dir);

  if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
    std::cerr
        << "[bb_cov] Inputs directory does not exist or is not a directory."
        << std::endl;
    exit(1);
  }

  fs::path out_dir_path(outputs_dir);
  if (!fs::exists(out_dir_path)) { fs::create_directory(out_dir_path); }

  auto           dir_iter = fs::directory_iterator(dir_path);
  const uint32_t num_inputs = std::distance(dir_iter, fs::directory_iterator{});
  std::cout << "Found " << num_inputs << " inputs to process." << std::endl;

  dir_iter = fs::directory_iterator(dir_path);  // reset iterator

  uint32_t input_idx = 0;
  auto     start_time = std::chrono::steady_clock::now();

  for (const auto &entry : dir_iter) {
    std::string basename = entry.path().filename().string();
    if (!entry.is_regular_file()) { continue; }

    basename = entry.path().filename().string();
    if (basename.rfind("id:", 0) != 0) {  // starts with "id:"
      continue;
    }

    pid_t pid = fork();

    if (pid < 0) {
      std::cerr << "[bb_cov] Fork failed." << std::endl;
      exit(1);
    }

    if (pid == 0) {
      // child process

      // devnull
      int devnull_fd = open("/dev/null", O_RDWR);
      dup2(devnull_fd, STDOUT_FILENO);
      dup2(devnull_fd, STDERR_FILENO);
      close(devnull_fd);

      std::string input_path = entry.path().string();
      size_t      path_size = input_path.length() + 1;
      char       *input_path_cstr = new char[path_size];
      strncpy(input_path_cstr, input_path.c_str(), path_size);
      argv[placeholder_idx] = input_path_cstr;  // memory leak ...

      size_t pos = basename.find(',');
      if (pos != std::string::npos) { basename = basename.substr(0, pos); }
      std::string output_path = std::string(outputs_dir) + "/" + basename;
      path_size = output_path.length() + 1;

      char *output_path_cstr = new char[path_size];
      strncpy(output_path_cstr, output_path.c_str(),
              path_size);  // memory leak ...
      cov_output_fn = output_path_cstr;

#ifdef WRITE_COV_PER_BB
      __cov_read_prev_cov();
#endif

      return;
    }

    int32_t status = 0;
    waitpid(pid, &status, 0);

    input_idx++;
    show_progress(input_idx, num_inputs, start_time);
  }

  std::cout << "\n[bb_cov] All " << input_idx << " inputs processed."
            << std::endl;
  exit(0);
}

void __record_bb_cov(const char *file_name, const char *func_name,
                     const char *bb_name, const uint32_t bb_id) {
  if (bb_cov_arr == nullptr) { return; }

  {
    std::lock_guard<std::mutex> guard(cov_mutex);
    if (bb_cov_arr[bb_id] == 1) { return; }
    bb_cov_arr[bb_id] = 1;
  }

  uint8_t file_hash = simple_hash(file_name);
  uint8_t func_hash = simple_hash(func_name);
  uint8_t bb_hash = simple_hash(bb_name);

  const CFileEntry *file_entry = __file_func_map[file_hash];
  // skip nullptr checks for speed, entry pointers should not be nullptr

  while (file_entry != nullptr) {
    if (file_entry->filename == file_name) { break; }
    file_entry = file_entry->next;
  }

  const CFuncEntry *func_entry = file_entry->funcs[func_hash];
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

#ifdef WRITE_COV_PER_BB
  __write_cov();
#endif

  return;
}

static void __write_cov() {
  if (cov_output_fn == nullptr) {
    std::cerr << "[bb_cov] No coverage information collected." << std::endl;
    return;
  }

  std::ofstream cov_file_out(cov_output_fn, std::ios::out);
  if (!cov_file_out.is_open()) {
    std::cerr << "[bb_cov] Failed to open coverage output file." << std::endl;
    return;
  }

  const int hash_map_size = sizeof(uint8_t) * 256;
  for (size_t file_idx = 0; file_idx < hash_map_size; file_idx++) {
    const CFileEntry *file_entry = __file_func_map[file_idx];
    while (file_entry != nullptr) {
      const char *file_name = file_entry->filename;
      cov_file_out << "File " << file_name << "\n";

      // to merge with previous coverage info, fetch previous coverage
      const std::map<std::string, std::set<std::string>> *prev_file_cov =
          nullptr;
      auto search = prev_cov.find(file_name);
      if (search != prev_cov.end()) { prev_file_cov = &(search->second); }

      for (size_t func_idx = 0; func_idx < hash_map_size; func_idx++) {
        const CFuncEntry *func_entry = file_entry->funcs[func_idx];
        while (func_entry != nullptr) {
          const char *func_name = func_entry->func_name;
          cov_file_out << "F " << func_name << " ";

          // merge with previous coverage info
          const std::set<std::string> *prev_func_cov = nullptr;
          if (prev_file_cov != nullptr) {
            auto search2 = prev_file_cov->find(func_name);
            if (search2 != prev_file_cov->end()) {
              prev_func_cov = &(search2->second);
            }
          }

          std::stringstream bb_ss;

          bool is_func_covered = false;
          for (size_t bb_idx = 0; bb_idx < hash_map_size; bb_idx++) {
            const CBBEntry *bb_entry = func_entry->bbs[bb_idx];
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

static void __cov_read_prev_cov() {
  if (cov_output_fn == nullptr) { return; }

  std::ifstream cov_file_in(cov_output_fn, std::ios::in);
  if (!cov_file_in.is_open()) { return; }

  std::cout << "[bb_cov] Found existing coverage file, reading ...\n";
  std::string type;
  std::string name;
  bool        is_covered = false;
  std::string cur_func = "";
  std::string line;

  std::map<std::string, std::set<std::string>> *cur_file_map = nullptr;
  std::set<std::string>                        *cur_func_map = nullptr;

  bool found_error = false;

  while (getline(cov_file_in, line)) {
    if (line.length() == 0) { continue; }

    auto pos1 = line.find(" ");

    if (pos1 == std::string::npos) {
      found_error = true;
      break;
    }

    auto pos2 = line.find_last_of(" ");

    if (pos2 == std::string::npos) {
      found_error = true;
      break;
    }

    type = line.substr(0, pos1);
    name = line.substr(pos1 + 1, pos2 - pos1 - 1);
    is_covered = line.substr(pos2 + 1) == "1";

    if (type == "File") {
      cur_file_map =
          &prev_cov
               .try_emplace(name,
                            std::map<std::string, std::set<std::string>>())
               .first->second;
      continue;
    }

    if (!is_covered) { continue; }

    if (type == "F") {
      if (cur_file_map == nullptr) {
        found_error = true;
        continue;
      }

      cur_func_map = &(*cur_file_map)
                          .try_emplace(name, std::set<std::string>())
                          .first->second;
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
    std::cout << "Error reading existing coverage file, coverage may be not "
                 "accurate"
              << std::endl;
  }
  return;
}

void __cov_fini() {
  std::lock_guard<std::mutex> guard(cov_mutex);

  if (bb_cov_arr == nullptr) { return; }

  // previous coverage collected from existing coverage files
#ifndef WRITE_COV_PER_BB
  __cov_read_prev_cov();
#endif

  if (bb_cov_arr == nullptr) {
    std::cout << "[bb_cov] No coverage information collected." << std::endl;
    return;
  }

  std::cout << "[bb_cov] Writing coverage info to files..." << std::endl;
  __write_cov();

  free(bb_cov_arr);
  bb_cov_arr = nullptr;
  return;
}

}  // extern "C"
