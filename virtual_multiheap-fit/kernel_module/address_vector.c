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
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/string.h>
#include "address_vector.h"
#include "kvmalloc.h"

#define ARRAY_INIT_SIZE 2
#define ARRAY_MIN_SIZE  ARRAY_INIT_SIZE
#define MAX(x, y) (x) < (y) ? (y) : (x)

/* A function used when the size of the array increases */
static int bulge_addr_vec(address_vector_t* vector_ptr, size_t new_size);
/* A function used when the size of the array decreases */
static int realloc_addr_vec(
    address_vector_t* vector_ptr, size_t new_array_size);

address_vector_t* alloc_addr_vec(size_t init_array_size) {
  address_vector_t* vector_ptr;
  int err_code;

  vector_ptr =
    (address_vector_t*) kmalloc(sizeof(address_vector_t), GFP_KERNEL);
  if (vector_ptr == NULL) {
    printk(KERN_ALERT "%s: kmalloc failed\n", __FUNCTION__);
    return NULL;
  }

  err_code = init_addr_vec(vector_ptr, init_array_size);
  if (err_code < 0) {
    kfree(vector_ptr);
    return NULL;
  }
  vector_ptr->pagesize_order = 0;
  vector_ptr->id_max         = 0;
  return vector_ptr;
}

int init_addr_vec(address_vector_t* vector_ptr, size_t init_array_size) {
  void** addr_array;

  init_array_size = MAX(init_array_size, ARRAY_INIT_SIZE);

  addr_array = (void**) kvmalloc(sizeof(void*) * init_array_size);
  if (addr_array == NULL) {
    printk(KERN_ALERT "%s: vmalloc/kmalloc failed\n", __FUNCTION__);
    return -ENOMEM;
  }

  vector_ptr->addr_array = addr_array;
  vector_ptr->length     = 0;
  vector_ptr->array_size = init_array_size;

  memset(vector_ptr->addr_array, 0, sizeof(void*) * init_array_size);
  return 0;
}

void free_addr_vec(address_vector_t* vector_ptr) {
  final_addr_vec(vector_ptr);
  kfree(vector_ptr);
}

void final_addr_vec(address_vector_t* vector_ptr) {
  kvfree(vector_ptr->addr_array, vector_ptr->array_size * sizeof(void*));
  vector_ptr->addr_array = NULL;
}

int resize_addr_vec(address_vector_t* vector_ptr, size_t new_size) {
  if (new_size < vector_ptr->array_size) {
    printk(KERN_ALERT "%s: shrink is not supported", __FUNCTION__);
    return - EINVAL;
  } else if (new_size > vector_ptr->array_size) {
    return bulge_addr_vec(vector_ptr, new_size);
  } else {
    return 0;
  }
}

int put_addr_vec(address_vector_t* vector_ptr, size_t index, void* addr) {
  if (index >= vector_ptr->array_size) {
    printk(KERN_ALERT "%s: index >= array_size\n", __FUNCTION__);
    return -EINVAL;
  } else {
    vector_ptr->id_max = MAX(vector_ptr->id_max, index);
    if (vector_ptr->addr_array[index] == NULL) vector_ptr->length++;
    if (addr == NULL) vector_ptr->length--;

    vector_ptr->addr_array[index] = addr;
    return 0;
  }
}

void* get_addr_vec(address_vector_t* vector_ptr, size_t index) {
  if (index >= vector_ptr->array_size) {
    printk(KERN_ALERT "%s: index >= array_size\n", __FUNCTION__);
    return NULL;
  }

  return vector_ptr->addr_array[index];
}

static int bulge_addr_vec(address_vector_t* vector_ptr, size_t new_length) {
  size_t old_array_size = vector_ptr->array_size;
  size_t new_array_size = new_length;
  int err_code;

  err_code = realloc_addr_vec(vector_ptr, new_array_size);
  if (err_code >= 0) {
    memset(vector_ptr->addr_array + old_array_size, 0,
        sizeof(void*) * (new_array_size - old_array_size));
  }
  return err_code;
}

static int realloc_addr_vec(
    address_vector_t* vector_ptr, size_t new_array_size) {
  void* old_addr_array;
  void* new_addr_array;
  if (new_array_size == vector_ptr->array_size) return 0;

  old_addr_array = vector_ptr->addr_array;
  new_addr_array = kvrealloc(
    old_addr_array,
    vector_ptr->array_size * sizeof(void*),
    new_array_size * sizeof(void*));
  if (new_addr_array == NULL) {
    printk(KERN_ALERT "%s: kvrealloc failed\n", __FUNCTION__);
  }

  vector_ptr->addr_array = new_addr_array;
  vector_ptr->array_size = new_array_size;
  return 0;
}

size_t get_size_addr_vec(address_vector_t* vector_ptr) {
  return sizeof(address_vector_t)
    + vector_ptr->length * (1ULL << (12 + vector_ptr->pagesize_order))
    + vector_ptr->id_max * sizeof(void*);
}

int set_pagesize_order(address_vector_t* vector_ptr,
    unsigned pagesize_order) {
  if (vector_ptr->length == 0) {
    vector_ptr->pagesize_order = pagesize_order;
    return 0;
  } else {
    return -EINVAL;
  }
}

unsigned get_pagesize_order(address_vector_t* vector_ptr) {
  return vector_ptr->pagesize_order;
}

unsigned long get_pseudo_pagesize(address_vector_t* vector_ptr) {
  return 1ul << (vector_ptr->pagesize_order + 12);
}
