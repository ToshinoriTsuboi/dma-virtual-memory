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
#ifndef KVMALLOC_H__
#define KVMALLOC_H__

#include <linux/types.h> /* Necessary to use 'size_t' */

/* switch method with 1 MB as the boundary */
#define VMALLOC_SHRESHOLD 0x100000
/**  allocate memory with kmalloc /vmalloc */
void* kvmalloc(size_t size);
/**
 * Reallocate the memory allocated by 'kvmalloc'. 'old_size' is necessary
 * to know whether the memory was allocated using kmalloc or vmalloc.
 */
void* kvrealloc(void* old_address, size_t old_size, size_t new_size);
/** deallocate the memory allocated by 'kvmalloc' */
void kvfree(void* address, size_t size);

#endif /* KVMALLOC_H__ */
