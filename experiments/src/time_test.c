#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <sys/time.h>
#include <sys/resource.h>

#include "allocator.h"
#include "memlog.h"

static void print_usage(const char* program_name);

int main(int argc, char* argv[]) {
  memlog_t* memlog;
  int allocator;
  size_t i;
  enum command_type type;
  size_t command_nr, idx, size;
  struct rusage start_usage, end_usage;
  int64_t elapsed_user, elapsed_system;

  if (argc < 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  memlog    = memlog_open(argv[1]);
  allocator = atoi(argv[2]);
  if (allocator >= ALLOC_NB) {
    fprintf(stderr, "allocator error\n");
    return EXIT_FAILURE;
  }

  init_funcs[allocator](memlog->mem_min, memlog->mem_max,
      memlog->block_max, memlog->require_size);

  if (getrusage(RUSAGE_SELF, &start_usage) < 0) {
    perror("getrusage");
    exit(EXIT_FAILURE);
  }

  command_nr = memlog->command_nr;
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
    }
  }

  if (getrusage(RUSAGE_SELF, &end_usage) < 0) {
    perror("getrusage");
    exit(EXIT_FAILURE);
  }

  elapsed_user =
    (int64_t)(end_usage.ru_utime.tv_sec - start_usage.ru_utime.tv_sec) * 1000000
    + (end_usage.ru_utime.tv_usec - start_usage.ru_utime.tv_usec);
  elapsed_system =
    (int64_t)(end_usage.ru_stime.tv_sec - start_usage.ru_stime.tv_sec) * 1000000
    + (end_usage.ru_stime.tv_usec - start_usage.ru_stime.tv_usec);
  printf("%s %" PRId64 " us user  %" PRId64 " us system  %" PRId64 " us total\n",
    allocator_name[allocator],
    elapsed_user, elapsed_system, elapsed_user + elapsed_system);

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
