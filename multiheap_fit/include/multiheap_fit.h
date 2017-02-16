/*
  Multiheap-fit is a space-saving dynamic memory allocator with virtual memory
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
#ifndef MULTIHEAP_FIT_H__
#define MULTIHEAP_FIT_H__

#include <stddef.h>
#include <stdint.h>

/* Type of Multiheap-fit */
typedef void*  mf_t;
/* block id */
typedef uint32_t blockid_t;

/**
 * Initialize Multiheap-fit's internal data.
 * @param mem_min      min size of allocated block
 * @param mem_max      max size of allocated block
 * @param elem_nr_max  max number of allocated block
 * @param max_byte     max total allocated size
 * @return             multiheap-fit handler
 *
 * In the current implementation, our program cannot detect
 * whether total allocated size exceededs max_byte.
 */
mf_t mf_init(size_t mem_min, size_t mem_max,
  size_t elem_nr_max, size_t max_byte);

/**
 * Finalize Multiheap-fit's internal data.
 */
void mf_final(mf_t mf);

/**
 * allocate memory block
 * @param bid     allocating block id   [0, elem_nr_max)
 * @param length  required memory size  [mem_min, mem_max]
 *
 * The block id management must be done by the application.
 * The argument 'bid' must not comflict with other allocated block id.
 */
void mf_allocate(mf_t mf, blockid_t bid, size_t length);

/**
 * deallocate memory block
 * @param bid  deallocating block id
 *
 * This procedure might move another allocated block.
 */
void mf_deallocate(mf_t mf, blockid_t bid);

/**
 * change size of allocated memory block
 * @param bid         resizing block id
 * @param new_length  new block length
 *
 * This is an experimental function.
 */
void mf_reallocate(mf_t mf, blockid_t bid, size_t new_length);

/**
 * dereference memory block
 * @param elem_id  dereferencing block id
 * @return         current addres of the block
 *
 * The address of the block might move according to deallocating,
 * so the application must call this function immediately before using.
 */
void* mf_dereference(mf_t mf, blockid_t bid);

/**
 * constant version of mf_dereference
 */
const void* mf_dereference_c(const mf_t mf, blockid_t bid);

/**
 * calculate the length of the allocated memory block
 * @param  bid  allocated block id
 * @return      internal length of bid
 *
 * The return value is "internal" length of the block. It might be
 * longer than the size required in 'mf_allocate'.
 */
size_t mf_length(const mf_t mf, blockid_t bid);

/**
 * dereference and calculate the length of the allocated memory block.
 * @param  bid         allocated block id
 * @param  block_addr  used to store dereferenced address
 * @return             internal length of bid
 *
 * This function is faster than calling 'mf_dereference' and 'mf_length'.
 */
size_t mf_dereference_and_length(mf_t mf, blockid_t bid, void** block_addr);


/**
 * calculate total using size
 *
 * This function contains the for loop of size class. Therefore,
 * depending on how to determin size classes, this function will be slow.
 */
size_t mf_using_mem(const mf_t mf);

#endif /* MULTIHEAP_FIT_H__ */
