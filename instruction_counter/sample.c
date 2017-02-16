#include <stdio.h>
#include <stdlib.h>
#include "instruction_counter.h"

void __attribute__((optimize("0"))) plus(void) {
  int a = 1;
  int b = 2;
  int c = 0;
  instruction_count_set_string("plus");
  instruction_count_start();
  c = a + b;
  instruction_count_end();
}

void __attribute__((optimize("0"))) loop(void) {
  int i;
  int sum = 0;

  instruction_count_set_string("loop");
  instruction_count_start();
  for (i = 0; i < 100; ++i) {
    sum += i;
  }
  instruction_count_end();
}

int main(int argc, char* argv[]) {
  instruction_count_init();
  plus();
  loop();

  return EXIT_SUCCESS;
}
