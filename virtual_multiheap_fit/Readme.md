# Virtual Multiheap-fit

A space-saving dynamic memory allocator by mapping physical pages well.

**This repository is for experimental use, so it may contain some dangerous code.
Use this repository at your own risk.
Especially in Virtual Multiheap-fit, there is processing which requires
administrator authority, so there is a risk of disastrous error.**

## Requirement

The code is written assuming only when using GCC on Linux.
Probably it can not be compiled by compilers other than GCC.

The operation was checked only on Linux Kernel version 3.13.0, so
it may not work with other versions.

In order to compile the kernel module, you may have to install header files in
`/lib/modules`. These can be installed using a package management system such
as `apt`.

## Usage

First, kernel module should be compiled and inserted to the system.
Compiling can be done only once, but inserting should be done every time
OS is restarted.
These operations can be performed by the following command in
`kernel_module` directory:

```sh
make insmod
```

While executing the above command, you may be asked for administrator authority.

To reinstall the modified kernel module, you need to remove old kernel module
from the system by typing the following command:

```sh
make rmmod
```

This command may also be asked for administrator authority.

You can compile the library body simply by moving to `allocator` directory and
typing `make`.

## Example

`allocator/sample.c` is a sample code for operation confirmation.
Compile and run it as follows:

```sh
pushd kernel_module
make insmod
popd
cd allocator
make
gcc -I./include -o sample sample.c virtual_multiheap_fit.a -lm
./sample
```

## Notes

So many mappings can be created in
the kernel module of Virtual Multiheap-fit, so the number of it may exceed
the maximum number.
The maximum number of maps a process can make is written in the file
`/proc/sys/vm/max_map_count`.

In order to avoid this, add `vm.max_map_count = <huge number>` to
the file `/etc/sysctl.conf` and type `sudo sysctl -p` to refrect these changes.

For brevity of code, we do not assign a block number(bid) during `vmf_allocate`.
Therefore, it is necessary for the user to determine whether each block number
is currently in use or not.
