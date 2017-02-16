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
#include <linux/module.h>
#include <linux/vmalloc.h>
#include <linux/slab.h>
#include <linux/string.h>

#include "kvmalloc.h"

/* Judge whether the memory should be allocated by vmalloc or kvmalloc  */
#define SHOULD_VMALLOC(size) ((size) >  VMALLOC_SHRESHOLD)
#define SHOULD_KMALLOC(size) ((size) <= VMALLOC_SHRESHOLD)

void* kvmalloc(size_t size) {
  if (SHOULD_VMALLOC(size)) {
    return vmalloc(size);
  } else {
    return kmalloc(size, GFP_KERNEL);
  }
}

void* kvrealloc(void* old_address, size_t old_size, size_t new_size) {
  void* new_address;
  size_t copy_size;

  if (old_address == NULL) {
    return NULL;
  } else if (SHOULD_KMALLOC(old_size) && SHOULD_KMALLOC(new_size)) {
    /* If the memory is allocated by 'kmalloc', it can easily be reallocated
       by 'krealloc'. */
    return krealloc(old_address, new_size, GFP_KERNEL);
  } else {
    /* It should be moved by 'memcpy' */
    new_address = kvmalloc(new_size);
    /* If memory allocation fails, there is no need to do anything */
    if (new_address == NULL) return NULL;

    copy_size = min(old_size, new_size);
    memcpy(new_address, old_address, copy_size);
    kvfree(old_address, old_size);

    return new_address;
  }
}

void kvfree(void* address, size_t size) {
  if (size > VMALLOC_SHRESHOLD) {
    vfree(address);
  } else {
    kfree(address);
  }
}
