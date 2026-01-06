#include "func/func_seq_rt.hpp"

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

static std::ofstream seq_output_f;
static std::mutex    cov_mutex;

// Used for fast check of covered basic blocks

namespace fs = std::filesystem;

#define FORKSRV_READ_FD 198
#define FORKSRV_WRITE_FD (FORKSRV_READ_FD + 1)

extern "C" {

void show_progress(size_t current, size_t total,
                   std::chrono::steady_clock::time_point start_time) {
  double  progress = (double)current / total;
  int32_t barWidth = 40;

  // Print progress bar
  std::cout << "[";
  int32_t pos = barWidth * progress;
  for (int32_t i = 0; i < barWidth; ++i) {
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
    seq_output_f.open(env_output_fn);
    std::cout << "[func_seq] Coverage output file: " << env_output_fn
              << std::endl;
    return;
  }

  // The environment variable is not set, parse arguments

  int32_t argc = *argc_ptr;

  if (argc < 2) {
    std::cout << "[func_seq] Usage : " << argv[0]
              << " <args ...> [seq_output_fn] [inputs_dir] [seq_output_dir] \n";
    std::cout << "  <args ...> : arguments for the target program\n";
    std::cout
        << "  if @@ placeholder is NOT in <args ...>, it is considered as a "
           "normal execution with coverage instrumentation, and generates "
           "coverage report output file to [seq_output_fn].\n\n";
    std::cout << "  if @@ placeholder is in <args ...>, it is considered as "
                 "replaying all inputs in <inputs_dir> and generating coverage "
                 "reports to [seq_output_dir].\n";

    std::cout << "\n";
    std::cout
        << "If environment variable FUNC_SEQ_OUTPUT_FN is set, "
           "it will override [seq_output_fn] argument. (no directory mode)\n";
    exit(1);
  }

  uint32_t placeholder_idx = -1;
  for (uint32_t idx = 1; idx < (uint32_t)argc; idx++) {
    if (strncmp(argv[idx], "@@", 3) == 0) {
      placeholder_idx = idx;
      break;
    }
  }

  if (placeholder_idx == -1) {
    // normal execution with one input file
    int32_t     new_argc = argc - 1;
    const char *seq_output_fn = argv[new_argc];

    seq_output_f.open(seq_output_fn);
    argv[new_argc] = nullptr;
    *argc_ptr = new_argc;
    std::cout << "[func_seq] Coverage output file: " << seq_output_fn
              << std::endl;
    return;
  }

  std::cout << "[func_seq] Replaying all inputs in directory mode.\n";

  *argc_ptr = argc - 2;
  const char *inputs_dir = argv[argc - 2];
  const char *outputs_dir = argv[argc - 1];
  argv[argc - 2] = nullptr;
  argv[argc - 1] = nullptr;

  fs::path dir_path(inputs_dir);

  if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
    std::cerr << "[func_seq] Inputs directory does not exist or is not a "
                 "directory.\n";
    exit(1);
  }

  fs::path out_dir_path(outputs_dir);
  if (!fs::exists(out_dir_path)) { fs::create_directory(out_dir_path); }

  auto           dir_iter = fs::directory_iterator(dir_path);
  const uint32_t num_inputs = std::distance(dir_iter, fs::directory_iterator{});
  std::cout << "Found " << num_inputs << " inputs to process.\n";

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
      std::cerr << "[func_seq] Fork failed." << std::endl;
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
      seq_output_f.open(output_path);

      return;
    }

    int32_t status = 0;
    waitpid(pid, &status, 0);

    input_idx++;
    show_progress(input_idx, num_inputs, start_time);
  }

  std::cout << "\n[func_seq] All " << input_idx << " inputs processed.\n";
  exit(0);
}

void __record_func_entry(const char *file_name, const char *func_name) {
  if (!seq_output_f.is_open()) { return; }

  std::lock_guard<std::mutex> guard(cov_mutex);
  seq_output_f << file_name << ":" << func_name << " ENTRY\n";
  return;
}

void __record_func_external(const char *func_name) {
  if (!seq_output_f.is_open()) { return; }

  std::lock_guard<std::mutex> guard(cov_mutex);
  seq_output_f << func_name << " EXTERNAL\n";
  return;
}

void __record_func_ret(const char *file_name, const char *func_name) {
  if (!seq_output_f.is_open()) { return; }

  std::lock_guard<std::mutex> guard(cov_mutex);
  seq_output_f << file_name << ":" << func_name << " RETURN\n";
  return;
}

void __cov_fini() {
  std::lock_guard<std::mutex> guard(cov_mutex);
  seq_output_f.close();
  return;
}

}  // extern "C"
