/*
  Instruction Counter is a counter counting  number of instructions
  by `ptrace` system call
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
#include <ctype.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include "internal.h"
#include "instruction_counter.h"

void __attribute__((optimize("0"))) instruction_count_init(void) {
  int size;

  size = write(PIPE_FD, INIT_STRING, sizeof(INIT_STRING));
  if (size != sizeof(INIT_STRING)) exit(EXIT_FAILURE);

  instruction_count_start();
  instruction_count_end();
}

void instruction_count_set_string(const char* str) {
  char buf[32];
  char out_str[128];
  size_t i;
  char c;
  int size;

  if (str == NULL) str = "";

  for (i = 0; i < sizeof(buf) - 1; ++i) {
    c = str[i];
    if (!isprint(c)) break;
    buf[i] = c;
  }

  if (i == 0) {
    strcpy(buf, "COUNT");
    i = sizeof("COUNT") + 1;
  } else {
    buf[i] = '\0';
  }

  strcpy(out_str, NAME_STRING);
  strcat(out_str, buf);

  size = write(PIPE_FD, out_str, sizeof(NAME_STRING) - 1 + i);
  if (size != sizeof(NAME_STRING) - 1 + i) exit(EXIT_FAILURE);
}

void __attribute__((optimize("0"))) instruction_count_start(void) {
  int size;

  size = write(PIPE_FD, START_STRING, sizeof(START_STRING));
  if (size != sizeof(START_STRING)) exit(EXIT_FAILURE);
}

void __attribute__((optimize("0"))) instruction_count_end(void) {
  int size;

  size = write(PIPE_FD, END_STRING, sizeof(END_STRING));
  if (size != sizeof(END_STRING)) exit(EXIT_FAILURE);
}
