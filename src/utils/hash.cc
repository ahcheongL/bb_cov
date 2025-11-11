#include "utils/hash.hpp"

u_int8_t simple_hash(const char *str) {
  if (str == NULL) { return 0; }

  u_int8_t hash = 0;
  while (*str) {
    hash ^= *str;
    str++;
  }
  return hash;
}

u_int8_t simple_hash(const std::string &str) {
  return simple_hash(str.c_str());
}