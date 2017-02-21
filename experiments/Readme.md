# Experiments

Numerical experiments for measuring Multiheap-fit and Virtual Multiheap-fit
performance

**This repository is for experimental use, so it may contain some dangerous code.
Use this repository at your own risk.**

## Requirement

The code is written assuming only when using GCC on Linux.
Probably it can not be compiled by compilers other than GCC.

## Usage

Simply typing `make` will generate executable files named `inst_test.out`,
`time_test.out` and `memory_test.out`.
Each program takes the same arguments: The path of the `.memlog` file and
allocator number. The `memlog` format will be described in detail in the
next section. Also, the allocator number can be confirmed with running the
program without arguments.

- `inst_test.out` : count the number of instructions of allocate / deallocate /
reallocate
- `time_test.out` : measure time taken for memory management
- `memory_test.out` : measure memory consumption of each allocator at each time

`inst_test.out` only must be run through `instruction_counter`.

## Memlog format

Memlog file is interpreted line by line.
The content of the file is an instruction to program X.
The following commands can be used:

- `m <idx> <size>` : allocate 'size' byte at idx-th block
- `f <idx>` : deallocate idx-th block
- `r <idx> <size>` : reallocate
- all lines starting with other characters are ignored

It is better to set idx as small as possible.

## Example

### `inst_test.out`

The following is sample code for counting the number of instruction of
the worst case of Multiheap-fit allocating.

```sh
../instruction_counter/inst_test.out ./inst_test.out bad_case/MFm.memlog 0
```

### `time_test.out`

The following is sample code for measuring the time taken for 'CFRAC' memtrace.

```sh
./time_test.out real_app/cfrac.memlog 0
```

### `memory_test.out`

The following is sample code for measuring memory consumption required for
'CFRAC' memtrace.

```sh
./memory_test.out real_app/cfrac.memlog 0 > 0.dat
(echo 'plot "0.dat"'; cat) | gnuplot
```

## Notes

`malloc.h` and `malloc.c` are written by Doug Lea (available
[here](http://g.oswego.edu/pub/misc/malloc.c) and
[here](http://g.oswego.edu/pub/misc/malloc.h)).

Our code supports [TLSF](http://www.gii.upv.es/tlsf/main/repo) and
[Compact-fit](http://tiptoe.cs.uni-salzburg.at/compact-fit/).
Due to licensing issues, these codes are not included in this repository.
The way of introduction is as follows.

### TLSF

Download and decompress
[TLSF-2.4.6.tbz2](http://www.gii.upv.es/tlsf/main/repo). Then,
copy `target.h`, `tlsf.h` and `tlsf.c` in `TLSF-2.4.6/src` to `experiments/src`.
Finally, apply the patch `tlsf.c.patch`.

By doing the above operation, TLSF is automatically enabled.

### Compact-fit

Download and decompress
[compact-fit-0.9.tar.gz](http://tiptoe.cs.uni-salzburg.at/compact-fit/).
Then, copy `arch_dep.h`, `cf.h` and `cf.c` in `compact-fit-0.9`
to `experiments/src`. Finally, apply the patch `cf.h.patch` and `cf.c.patch`.

By doing the above operation, Compact-fit is automatically enabled.
