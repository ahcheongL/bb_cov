#include "hash.hpp"

unsigned char simple_hash(const char *str) {
  if (str == NULL) { return 0; }

  unsigned char hash = 0;
  while (*str) {
    hash ^= *str;
    str++;
  }
  return hash;
}

unsigned char simple_hash(const std::string &str) {
  return simple_hash(str.c_str());
}