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
#ifndef ADDRESS_VECTOR_H__
#define ADDRESS_VECTOR_H__

#include <linux/types.h>  /* Necessary to use size_t */

/* Variable length array of void*. It behaves like 'std::vector<void *>'. */
typedef struct {
  /* number of addresses which is not NULL. */
  size_t length;
  /* variable length array of addresses. */
  void** addr_array;
  /* the length of variable length array */
  size_t array_size;
  /* physical page size stored in this structure */
  unsigned pagesize_order;
  /* max required page id */
  size_t id_max;
} address_vector_t;

/** allocate 'address_vector'. The memory allocated by this function
    should be deallocated by 'free_addr_vec'. */
address_vector_t* alloc_addr_vec(size_t init_array_size);
/** Initialize 'address_vector'. The memory allocated by this function
    should be finalized by 'final_addr_vec'. */
int init_addr_vec(address_vector_t* vector_ptr, size_t init_array_size);
/** Deallocate 'address_vector'. */
void free_addr_vec(address_vector_t* vector_ptr);
/** Finalize 'address_vector' */
void final_addr_vec(address_vector_t* vector_ptr);
/** Resize length of the array. This functioni does not
    necessarily call kvrealloc. */
int resize_addr_vec(address_vector_t* vector_ptr, size_t new_size);
/** Store 'addr' in 'idx' number of the array. */
int put_addr_vec(address_vector_t* vector_ptr, size_t index, void* addr);
/** Return 'addr' in 'idx' number of the array. */
void* get_addr_vec(address_vector_t* vector_ptr, size_t index);
/** Return the amount of memory used in 'vector_ptr'. */
size_t get_size_addr_vec(address_vector_t* vector_ptr);
/** Set physical pagesize.
  Pagesize can be changed only if the array is empty. */
int set_pagesize_order(address_vector_t* vector_ptr, unsigned pagesize_order);
/** Get order of current physical pagesize. */
unsigned get_pagesize_order(address_vector_t* vector_ptr);
/** Get current physical pagesize.  */
unsigned long get_pseudo_pagesize(address_vector_t* vector_ptr);

#endif /* ADDRESS_VECTOR_H__ */
