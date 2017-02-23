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
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <stdbool.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>
#include <sys/ptrace.h>
#include <sys/wait.h>
#include <sys/user.h>
#include <sys/select.h>

#include "internal.h"
#include "instruction_counter.h"

#define READ_IDX  0
#define WRITE_IDX 1
#define BIAS_INIT_VALUE 0xdeadbeefull

int main(int argc, char* argv[], char* envp[]) {
  int pid, status;
  fd_set fds;
  struct timeval t;
  int pipe_child2parent[2];

  uint64_t iteration_count = 0;
  uint64_t counter_bias    = BIAS_INIT_VALUE;
  bool is_initializing = false;
  bool is_counting = false;
  char buffer[256];
  char tag_name[256];
  int size;

  snprintf(tag_name, sizeof(tag_name), "COUNT");

  if (argc <= 1) {
    fprintf(stderr, "usage  %s program [program args]\n", argv[0]);
    exit(EXIT_FAILURE);
  }

  if (pipe(pipe_child2parent) < 0) {
    perror("pipe child2parent failed");
    goto pipe_child2parent_failed;
  }

  pid = fork();
  if (pid < 0) {
    perror("fork");
    goto fork_failed;
  }

  if (pid == 0) {
    /* child process */
    dup2(pipe_child2parent[WRITE_IDX], PIPE_FD);
    close(pipe_child2parent[READ_IDX]);
    close(pipe_child2parent[WRITE_IDX]);

    ptrace(PTRACE_TRACEME, 0, NULL, NULL);
    execve(argv[1], argv + 1, envp);
    perror("execve");
    exit(EXIT_FAILURE);
  }

  /* parent process */
  close(pipe_child2parent[WRITE_IDX]);
  for (;;) {
    waitpid(pid, &status, 0);
    if (WIFEXITED(status)) break;

    FD_ZERO(&fds);
    FD_SET(pipe_child2parent[READ_IDX], &fds);
    t.tv_sec = t.tv_usec = 0;
    select(pipe_child2parent[READ_IDX] + 1, &fds, NULL, NULL, &t);
    if (FD_ISSET(pipe_child2parent[0], &fds)) {
      size = read(pipe_child2parent[READ_IDX], buffer, sizeof(buffer) - 1);
      buffer[size] = '\0';

      if (strcmp(buffer, START_STRING) == 0) {
        is_counting = true;
        iteration_count = 0;
      } else if (strcmp(buffer, END_STRING) == 0) {
        is_counting = false;
        if (!is_initializing) {
          if (counter_bias == BIAS_INIT_VALUE) {
            fprintf(stderr, "warning  iteration counter not initialized\n");
          } else {
            fprintf(stderr, "%s\t%8" PRIu64 "\n", tag_name,
              iteration_count - counter_bias);
          }
          fflush(stderr);
        } else {
          is_initializing = false;
          counter_bias = iteration_count;
        }
      } else if (strcmp(buffer, INIT_STRING) == 0) {
        is_initializing = true;
      } else if (strncmp(buffer, NAME_STRING, sizeof(NAME_STRING) - 1) == 0) {
        snprintf(tag_name, sizeof(tag_name), "%s",
          buffer + sizeof(NAME_STRING) - 1);
      }
    }

    if (is_counting) {
      ptrace(PTRACE_SINGLESTEP, pid, NULL, NULL);
      iteration_count++;
    } else {
      ptrace(PTRACE_SYSCALL, pid, NULL, NULL);
    }
  }

  return EXIT_SUCCESS;

fork_failed:
  close(pipe_child2parent[READ_IDX]);
  close(pipe_child2parent[WRITE_IDX]);
pipe_child2parent_failed:
  return EXIT_FAILURE;
}
