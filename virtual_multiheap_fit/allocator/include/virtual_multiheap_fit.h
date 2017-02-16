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
#ifndef VIRTUAL_MULTIHEAP_FIT_H__
#define VIRTUAL_MULTIHEAP_FIT_H__

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

/* Type of Virtual Multiheap-fit */
typedef void* vmf_t;
/* block id */
typedef uint32_t blockid_t;

/**
 * Initialize Virtual Multiheap-fit's internal data.
 * @param mem_min      min size of allocated block
 * @param mem_max      max size of allocated block
 * @param elem_nr_max  max number of allocated block
 * @param max_byte     max total allocated size
 * @return             multiheap-fit handler
 *
 * In the current implementation, our program cannot detect
 * whether total allocated size exceededs max_byte.
 */
vmf_t vmf_init(size_t mem_min, size_t mem_max,
    size_t element_nr_max, size_t total_sup);

/**
 * Finalize Virtual Multiheap-fit's internal data.
 */
void vmf_final(vmf_t vmf);

/**
 * allocate memory block
 * @param bid     allocating block id   [0, elem_nr_max)
 * @param length  required memory size  [mem_min, mem_max]
 *
 * The block id management must be done by the application.
 * The argument 'bid' must not comflict with other allocated block id.
 */
void vmf_allocate(vmf_t vmf, blockid_t bid, size_t length);

/**
 * deallocate memory block
 * @param bid  deallocating block id
 *
 * This procedure might move another allocated block.
 */
void vmf_deallocate(vmf_t vmf, blockid_t bid);

/**
 * change size of allocated memory block
 * @param bid         resizing block id
 * @param new_length  new block length
 *
 * This is an experimental function.
 */
void vmf_reallocate(vmf_t vmf, blockid_t bid, size_t new_length);

/**
 * dereference memory block
 * @param elem_id  dereferencing block id
 * @return         current addres of the block
 *
 * The address of the block might move according to deallocating,
 * so the application must call this function immediately before using.
 */
void* vmf_dereference(vmf_t vmf, blockid_t bid);

/**
 * constant version of vmf_dereference
 */
const void* vmf_dereference_c(const vmf_t vmf, blockid_t bid);

/**
 * calculate the length of the allocated memory block
 * @param  bid  allocated block id
 * @return      internal length of bid
 *
 * The return value is "internal" length of the block. It might be
 * longer than the size required in 'mf_allocate'.
 */
size_t vmf_length(const vmf_t vmf, blockid_t bid);

/**
 * dereference and calculate the length of the allocated memory block.
 * @param  bid         allocated block id
 * @param  block_addr  used to store dereferenced address
 * @return             internal length of bid
 *
 * This function is faster than calling 'vmf_dereference' and 'vmf_length'.
 */
size_t vmf_dereference_and_length(vmf_t vmf, blockid_t bid, void** block_addr);


/**
 * calculate total using size
 *
 * This function contains the for loop of size class. Therefore,
 * depending on how to determin size classes, this function will be slow.
 */
size_t vmf_using_mem(const vmf_t vmf);

#endif /* VIRTUAL_MULTIHEAP_FIT_H__ */
