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
#ifndef INSTRUCTION_COUNTER_H__
#define INSTRUCTION_COUNTER_H__

/** This function needs to be called at the beginning of the program
    to measure and modify function call overhead */
void instruction_count_init(void);
/**
 * Set string
 * @param str  string printed at the end
 */
void instruction_count_set_string(const char* str);
/** Start instruction counting */
void instruction_count_start(void);
/** End instruction counting and print counts to standard err output */
void instruction_count_end(void);

#endif /* INSTRUCTION_COUNTER_H__ */
