# Multiheap-fit

A space-saving dynamic memory allocator by having a lot of heaps

**This repository is for experimental use, so it may contain some dangerous code.
Use this repository at your own risk.**

## Requirement

The code is written assuming only when using GCC on Linux.
Probably it can not be compiled by compilers other than GCC.

## Usage

Simply typing `make` will generate a library file named `multiheap-fit.a`.
When using this library, a link to C mathematic functions may be necessary.

## Example

`sample.c` is a sample code for operation confirmation.
Compile and run it as follows:

```sh
make
gcc -I./include -o sample sample.c multiheap_fit.a -lm
./sample
```

## Notes

For brevity of code, we do not assign a block number(bid) during `mf_allocate`.
Therefore, it is necessary for the user to determine whether each block number
is currently in use or not.
