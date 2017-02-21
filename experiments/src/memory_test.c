#include <stdio.h>
#include <stdlib.h>

#include "allocator.h"
#include "memlog.h"

static void print_usage(const char* program_name);

int main(int argc, char* argv[]) {
  memlog_t* memlog;
  int allocator;
  size_t i;
  size_t curr_time;
  enum command_type type;
  size_t command_nr, idx, size;

  if (argc < 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  memlog = memlog_open(argv[1]);
  allocator = atoi(argv[2]);
  if (allocator >= ALLOC_NB) {
    fprintf(stderr, "allocator error\n");
    return EXIT_FAILURE;
  }

  init_funcs[allocator](memlog->mem_min, memlog->mem_max,
    memlog->block_max, memlog->require_size);
  command_nr = memlog->command_nr;
  curr_time  = 0;
  for (i = 0; i < command_nr; ++i) {
    type = memlog->commands[i].type;
    idx  = memlog->commands[i].idx;
    size = memlog->commands[i].size;
    if (command_kind(type) == COMMAND_ALLOCATE) {
      allocate_funcs[allocator](idx, size);
    } else if (command_kind(type) == COMMAND_DEALLOCATE) {
      deallocate_funcs[allocator](idx);
    } else if (command_kind(type) == COMMAND_REALLOCATE) {
      reallocate_funcs[allocator](idx, size);
    } else {
      continue;
    }
    curr_time++;
    printf("%zu %zu\n", curr_time, getsize_funcs[allocator]());
  }

  return EXIT_SUCCESS;
}

static void print_usage(const char* program_name) {
  int i;
  printf("%s <memlog file> <allocator number>\n", program_name);
  putchar('\n');
  printf(" Number |        Allocator Name \n");
  printf("--------+-----------------------\n");
  for (i = 0; i < ALLOC_NB; ++i) {
    printf("%7d | %21s\n", i, allocator_name[i]);
  }
}
