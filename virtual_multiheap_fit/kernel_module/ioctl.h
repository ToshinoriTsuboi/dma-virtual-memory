/*
  Virtual Multiheap-fit is a space-saving dynamic memory allocator with
  virtual memory
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
#ifndef PAGE_ALLOCATOR_IOCTL_H__
#define PAGE_ALLOCATOR_IOCTL_H__

#include <linux/ioctl.h>

/* Magic number of the kernel module */
#define ALLOCATOR_IOC_MAGIC        0xbb
/* Allocate a page to the specified position */
#define ALLOCATOR_IOC_ALLOC        _IOW(ALLOCATOR_IOC_MAGIC, 0, unsigned long)
/* Deallocate a page to the specified position */
#define ALLOCATOR_IOC_DEALLOC      _IOW(ALLOCATOR_IOC_MAGIC, 1, unsigned long)
/* Change the number of pages to manage */
#define ALLOCATOR_IOC_RESIZE       _IOW(ALLOCATOR_IOC_MAGIC, 2, unsigned long)
/* Total size used in the module */
#define ALLOCATOR_IOC_TOTAL_SIZE   _IOR(ALLOCATOR_IOC_MAGIC, 3, unsigned long)
/* Set physical pagesize order in the module.
   New pagesize is set to 2^{arg + 12}. */
#define ALLOCATOR_IOC_SET_PAGESIZE_ORDER _IOW(ALLOCATOR_IOC_MAGIC, 4, unsigned)

/* Number of ioctl commands */
#define ALLOCATOR_IOC_MAXNR 5

#endif /* PAGE_ALLOCATOR_IOCTL_H__ */
