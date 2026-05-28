#include "utils/hash.hpp"

uint8_t bb_cov_simple_hash(const char *str) {
  if (str == NULL) {
    return 0;
  }

  uint8_t hash = 0;
  while (*str) {
    hash ^= *str;
    str++;
  }
  return hash;
}

uint8_t bb_cov_simple_hash(const std::string &str) {
  return bb_cov_simple_hash(str.c_str());
}