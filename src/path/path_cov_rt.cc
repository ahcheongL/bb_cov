#include "path/path_cov_rt.hpp"

#include "utils/progress_bar.hpp"
#include <string.h>

#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <sys/wait.h>
#include <unistd.h>

static const char *cov_output_fn = nullptr;
static int path_hash_fd[2];
namespace fs = std::filesystem;

#define TIMEOUT_MILLISECONDS 5000

#pragma clang attribute push(__attribute__((annotate("probe_function"))),      \
                             apply_to = function)

extern "C" {

void __get_output_fn(int *argc_ptr, char **argv) {
  int argc = *argc_ptr;

  if (argc < 2) {
    std::cout << "[path_cov] Usage : " << argv[0]
              << " <args ...> [inputs_dir] <cov_output_fn>\n";
    std::cout
        << "  if @@ placeholder is NOT in <args ...>, it is considered as a "
           "normal execution with coverage instrumentation, and generates "
           "coverage output file to [cov_output_fn].\n\n";
    std::cout << "  if @@ placeholder is in <args ...>, it is considered as "
                 "replaying all inputs in <inputs_dir> and generating coverage "
                 "reports to [cov_output_fn].\n";
    std::cout << "  It assumes the input files are named in format id:xxx, and "
                 "the id integer ranges from 0 to (num_inputs -1).";
    std::cout << "  There should be no other files in the input directory.\n";
    exit(1);
  }

  uint32_t placeholder_idx = -1;
  for (uint32_t idx = 1; idx < (uint32_t)argc; idx++) {
    if (strncmp(argv[idx], "@@", 3) == 0) {
      placeholder_idx = idx;
      break;
    }
  }

  __path_hash_val = 1;

  if (placeholder_idx == -1) {
    // normal execution with one input file
    cov_output_fn = argv[argc - 1];
    argv[argc - 1] = nullptr;
    *argc_ptr = argc - 1;
    std::cout << "[path_cov] Found " << __num_bbs << " basic blocks to track."
              << std::endl;
    std::cout << "[path_cov] Coverage output file: " << cov_output_fn
              << std::endl;
    return;
  }

  if (argc < 3) {
    std::cout << "[path_cov] Usage : " << argv[0]
              << " <args ...> [inputs_dir] <cov_output_fn>\n";
    std::cout
        << "  if @@ placeholder is NOT in <args ...>, it is considered as a "
           "normal execution with coverage instrumentation, and generates "
           "coverage output file to [cov_output_fn].\n\n";
    std::cout << "  if @@ placeholder is in <args ...>, it is considered as "
                 "replaying all inputs in <inputs_dir> and generating coverage "
                 "reports to [cov_output_fn].\n";
    std::cout << "  It assumes the input files are named in format id:xxx, and "
                 "the id integer ranges from 0 to (num_inputs -1).";
    std::cout << "  There should be no other files in the input directory.\n";
    exit(1);
  }

  *argc_ptr = argc - 2;
  const char *inputs_dir = argv[argc - 2];
  cov_output_fn = argv[argc - 1];
  argv[argc - 2] = nullptr;
  argv[argc - 1] = nullptr;

  fs::path dir_path(inputs_dir);

  if (!fs::exists(dir_path) || !fs::is_directory(dir_path)) {
    std::cerr
        << "[path_cov] Inputs directory does not exist or is not a directory."
        << std::endl;
    exit(1);
  }

  auto dir_iter = fs::directory_iterator(dir_path);
  const uint32_t num_inputs = std::distance(dir_iter, fs::directory_iterator{});
  std::cout << "Found " << num_inputs << " inputs to process." << std::endl;

  uint32_t *path_hash_values = (uint32_t *)calloc(num_inputs, sizeof(uint32_t));

  uint32_t input_idx = 0;
  auto start_time = std::chrono::steady_clock::now();

  const size_t inputs_dir_len = strlen(inputs_dir);

  // enough for inputs_dir + "/id:xxx"
  char *new_input_path = (char *)malloc(inputs_dir_len + 20); // memory leak ...
  if (new_input_path == nullptr) {
    std::cerr << "[path_cov] Failed to allocate memory for input path."
              << std::endl;
    exit(1);
  }

  for (; input_idx < num_inputs; input_idx++) {
    snprintf(new_input_path, inputs_dir_len + 20, "%s/id:%u", inputs_dir,
             input_idx);

    if (!fs::exists(new_input_path)) {
      continue;
    }

    memset(path_hash_fd, 0, sizeof(path_hash_fd));
    if (pipe(path_hash_fd) == -1) {
      std::cerr << "[path_cov] Pipe failed." << std::endl;
      exit(1);
    }

    pid_t pid = fork();

    if (pid < 0) {
      std::cerr << "[path_cov] Fork failed." << std::endl;
      exit(1);
    }

    if (pid == 0) {
      // child process
      free(path_hash_values);
      close(path_hash_fd[0]);

      // devnull
      int devnull_fd = open("/dev/null", O_RDWR);
      dup2(devnull_fd, STDOUT_FILENO);
      dup2(devnull_fd, STDERR_FILENO);
      close(devnull_fd);

      argv[placeholder_idx] = new_input_path;

      __path_hash_val = 1;
      cov_output_fn = nullptr;

#ifdef WRITE_COV_PER_BB
      __cov_read_prev_cov();
#endif

      return;
    }

    close(path_hash_fd[1]);

    struct pollfd pfd;
    pfd.fd = path_hash_fd[0];
    pfd.events = POLLIN; // We want to know when data is ready to read

    int ret = poll(&pfd, 1, TIMEOUT_MILLISECONDS);

    if (ret > 0 && (pfd.revents & POLLIN)) {
      // Data is ready!
      ssize_t bytes_read =
          read(path_hash_fd[0], &__path_hash_val, sizeof(__path_hash_val));
      if (bytes_read == sizeof(__path_hash_val)) {
        path_hash_values[input_idx] = __path_hash_val;
      }
    } else {
      // Timeout or error
      // if (ret == 0) {
      //   // Timeout
      // } else {
      //   // error
      // }
      // Kill the stuck child so it doesn't become a ghost process
      kill(pid, SIGINT);
    }

    close(path_hash_fd[0]);

    int32_t status = 0;
    waitpid(pid, &status, 0);
    show_progress(input_idx, num_inputs, start_time);
  }

  PROGRESS_BAR_END();

  free(new_input_path);

  std::ofstream cov_file_out(cov_output_fn, std::ios::out);
  if (!cov_file_out.is_open()) {
    std::cerr << "[path_cov] Failed to open coverage output file." << std::endl;
    exit(1);
  }

  for (input_idx = 0; input_idx < num_inputs; input_idx++) {
    cov_file_out << path_hash_values[input_idx] << "\n";
  }

  cov_file_out.close();

  std::cout << "\n[path_cov] All " << input_idx << " inputs processed."
            << std::endl;
  exit(0);

  free(path_hash_values);
  return;
}

void __cov_fini() {
  if (cov_output_fn == nullptr) {
    if (path_hash_fd[1] != 0) {
      write(path_hash_fd[1], &__path_hash_val, sizeof(__path_hash_val));
      close(path_hash_fd[1]);
    }
    return;
  }

  std::ofstream cov_file_out(cov_output_fn, std::ios::out);
  if (!cov_file_out.is_open()) {
    std::cerr << "[path_cov] Failed to open coverage output file." << std::endl;
    return;
  }

  cov_file_out << __path_hash_val;

  cov_file_out.close();
  return;
}
}

#pragma clang attribute pop