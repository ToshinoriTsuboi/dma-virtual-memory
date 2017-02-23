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
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include "address_vector.h"
#include "page_manage.h"

#define PRINT_ENTERING() printk(KERN_ALERT "Entering %s\n", __FUNCTION__)

int allocate_page(address_vector_t* vector_ptr, size_t index) {
  void* page_address;

  if (get_addr_vec(vector_ptr, index) != NULL) return 0;
  page_address =
      (void*) __get_free_pages(GFP_HIGHUSER, vector_ptr->pagesize_order);
  if (page_address == NULL) {
    printk(KERN_ALERT "%s: __get_free_page failed\n", __FUNCTION__);
    return -ENOMEM;
  }

  return put_addr_vec(vector_ptr, index, page_address);
}

int deallocate_page(address_vector_t* vector_ptr, size_t index) {
  void* page_address;

  page_address = get_addr_vec(vector_ptr, index);
  if (page_address == NULL) return 0;
  free_pages((unsigned long) page_address, vector_ptr->pagesize_order);
  return put_addr_vec(vector_ptr, index, NULL);
}

int freeall_page(address_vector_t* vector_ptr) {
  void* page_address;
  size_t size = vector_ptr->array_size;
  size_t index;

  for (index = 0; index < size; ++index) {
    page_address = get_addr_vec(vector_ptr, index);
    if (page_address == NULL) continue;
    free_pages((unsigned long) page_address, vector_ptr->pagesize_order);
  }

  return 0;
}
