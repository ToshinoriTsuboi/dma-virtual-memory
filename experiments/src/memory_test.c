/*
  Copyright (c) 2016-2017 Toshinori Tsuboi

  Permission is hereby granted, free of charge, to any person obtaining a copy of
  this software and associated documentation files (the "Software"), to deal in
  the Software without restriction, including without limitation the rights to
  use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
  the Software, and to permit persons to whom the Software is furnished to do so,
  subject to the following conditions:

  The above copyright notice and this permission notice shall be included in all
  copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS
  FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
  COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
  IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#include "allocator.h"
#include "memlog.h"

#include "allocator.h"
#include "memlog.h"

#ifndef LARGE_CASE
#  define LARGE_CASE 1
#endif

/** Maximum size of total requested memory.
  (This value is used in give_worst().)
  About 16 times MAX_REQUEST_SIZE might be used, so it
  should be set much smaller than RAM size of your computer. */
#if LARGE_CASE
#  define MAX_REQUEST_SIZE 67108864
#else
#  define MAX_REQUEST_SIZE 262144
#endif
/** Minimum value of requestable block size at once.
  Even if 1 byte block, most of allocators use some extra region.
  Therefore, minimum value is needed to prevent allocators from
  using huge amount of memory. */
#define MIN_BLOCK_SIZE 16
/** Maimum value of requestable blocks size at once.
  It is recommended to set it to 128 KB or less because
  DLmalloc uses mmap directly when allocating more than
  256 KB block. */
#if LARGE_CASE
#  define MAX_BLOCK_SIZE 131072
#else
#  define MAX_BLOCK_SIZE 4096
#endif

/** Structure to store allocated memory blocks */
typedef struct {
  /** Start address of the block */
  void*  addr;
  /** Length of the block */
  size_t len;
  /** Index used in allocator wrapper functions */
  size_t idx;
} memblock_t;

static void print_usage(const char* program_name);

/** Swap memory block */
static inline void swap_block(memblock_t* block1, memblock_t* block2) {
  memblock_t tmp = *block1;
  *block1 = *block2;
  *block2 = tmp;
}

/** Give memory worst case of no moving allocators.
  Test case is generated depending on the block arrangement.

  [Reference]
  J. M. Robson. 1974. Bounds for Some Functions Concerning
  Dynamic Storage Allocation. J. ACM 21, 3 (July 1974), 491-499.
 */
static void give_worst(int allocator);
/** Read memory trace by file and measure allocator's memory consumption  */
static void memory_trace(const char* filename, int allocator);
/** Print usage to standard output */
static void print_usage(const char* program_name);

/** Wrapped malloc function to exit on failure */
static void* safe_malloc(size_t size);
/** Calculate rounded up log arithmetic value */
static size_t integer_log(size_t n);
/** Compare operator to sort address in ascending order. */
static int qsort_compare(const void* left, const void* right);
/** Determine whether section[start, start + len) contains such digits
  that last `key_size` digits are equal to `key`. */
static inline bool is_inside(void* start, size_t len,
  size_t key, size_t key_size);

/** Calculate next key depending on the block arrangement. */
static size_t calc_next_key(memblock_t* memblocks, size_t used_idx,
  size_t key, size_t new_key_size);
/** Remove all such blocks that they don't contain key, and
  move the block that was not released to the front.
  The returnvalue is the number of blocks not released. */
static size_t remove_nokey_block(memblock_t* memblocks, size_t used_idx,
  int allocator, size_t key, size_t key_size, size_t* allocatable_size);
/** Allocate as many `block_size` blocks as possible
  to the index after `max_idx`. */
static void allocate_max(memblock_t* memblocks, size_t* max_idx,
  int allocator, size_t block_size, size_t* allocatable_size);
/** Update addresses of the blocks. */
static void update_reference(memblock_t* memblocks, size_t max_idx,
  int allocator);
/** Remove blocks by "gap rule". */
static size_t remove_by_gap_rule(memblock_t* memblocks, size_t used_idx,
  int allocator, size_t key, size_t key_size, size_t* allocatable_size);
/** Verify whether the block arrangement is correct or not. */
static inline bool verify_position(const memblock_t* memblocks, size_t num,
  size_t key, size_t count);

int main(int argc, char* argv[]) {
  int allocator;
  if (argc < 3) {
    print_usage(argv[0]);
    return EXIT_FAILURE;
  }

  allocator = atoi(argv[2]);
  if (allocator >= ALLOC_NB) {
    fprintf(stderr, "allocator error\n");
    exit(EXIT_FAILURE);
  }

  if (strcmp(argv[1], "--worst") == 0) {
    give_worst(allocator);
  } else {
    memory_trace(argv[1], allocator);
  }

  return EXIT_SUCCESS;
}

static void give_worst(int allocator) {
  memblock_t* memblocks;
  size_t i;
  size_t curr_size = MIN_BLOCK_SIZE;
  size_t log_curr_size;
  size_t key = 0;
  size_t log_max_bsize = integer_log(MAX_BLOCK_SIZE);
  size_t allocatable_size = 0;
  size_t used_idx = MAX_REQUEST_SIZE / MIN_BLOCK_SIZE;
  size_t new_used_idx;

  init_funcs[allocator](MIN_BLOCK_SIZE, MAX_BLOCK_SIZE,
    MAX_REQUEST_SIZE / MIN_BLOCK_SIZE, MAX_REQUEST_SIZE);
  memblocks = (memblock_t*)safe_malloc(sizeof(memblock_t) * used_idx);

  /* First, allocate as many MIN_BLOCK_SIZE blocks as possible */
  for (i = 0; i < used_idx; ++i) {
    allocate_funcs[allocator](i, curr_size);
    memblocks[i].addr = dereference_funcs[allocator](i);
    memblocks[i].len  = curr_size;
    memblocks[i].idx  = i;
  }

  curr_size <<= 1;
  for (log_curr_size = integer_log(MIN_BLOCK_SIZE) + 1;
      log_curr_size  < log_max_bsize;
      curr_size <<= 1, ++log_curr_size) {
    key = calc_next_key(memblocks, used_idx, key, log_curr_size);

    new_used_idx =
      remove_nokey_block(memblocks, used_idx, allocator, key,
        log_curr_size, &allocatable_size);
    allocate_max(memblocks, &new_used_idx, allocator,
      curr_size, &allocatable_size);

    used_idx = new_used_idx;
    assert(verify_position(memblocks, used_idx, key, log_curr_size));
    update_reference(memblocks, used_idx, allocator);
  }

  qsort((void*)memblocks, used_idx, sizeof(memblock_t), qsort_compare);
  new_used_idx = remove_by_gap_rule(memblocks, used_idx, allocator,
    key, log_curr_size, &allocatable_size);
  allocate_max(memblocks, &new_used_idx, allocator,
    curr_size, &allocatable_size);
  used_idx = new_used_idx;

  qsort((void*)memblocks, used_idx, sizeof(memblock_t), qsort_compare);
  printf("memory consumption -> %.03f\n",
    getsize_funcs[allocator]()/1024.0/1024.0);
  printf("theoritical bound  -> %.03f\n",
    (MAX_REQUEST_SIZE * (1 + integer_log(MAX_BLOCK_SIZE / MIN_BLOCK_SIZE) / 2)
    - MAX_BLOCK_SIZE + 1)/1024.0/1024.0);
}

static void memory_trace(const char* filename, int allocator) {
  memlog_t* memlog;
  size_t i;
  size_t curr_time;
  enum command_type type;
  size_t command_nr, idx, size;

  memlog = memlog_open(filename);
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
}

static void print_usage(const char* program_name) {
  int i;
  printf("%s <memlog file> <allocator number>\n", program_name);
  printf("If <memlog file> is set '--worst', max memory consumption case is ");
  printf("generated automatically.\n");
  putchar('\n');
  printf(" Number |        Allocator Name \n");
  printf("--------+-----------------------\n");
  for (i = 0; i < ALLOC_NB; ++i) {
    printf("%7d | %21s\n", i, allocator_name[i]);
  }
}

static void* safe_malloc(size_t size) {
  void* addr = malloc(size);
  if (addr == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }

  return addr;
}

static size_t integer_log(size_t n) {
  size_t ret = 0;
  --n;
  while (n > 0) {
    n >>= 1;
    ++ret;
  }
  return ret;
}

static int qsort_compare(const void* left, const void* right) {
  void* left_addr  = ((memblock_t*)left)->addr;
  void* right_addr = ((memblock_t*)right)->addr;
  if (left_addr < right_addr) {
    return - 1;
  } else if (left_addr == right_addr) {
    return 0;
  } else {
    return 1;
  }
}

static inline bool is_inside(void* start, size_t len,
    size_t key, size_t key_size) {
  /** Covering `key_size` digits */
  size_t mask = (1 << key_size) - 1;
  /** Used when carry over */
  size_t carry_key = key | (1 << key_size);
  /** extract last `key_size` digits of addr */
  size_t start_lower = (size_t)start & mask;
  size_t end_lower = start_lower + len;

  /* Section is large enough */
  if (len >= (1ull << key_size)) return true;
  if (start_lower <= key && key < end_lower) return true;
  if (start_lower <= carry_key && carry_key < end_lower) return true;
  return false;
}

static size_t calc_next_key(memblock_t* memblocks, size_t used_idx,
    size_t key, size_t new_key_size) {
  size_t odd_sum = 0;
  size_t even_sum = 0;
  size_t i;
  void* addr;
  size_t len;

  for (i = 0; i < used_idx; ++i) {
    addr = memblocks[i].addr;
    len  = memblocks[i].len;
    if (is_inside(addr, len, key, new_key_size)) {
      even_sum += len;
    } else {
      odd_sum += len;
    }
  }
  return key | (even_sum >= odd_sum ? 0 : (1 << (new_key_size - 1)));
}

static size_t remove_nokey_block(memblock_t* memblocks, size_t used_idx,
    int allocator, size_t key, size_t key_size, size_t* allocatable_size) {
  size_t i;
  size_t new_used_idx = 0;
  void*  addr;
  size_t len;

  for (i = 0; i < used_idx; ++i) {
    addr = memblocks[i].addr;
    len  = memblocks[i].len;
    if (is_inside(addr, len, key, key_size)) {
      swap_block(&memblocks[new_used_idx++], &memblocks[i]);
    } else {
      *allocatable_size += memblocks[i].len;
      deallocate_funcs[allocator](memblocks[i].idx);
      memblocks[i].len = 0;
    }
  }

  return new_used_idx;
}

static void allocate_max(memblock_t* memblocks, size_t* max_idx,
    int allocator, size_t block_size, size_t* allocatable_size) {
  size_t idx;
  while (*allocatable_size >= block_size) {
    idx = memblocks[*max_idx].idx;
    allocate_funcs[allocator](idx, block_size);
    memblocks[*max_idx].addr = dereference_funcs[allocator](idx);
    memblocks[*max_idx].len  = block_size;
    (*max_idx)++;
    *allocatable_size -= block_size;
  }
}

static void update_reference(memblock_t* memblocks, size_t max_idx,
    int allocator) {
  size_t i;
  size_t idx;

  /** No moving allocators don't have to update addresses of memory blocks */
  if (allocator == ALLOC_DL
#ifdef ENABLE_TLSF
      || allocator == ALLOC_TLSF
#endif
      ) {
    return;
  }

  for (i = 0; i < max_idx; ++i) {
    idx = memblocks[i].idx;
    memblocks[i].addr = dereference_funcs[allocator](idx);
  }
}

static size_t remove_by_gap_rule(memblock_t* memblocks, size_t used_idx,
    int allocator, size_t key, size_t key_size, size_t* allocatable_size) {
  size_t odd_sum  = 0;
  size_t even_sum = 0;
  size_t removed_side;
  size_t new_used_idx;
  void* last_used_addr;
  size_t i;
  size_t len;

  for (i = 0; i < used_idx; ++i) {
    if (i % 2 == 0) {
      even_sum += memblocks[i].len;
    } else {
      odd_sum += memblocks[i].len;
    }
  }

  removed_side   = (even_sum >= odd_sum ? 0 : 1);
  last_used_addr = (void*)0;
  new_used_idx   = 0;
  for (i = 0; i < used_idx; ++i) {
    if (i % 2 == removed_side) {
      len = ((char*)memblocks[i].addr - (char*)last_used_addr);
      if (is_inside(last_used_addr, len, key, key_size)) {
        swap_block(&memblocks[new_used_idx++], &memblocks[i]);
      } else {
        *allocatable_size += memblocks[i].len;
        deallocate_funcs[allocator](memblocks[i].idx);
        memblocks[i].len = 0;
      }
    } else {
      last_used_addr = (char*)memblocks[i].addr + memblocks[i].len;
      swap_block(&memblocks[new_used_idx++], &memblocks[i]);
    }
  }

  return new_used_idx;
}

static inline bool verify_position(const memblock_t* memblocks, size_t num,
    size_t key, size_t count) {
  size_t i;
  void* addr;
  size_t len;

  /* After moving `count` times, all blocks must contain such address
    that the last `count` digits are equal to `key`.  */
  for (i = 0; i < num; ++i) {
    addr = memblocks[i].addr;
    len  = memblocks[i].len;
    if (!is_inside(addr, len, key, count)) {
      return false;
    }
  }
  return true;
}
