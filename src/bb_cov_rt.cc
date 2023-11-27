#include <stdio.h>

#include <fstream>
#include <iostream>
#include <map>
#include <string>

using namespace std;

extern "C" {

// coverage
static map<char *, map<string, map<string, bool>>> __replay_coverage_info;

void __record_bb_cov(char *file_name, char *func_name, char *bb_name) {
  if (__replay_coverage_info.find(file_name) == __replay_coverage_info.end()) {
    __replay_coverage_info.insert(
        make_pair(file_name, map<string, map<string, bool>>()));
  }

  map<string, map<string, bool>> &file_info = __replay_coverage_info[file_name];
  if (file_info.find(func_name) == file_info.end()) {
    file_info.insert(make_pair(func_name, map<string, bool>()));
  }

  map<string, bool> &func_info = file_info[func_name];
  if (func_info.find(bb_name) == func_info.end()) {
    func_info.insert(make_pair(bb_name, true));
  }

  return;
}

void __cov_fini() {
  for (auto iter : __replay_coverage_info) {
    const string cov_file_name = string(iter.first) + ".cov";

    map<string, map<string, bool>> &file_info = iter.second;

    ifstream cov_file_in(cov_file_name, ios::in);

    if (cov_file_in.is_open()) {
      string type;
      string name;
      bool is_covered = false;
      string cur_func = "";
      string line;
      while (getline(cov_file_in, line)) {
        auto pos1 = line.find(" ");

        if (pos1 == string::npos) {
          break;
        }

        auto pos2 = line.find_last_of(" ");

        if (pos2 == string::npos) {
          break;
        }

        if (pos1 == pos2) {
          break;
        }

        type = line.substr(0, pos1);
        name = line.substr(pos1 + 1, pos2 - pos1 - 1);
        is_covered = line.substr(pos2 + 1) == "1";
        if (type == "F") {
          if (file_info.find(name) == file_info.end()) {
            file_info.insert(make_pair(name, map<string, bool>()));
          }

          cur_func = name;
        } else {
          if (file_info[cur_func].find(name) == file_info[cur_func].end()) {
            file_info[cur_func].insert(make_pair(name, is_covered));
          } else if (is_covered) {
            file_info[cur_func][name] = true;
          }
        }
      }
    }
    cov_file_in.close();

    ofstream cov_file_out(cov_file_name, ios::out);

    for (auto iter2 : file_info) {
      bool is_func_covered = false;
      const map<string, bool> &func_info = iter2.second;

      for (auto iter3 : func_info) {
        if (iter3.second) {
          is_func_covered = true;
          break;
        }
      }

      cov_file_out << "F " << iter2.first << " " << is_func_covered << "\n";

      for (auto iter3 : func_info) {
        cov_file_out << "B " << iter3.first << " " << iter3.second << "\n";
      }
    }

    cov_file_out.close();
  }
  return;
}

}  // extern "C"