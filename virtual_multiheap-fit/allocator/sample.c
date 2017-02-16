#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "virtual_multiheap_fit.h"

#define SAMPLE_STR "Hello World"

int main(int argc, char* argv[]) {
  void* addr;
  vmf_t vmf;

  vmf = vmf_init(1, 2048, 16, 32768);
  vmf_allocate(vmf, 0, 1024);
  vmf_allocate(vmf, 1, 1024);

  addr = vmf_dereference(vmf, 1);
  strcpy(addr, SAMPLE_STR);
  printf("%p : %s\n", addr, (char*)addr);

  vmf_deallocate(vmf, 0);
  addr = vmf_dereference(vmf, 1);
  printf("%p : %s\n", addr, (char*)addr);

  return EXIT_SUCCESS;
}
