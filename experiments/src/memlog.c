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
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include "memlog.h"

#define BUFFER_SIZE 1024

#define MEMLOG_MIN(x, y) ((x) < (y) ? (x) : (y))
#define MEMLOG_MAX(x, y) ((x) > (y) ? (x) : (y))

static inline void* safe_malloc(size_t size);
/**
 * Generate information about 'mem_min', 'mem_max', 'require_size'
 * from 'memlog'
 */
static void generate_stat_info(memlog_t* memlog);

memlog_t* memlog_open(const char* filename) {
  FILE* input_file;
  char str_buffer[BUFFER_SIZE];
  char command_chr;
  size_t idx, size;
  size_t idx_max = 0;
  memlog_t* memlog;
  enum command_type type;

  memlog = safe_malloc(sizeof(memlog_t));
  memlog->commands = safe_malloc(sizeof(command_t));
  memlog->command_nr = 0;

  input_file = fopen(filename, "r");
  if (input_file == NULL) {
    perror("fopen");
    exit(EXIT_FAILURE);
  }

  while (fgets(str_buffer, BUFFER_SIZE, input_file) != NULL) {
    command_chr = str_buffer[0];

    switch (command_chr) {
    case 'm': type = COMMAND_ALLOCATE;      break;
    case 'M': type = COMMAND_ALLOCATE_M;    break;
    case 'f': type = COMMAND_DEALLOCATE;    break;
    case 'F': type = COMMAND_DEALLOCATE_M;  break;
    case 'r': type = COMMAND_REALLOCATE;    break;
    case 'R': type = COMMAND_REALLOCATE_M;  break;
    case 'd': type = COMMAND_DEREFERENCE;   break;
    case 's': type = COMMAND_GETSIZE;       break;
    default:  type = COMMAND_UNKNOWN;
    }
    if (type ==COMMAND_UNKNOWN) continue;

    if (command_kind(type) == COMMAND_ALLOCATE ||
        command_kind(type) == COMMAND_REALLOCATE) {
      if (sscanf(str_buffer + 1, " %zu %zu", &idx, &size) < 2) {
        fprintf(stderr, "format error\n");
        exit(EXIT_FAILURE);
      }
      idx_max = MEMLOG_MAX(idx_max, idx);
      memlog->commands[memlog->command_nr].idx  = idx;
      memlog->commands[memlog->command_nr].size = size;
    } else if (command_kind(type) == COMMAND_DEALLOCATE) {
      if (sscanf(str_buffer + 1, " %zu", &idx) < 1) {
        fprintf(stderr, "format error\n");
        exit(EXIT_FAILURE);
      }
      memlog->commands[memlog->command_nr].idx = idx;
    }

    memlog->commands[memlog->command_nr].type = type;
    memlog->command_nr++;
    memlog->commands = realloc(memlog->commands,
      (memlog->command_nr + 1) * sizeof(command_t));
  }
  memlog->block_max = idx_max + 1;
  generate_stat_info(memlog);

  fclose(input_file);
  return memlog;
}

void memlog_finalize(memlog_t* memlog) {
  free(memlog->commands);
  free(memlog);
}

static inline void* safe_malloc(size_t size) {
  void* addr = malloc(size);

  if (addr == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  return addr;
}

static void generate_stat_info(memlog_t* memlog) {
  size_t command_idx;
  size_t* idx2size;
  size_t mem_min = SIZE_MAX;
  size_t mem_max = 0;
  size_t curr_size = 0;
  size_t require_size  = 0;
  command_t command;

  idx2size = safe_malloc(memlog->block_max * sizeof(size_t));
  for (command_idx = 0; command_idx < memlog->command_nr; ++command_idx) {
    command = memlog->commands[command_idx];
    if (command_kind(command.type) == COMMAND_ALLOCATE) {
      idx2size[command.idx] = command.size;
      mem_min = MEMLOG_MIN(mem_min, command.size);
      mem_max = MEMLOG_MAX(mem_max, command.size);
      curr_size += command.size;
      require_size = MEMLOG_MAX(require_size, curr_size);
    } else if (command_kind(command.type) == COMMAND_DEALLOCATE) {
      curr_size -= idx2size[command.idx];
    } else if (command_kind(command.type) == COMMAND_REALLOCATE) {
      mem_min = MEMLOG_MIN(mem_min, command.size);
      mem_max = MEMLOG_MAX(mem_max, command.size);
      curr_size = curr_size - idx2size[command.idx] + command.size;
      require_size = MEMLOG_MAX(require_size, curr_size);
      idx2size[command.idx] = command.size;
    }
  }
  free(idx2size);

  memlog->mem_min = mem_min;
  memlog->mem_max = mem_max;
  memlog->require_size = require_size;
}
