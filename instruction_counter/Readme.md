# Instruction Counter

Counting number of instructions by `ptrace` system call

**This repository is for experimental use, so it may contain some dangerous code.
Use this repository at your own risk.**

## Requirement

The code is written assuming only when using GCC on Linux.
Probably it can not be compiled by compilers other than GCC.

Code that assumes x64 architecture is included. In particular, it does not
work on the ARM architecture.

## Usage

Simply typing `make` will generate a library file named `inst_counter.a` and
executable file named `inst_counter.out`.

To count the number of instructions, first call `instruction_count_init`.
Then surround the range of the code you want to measure with
`instruction_count_start` and `instruction_count_end`.
In order to accurately measure, it is desirable to compile the range to be
measured without optimizing it (by using something like
`__attribute__((optimize("0")))`).
If you call `instruction_count_set_string` just before the measurement,
you can change the string printed when measurement is over.

To execute a program which contains measuring code, execute it as follows:

```sh
'inst_counter.out' <program> [<program arguments>...]
```

## Example

`sample.c` is a sample code for operation confirmation.
Compile and run it as follows:

```sh
make
gcc -I./include -o sample sample.c inst_counter.a
./inst_counter.out ./sample
```
