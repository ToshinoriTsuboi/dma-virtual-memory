/*
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
#ifndef ALLOCATORS_H__
#define ALLOCATORS_H__

#include <stddef.h>  /* size_t */

/* Allocator IDs */
enum Allocator {
  ALOC_MF,    /* Multiheap-fit */
  ALLOC_VMF,  /* Virtual Multiheap-fit */
  ALLOC_DL,   /* DL malloc */

#ifdef ENABLE_TLSF
  ALLOC_TLSF, /* TLSF(Two-Level Segregated Fit) */
#endif /* ENABLE_TLSF */

#ifdef ENABLE_CF
  ALLOC_CF,   /* Compact-fit */
#endif /* ENABLE_CF */

  ALLOC_NB,  /* number of allocators */
};

/* An abstracted types of allocator's operation */
typedef void (*init_t)(size_t mem_min, size_t mem_max,
  size_t id_num, size_t require_size);
typedef void (*allocate_t)(size_t idx, size_t size);
typedef void (*deallocate_t)(size_t idx);
typedef void (*reallocate_t)(size_t idx, size_t size);
typedef void* (*dereference_t)(size_t idx);
typedef size_t (*getsize_t)(void);

/* Wrapped allocator functions */
extern const init_t        init_funcs[ALLOC_NB];
extern const allocate_t    allocate_funcs[ALLOC_NB];
extern const deallocate_t  deallocate_funcs[ALLOC_NB];
extern const reallocate_t  reallocate_funcs[ALLOC_NB];
extern const dereference_t dereference_funcs[ALLOC_NB];
extern const getsize_t     getsize_funcs[ALLOC_NB];
extern const char*   allocator_name[ALLOC_NB];

#ifdef INSTRUCTION_COUNTER_ENABLE
extern const allocate_t    allocate_measure_funcs[ALLOC_NB];
extern const deallocate_t  deallocate_measure_funcs[ALLOC_NB];
extern const reallocate_t  reallocate_measure_funcs[ALLOC_NB];
#endif /* instruction */

#endif /* ALLOCATORS_H__ */
