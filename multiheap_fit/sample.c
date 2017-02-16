#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "multiheap_fit.h"

#define SAMPLE_STR "Hello World"

int main(int argc, char* argv[]) {
  void* addr;
  mf_t mf;
  
  mf = mf_init(1, 2048, 16, 32768);
  mf_allocate(mf, 0, 1024);
  mf_allocate(mf, 1, 1024);

  addr = mf_dereference(mf, 1);
  strcpy(addr, SAMPLE_STR);
  printf("%p : %s\n", addr, (char*)addr);

  mf_deallocate(mf, 0);
  addr = mf_dereference(mf, 1);
  printf("%p : %s\n", addr, (char*)addr);

  return EXIT_SUCCESS;
}
