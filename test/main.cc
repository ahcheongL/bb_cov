#include <stdio.h>

void foo(int *a) {
  *a = 20;
  return;
}

int main(int argc, char *argv[]) {
  int a = 30;
  int b = 0;

  for (a = 0; a < 20; a++) {
    b += a;
  }

  foo(&a);

  return 0;
}