#include <stdio.h>

void f1(int *a) {
  if (a == NULL) { return; }
  *a = 20;
  return;
}

int main(int argc, char *argv[]) {
  int a = 30;
  int b = 0;

  for (a = 0; a < 20; a++) {
    b += a;
  }

  f1(&a);
  return 0;
}