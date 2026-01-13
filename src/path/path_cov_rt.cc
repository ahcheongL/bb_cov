#include "path/path_cov_rt.hpp"

#include <string.h>

#include <fstream>
#include <iostream>

static const char *cov_output_fn = NULL;

#pragma clang attribute push(__attribute__((annotate("probe_function"))), \
                             apply_to = function)

extern "C" {

void __get_output_fn(int *argc_ptr, char ***argv_ptr) {
  const int argc = (*argc_ptr) - 1;
  *argc_ptr = argc;

  if (argc == 0) {
    cout << "[path_cov] Usage : " << (*argv_ptr)[0][0]
         << " <args ...> <cov_output_fn>\n";
    cout << "[path_cov] Put <cov_output_fn> as the last argument." << endl;
    exit(1);
  }

  cov_output_fn = (*argv_ptr)[argc];
  (*argv_ptr)[argc] = NULL;

  cout << "[path_cov] Found " << __num_bbs << " basic blocks to track." << endl;
  cout << "[path_cov] Coverage output file: " << cov_output_fn << endl;

  __path_hash_val = 1;
  return;
}

void __cov_fini() {
  ofstream cov_file_out(cov_output_fn, ios::out);
  if (!cov_file_out.is_open()) {
    cerr << "[path_cov] Failed to open coverage output file." << endl;
    return;
  }

  cov_file_out << __path_hash_val;

  cov_file_out.close();
  return;
}
}

#pragma clang attribute pop