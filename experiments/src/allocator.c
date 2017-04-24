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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "allocator.h"
#include "multiheap_fit.h"
#include "virtual_multiheap_fit.h"
#include "malloc.h"

#ifdef INSTRUCTION_COUNTER_ENABLE
#include "instruction_counter.h"
#endif

#ifdef ENABLE_TLSF
#include "tlsf.h"
#endif

#ifdef ENABLE_CF
#include <sys/mman.h>
#include "cf.h"
#endif

/* In memory testing, TLSF allocator requires additional processing. */
#ifndef MEMORY_TEST
#  define MEMORY_TEST 0
#endif

#ifdef INSTRUCTION_COUNTER_ENABLE
#define NO_OPTIMIZE __attribute__((optimize("0")))
#endif /* INSTRUCTION_COUNTER_ENABLE */

#define ALLOCATOR_MAX(x, y) ((x) > (y) ? (x) : (y))
#define ALLOCATOR_MIN(x, y) ((x) < (y) ? (x) : (y))

/* structure to store addresses and lengths of memory blocks. */
typedef struct {
  void** addrs;
  size_t id_num;

#if MEMORY_TEST
  void*   addr_min;
  void*   addr_max;
  size_t* lens;
#endif /* MEMORY_TEST */
} block_info_t;

static block_info_t g_binfo;
static inline void* ptr_diff(void* addr, ptrdiff_t diff);
static inline void binfo_init(size_t id_num);
static inline void binfo_malloc(size_t idx, void* addr, size_t len);
static inline void binfo_free(size_t idx);
static inline void binfo_realloc(size_t idx, void* new_addr, size_t new_len);
static inline void* binfo_dereference(size_t idx);
#if MEMORY_TEST
static inline size_t binfo_getsize(void);
#endif /* MEMORY_TEST */

/* Multiheap-fit */
static mf_t mf;
static void init_mf(size_t mem_min, size_t mem_max,
    size_t id_num, size_t require_size) {
  mf = mf_init(mem_min, mem_max, id_num, require_size);
}

static void allocate_mf(size_t idx, size_t size) {
  mf_allocate(mf, idx, size);
}

static void deallocate_mf(size_t idx) {
  mf_deallocate(mf, idx);
}

static void reallocate_mf(size_t idx, size_t size) {
  mf_reallocate(mf, idx, size);
}

static void* dereference_mf(size_t idx) {
  return mf_dereference(mf, idx);
}

static size_t getsize_mf(void) {
  return mf_using_mem(mf);
}

#ifdef INSTRUCTION_COUNTER_ENABLE
static void NO_OPTIMIZE allocate_measure_mf(size_t idx, size_t size) {
  instruction_count_start();
  mf_allocate(mf, idx, size);
  instruction_count_end();
}

static void NO_OPTIMIZE deallocate_measure_mf(size_t idx) {
  instruction_count_start();
  mf_deallocate(mf, idx);
  instruction_count_end();
}

static void NO_OPTIMIZE reallocate_measure_mf(size_t idx, size_t size) {
  instruction_count_start();
  mf_reallocate(mf, idx, size);
  instruction_count_end();
}
#endif /* INSTRUCTION_COUNTER_ENABLE */

/* Virtual Multiheap-fit */
static vmf_t vmf;
static void init_vmf(size_t mem_min, size_t mem_max,
    size_t id_num, size_t require_size) {
  vmf = vmf_init(mem_min, mem_max, id_num, require_size);
}

static void allocate_vmf(size_t idx, size_t size) {
  vmf_allocate(vmf, idx, size);
}

static void deallocate_vmf(size_t idx) {
  vmf_deallocate(vmf, idx);
}

static void reallocate_vmf(size_t idx, size_t size) {
  vmf_reallocate(vmf, idx, size);
}

static void* dereference_vmf(size_t idx) {
  return vmf_dereference(vmf, idx);
}

static size_t getsize_vmf(void) {
  return vmf_using_mem(vmf);
}

#ifdef INSTRUCTION_COUNTER_ENABLE
static void NO_OPTIMIZE allocate_measure_vmf(size_t idx, size_t size) {
  instruction_count_start();
  vmf_allocate(vmf, idx, size);
  instruction_count_end();
}

static void NO_OPTIMIZE deallocate_measure_vmf(size_t idx) {
  instruction_count_start();
  vmf_deallocate(vmf, idx);
  instruction_count_end();
}

static void NO_OPTIMIZE reallocate_measure_vmf(size_t idx, size_t size) {
  instruction_count_start();
  vmf_reallocate(vmf, idx, size);
  instruction_count_end();
}
#endif /* INSTRUCTION_COUNTER_ENABLE */

/* DLmalloc */
static mspace msp;
static void init_dl(size_t mem_min, size_t mem_max,
    size_t id_num, size_t require_size) {
  msp = create_mspace(0, 0);
  binfo_init(id_num);
}

static void allocate_dl(size_t idx, size_t size) {
  void* addr = mspace_malloc(msp, size);
  binfo_malloc(idx, addr, size);
}

static void deallocate_dl(size_t idx) {
  void* addr = binfo_dereference(idx);
  mspace_free(msp, addr);
  binfo_free(idx);
}

static void reallocate_dl(size_t idx, size_t size) {
  void* old_addr = binfo_dereference(idx);
  void* new_addr = mspace_realloc(msp, old_addr, size);
  binfo_realloc(idx, new_addr, size);
}

static void* dereference_dl(size_t idx) {
  return binfo_dereference(idx);
}

static size_t getsize_dl(void) {
#if !MEMORY_TEST
  return mspace_footprint(msp);
#else
  return binfo_getsize();
#endif
}

#ifdef INSTRUCTION_COUNTER_ENABLE
static void NO_OPTIMIZE allocate_measure_dl(size_t idx, size_t size) {
  void* addr;
  instruction_count_start();
  addr = mspace_malloc(msp, size);
  instruction_count_end();

  binfo_malloc(idx, addr, size);
}

static void NO_OPTIMIZE deallocate_measure_dl(size_t idx) {
  void* addr = binfo_dereference(idx);
  instruction_count_start();
  mspace_free(msp, addr);
  instruction_count_end();

  binfo_free(idx);
}

static void NO_OPTIMIZE reallocate_measure_dl(size_t idx, size_t size) {
  void* old_addr = binfo_dereference(idx);
  void* new_addr;
  instruction_count_start();
  new_addr = mspace_realloc(msp, old_addr, size);
  instruction_count_end();

  binfo_realloc(idx, new_addr, size);
}
#endif /* INSTRUCTION_COUNTER_ENABLE */

#ifdef ENABLE_TLSF
/* TLSF */
static void init_tlsf(size_t mem_min, size_t mem_max,
    size_t id_num, size_t require_size) {
  void* addr;
  binfo_init(id_num);

  addr = tlsf_malloc(1024);
  tlsf_free(addr);
}

static void allocate_tlsf(size_t idx, size_t size) {
  void* addr = tlsf_malloc(size);
  binfo_malloc(idx, addr, size);
}

static void deallocate_tlsf(size_t idx) {
  void* addr = binfo_dereference(idx);
  tlsf_free(addr);
  binfo_free(idx);
}

static void reallocate_tlsf(size_t idx, size_t size) {
  void* old_addr = binfo_dereference(idx);
  void* new_addr = tlsf_realloc(old_addr, size);
  binfo_realloc(idx, new_addr, size);
}

static void* dereference_tlsf(size_t idx) {
  return binfo_dereference(idx);
}

static size_t getsize_tlsf(void) {
#if MEMORY_TEST
  return binfo_getsize() + tlsf_impl_overhead();
#else
  return 0;
#endif
}

#ifdef INSTRUCTION_COUNTER_ENABLE
static void NO_OPTIMIZE allocate_measure_tlsf(size_t idx, size_t size) {
  void* addr;
  instruction_count_start();
  addr = tlsf_malloc(size);
  instruction_count_end();

  binfo_malloc(idx, addr, size);
}

static void NO_OPTIMIZE deallocate_measure_tlsf(size_t idx) {
  void* addr = binfo_dereference(idx);
  instruction_count_start();
  tlsf_free(addr);
  instruction_count_end();

  binfo_free(idx);
}

static void NO_OPTIMIZE reallocate_measure_tlsf(size_t idx, size_t size) {
  void* old_addr = binfo_dereference(idx);
  void* new_addr = binfo_dereference(idx);
  instruction_count_start();
  new_addr = tlsf_realloc(old_addr, size);
  instruction_count_end();

  binfo_realloc(idx, new_addr, size);
}
#endif /* INSTRUCTION_COUNTER_ENABLE */
#endif /* ENABLE_TLSF */

/* Compact-fit */
#ifdef ENABLE_CF
void*   cf_pool;
void*** cf_addrs;
size_t* cf_sizes;
static void init_cf(size_t mem_min, size_t mem_max,
    size_t id_num, size_t require_size) {
  size_t pool_size = (512 * 1024 * 1024);

#ifdef MAP_32BIT
  cf_pool = mmap(NULL, pool_size, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_32BIT | MAP_ANONYMOUS, -1, 0);
#else
  cf_pool = mmap(NULL, pool_size, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
#endif
  if (cf_pool == MAP_FAILED) {
    perror("mmap");
    exit(EXIT_FAILURE);
  }

  cf_init(pool_size, cf_pool);
  cf_addrs = malloc(sizeof(void*) * id_num);
  cf_sizes = malloc(sizeof(size_t) * id_num);
  if (cf_addrs == NULL || cf_sizes == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  memset(cf_sizes, 0, sizeof(size_t) * id_num);
}

static void allocate_cf(size_t idx, size_t size) {
  cf_addrs[idx] = cf_malloc(size);
  cf_sizes[idx] = size;
}

static void deallocate_cf(size_t idx) {
  cf_free(cf_addrs[idx]);
}

static void reallocate_cf(size_t idx, size_t size) {
  size_t copy_size = ALLOCATOR_MIN(size, cf_sizes[idx]);
  void** new_addr;

  new_addr = cf_malloc(size);
  memcpy(*new_addr, *cf_addrs[idx], copy_size);
  cf_free(cf_addrs[idx]);
  cf_addrs[idx] = new_addr;
  cf_sizes[idx] = size;
}

static void* dereference_cf(size_t idx) {
  return *cf_addrs[idx];
}

static size_t getsize_cf(void) {
  return cf_get_using_size();
}

#ifdef INSTRUCTION_COUNTER_ENABLE
static void NO_OPTIMIZE allocate_measure_cf(size_t idx, size_t size) {
  void* addr;
  instruction_count_start();
  addr = cf_malloc(size);
  instruction_count_end();
  cf_addrs[idx] = addr;
  cf_sizes[idx] = size;
}

static void NO_OPTIMIZE deallocate_measure_cf(size_t idx) {
  void* addr = cf_addrs[idx];
  instruction_count_start();
  cf_free(addr);
  instruction_count_end();
}

static void NO_OPTIMIZE reallocate_measure_cf(size_t idx, size_t size) {
  size_t copy_size = ALLOCATOR_MIN(size, cf_sizes[idx]);
  void** old_addr = cf_addrs[idx];
  void** new_addr;

  instruction_count_start();
  new_addr = cf_malloc(size);
  memcpy(*new_addr, *old_addr, copy_size);
  cf_free(old_addr);
  instruction_count_end();
  cf_addrs[idx] = new_addr;
  cf_sizes[idx] = size;
}
#endif /* INSTRUCTION_COUNTER_ENABLE */
#endif /* ENABLE_CF */

const init_t        init_funcs[ALLOC_NB] = {
  init_mf, init_vmf, init_dl,
#ifdef ENABLE_TLSF
  init_tlsf,
#endif
#ifdef ENABLE_CF
  init_cf,
#endif
};

const allocate_t    allocate_funcs[ALLOC_NB] = {
  allocate_mf, allocate_vmf, allocate_dl,
#ifdef ENABLE_TLSF
  allocate_tlsf,
#endif
#ifdef ENABLE_CF
  allocate_cf,
#endif
};

const deallocate_t  deallocate_funcs[ALLOC_NB] = {
  deallocate_mf, deallocate_vmf, deallocate_dl,
#ifdef ENABLE_TLSF
  deallocate_tlsf,
#endif
#ifdef ENABLE_CF
  deallocate_cf,
#endif
};

const reallocate_t  reallocate_funcs[ALLOC_NB] = {
  reallocate_mf, reallocate_vmf, reallocate_dl,
#ifdef ENABLE_TLSF
  reallocate_tlsf,
#endif
#ifdef ENABLE_CF
  reallocate_cf,
#endif
};

const dereference_t dereference_funcs[ALLOC_NB] = {
  dereference_mf, dereference_vmf, dereference_dl,
#ifdef ENABLE_TLSF
  dereference_tlsf,
#endif
#ifdef ENABLE_CF
  dereference_cf,
#endif
};

const getsize_t     getsize_funcs[ALLOC_NB] = {
  getsize_mf, getsize_vmf, getsize_dl,
#ifdef ENABLE_TLSF
  getsize_tlsf,
#endif
#ifdef ENABLE_CF
  getsize_cf,
#endif
};

const char*   allocator_name[ALLOC_NB] = {
  "Multiheap-fit", "Virtual Multiheap-fit", "DLmalloc",
#ifdef ENABLE_TLSF
  "TLSF",
#endif
#ifdef ENABLE_CF
  "Compact-fit"
#endif
};

#ifdef INSTRUCTION_COUNTER_ENABLE
const allocate_t allocate_measure_funcs[ALLOC_NB] = {
  allocate_measure_mf, allocate_measure_vmf, allocate_measure_dl,
#ifdef ENABLE_TLSF
  allocate_measure_tlsf,
#endif
#ifdef ENABLE_CF
  allocate_measure_cf,
#endif
};

const deallocate_t deallocate_measure_funcs[ALLOC_NB] = {
  deallocate_measure_mf, deallocate_measure_vmf, deallocate_measure_dl,
#ifdef ENABLE_TLSF
  deallocate_measure_tlsf,
#endif
#ifdef ENABLE_CF
  deallocate_measure_cf,
#endif
};

const reallocate_t reallocate_measure_funcs[ALLOC_NB] = {
  reallocate_measure_mf, reallocate_measure_vmf, reallocate_measure_dl,
#ifdef ENABLE_TLSF
  reallocate_measure_tlsf,
#endif
#ifdef ENABLE_CF
  reallocate_measure_cf,
#endif
};
#endif /* INSTRUCTION_COUNTER_ENABLE */

static inline void* ptr_diff(void* addr, ptrdiff_t diff) {
  return (void*)((uint8_t*)addr + diff);
}

static inline void binfo_init(size_t id_num) {
  g_binfo.addrs  = (void**)calloc(sizeof(void*), id_num);
  g_binfo.id_num = id_num;
  if (g_binfo.addrs == NULL) {
    perror("calloc");
    exit(EXIT_FAILURE);
  }

#if MEMORY_TEST
  g_binfo.addr_min = (void*)(-1);
  g_binfo.addr_max = (void*)(0);
  g_binfo.lens     = calloc(sizeof(size_t), id_num);
  if (g_binfo.lens == NULL) {
    perror("calloc");
    exit(EXIT_FAILURE);
  }
#endif /* MEMORY_TEST */
}

static inline void binfo_malloc(size_t idx, void* addr, size_t len) {
  g_binfo.addrs[idx] = addr;

#if MEMORY_TEST
  g_binfo.addr_min  = ALLOCATOR_MIN(g_binfo.addr_min, addr);
  g_binfo.addr_max  = ALLOCATOR_MAX(g_binfo.addr_max, ptr_diff(addr, len));
  g_binfo.lens[idx] = len;
#endif /* MEMORY_TEST */
}

static inline void binfo_free(size_t idx) {
#if MEMORY_TEST
  size_t i;
  void* new_addr_max;

  if ((uint8_t*)g_binfo.addrs[idx] + g_binfo.lens[idx] == g_binfo.addr_max) {
    new_addr_max = NULL;
    for (i = 0; i < g_binfo.id_num; ++i) {
      if (i == idx) continue;
      if (g_binfo.addrs[i] == NULL) continue;
      new_addr_max = ALLOCATOR_MAX(new_addr_max,
        ptr_diff(g_binfo.addrs[i], g_binfo.lens[i]));
    }
    g_binfo.addr_max = new_addr_max;
  }
  g_binfo.addrs[idx] = NULL;
  g_binfo.lens[idx]  = 0;
#endif /* MEMORY_TEST */
}

static inline void binfo_realloc(size_t idx, void* new_addr, size_t new_len) {
#if MEMORY_TEST
  void* old_addr = g_binfo.addrs[idx];
  void* new_addr_max;
  size_t i;
  if ((uint8_t*)old_addr + g_binfo.lens[idx] == g_binfo.addr_max) {
    if (old_addr == new_addr) {
      g_binfo.addr_max = ptr_diff(new_addr, new_len);
    } else {
      new_addr_max = NULL;
      for (i = 0; i < g_binfo.id_num; ++i) {
        if (i == idx) continue;
        if (g_binfo.addrs[i] == NULL) continue;
        new_addr_max = ALLOCATOR_MAX(new_addr_max,
          ptr_diff(g_binfo.addrs[i], g_binfo.lens[i]));
      }
      g_binfo.addr_max = new_addr_max;
    }
  } else {
    g_binfo.addr_max = ALLOCATOR_MAX(g_binfo.addr_max,
      ptr_diff(new_addr, new_len));
  }
  g_binfo.lens[idx]  = new_len;
#endif /* MEMORY_TEST */
  g_binfo.addrs[idx] = new_addr;
}

static inline void* binfo_dereference(size_t idx) {
  return g_binfo.addrs[idx];
}

#if MEMORY_TEST
static inline size_t binfo_getsize(void) {
  if (g_binfo.addr_max == (void*)(0) ||
      g_binfo.addr_min == (void*)(-1)) {
    return (size_t)0;
  } else {
    return (uint8_t*)g_binfo.addr_max - (uint8_t*)g_binfo.addr_min;
  }
}
#endif /* MEMORY_TEST */
