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
#ifndef MEMLOG_H__
#define MEMLOG_H__

#include <stddef.h>
#include <stdbool.h>
#include <inttypes.h>

/* memlog operations */
enum command_type {
  COMMAND_ALLOCATE    = 0,
  COMMAND_DEALLOCATE  = 1,
  COMMAND_REALLOCATE  = 2,
  COMMAND_DEREFERENCE = 3,
  COMMAND_GETSIZE     = 4,

  COMMAND_MEASURE_FLAG  = 8,
  COMMAND_ALLOCATE_M    = COMMAND_ALLOCATE    | COMMAND_MEASURE_FLAG,
  COMMAND_DEALLOCATE_M  = COMMAND_DEALLOCATE  | COMMAND_MEASURE_FLAG,
  COMMAND_REALLOCATE_M  = COMMAND_REALLOCATE  | COMMAND_MEASURE_FLAG,
  COMMAND_UNKNOWN       = COMMAND_REALLOCATE_M + 1,
};

static inline enum command_type command_kind(enum command_type type) {
  return (enum command_type)(type & (~COMMAND_MEASURE_FLAG));
}

typedef struct {
  /* Type of the command */
  enum command_type type;
  /* ID of block to allocate/deallocate/... */
  size_t idx;
  /* Size of block to allocate/reallocate */
  size_t size;
} command_t;

/* Structure for keeping the contents of the memlog file in RAM */
typedef struct {
  /* Array of commands read from the memlog file */
  command_t* commands;
  /* Number of commands in 'commands' */
  size_t command_nr;

  /* Minimum allocated block size */
  size_t mem_min;
  /* Maximum allocated block size */
  size_t mem_max;
  /* Maximum number of block */
  size_t block_max;
  /* Maximum size of allocated blocks */
  size_t require_size;
} memlog_t;

/* Read the file 'filename' and store information to the structure */
memlog_t* memlog_open(const char* filename);
/* Finalize the structure */
void memlog_finalize(memlog_t* memlog);

#endif /* MEMLOG_H__ */
