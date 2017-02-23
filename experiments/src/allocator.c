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
void** dl_addrs;
static void init_dl(size_t mem_min, size_t mem_max,
    size_t id_num, size_t require_size) {
  msp = create_mspace(0, 0);
  dl_addrs = malloc(sizeof(void*) * id_num);
  if (dl_addrs == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
}

static void allocate_dl(size_t idx, size_t size) {
  dl_addrs[idx] = mspace_malloc(msp, size);
}

static void deallocate_dl(size_t idx) {
  mspace_free(msp, dl_addrs[idx]);
}

static void reallocate_dl(size_t idx, size_t size) {
  dl_addrs[idx] = mspace_realloc(msp, dl_addrs[idx], size);
}

static void* dereference_dl(size_t idx) {
  return dl_addrs[idx];
}

static size_t getsize_dl(void) {
  return mspace_footprint(msp);
}

#ifdef INSTRUCTION_COUNTER_ENABLE
static void NO_OPTIMIZE allocate_measure_dl(size_t idx, size_t size) {
  void* addr;
  instruction_count_start();
  addr = mspace_malloc(msp, size);
  instruction_count_end();
  dl_addrs[idx] = addr;
}

static void NO_OPTIMIZE deallocate_measure_dl(size_t idx) {
  void* addr = dl_addrs[idx];
  instruction_count_start();
  mspace_free(msp, addr);
  instruction_count_end();
}

static void NO_OPTIMIZE reallocate_measure_dl(size_t idx, size_t size) {
  void* addr = dl_addrs[idx];
  instruction_count_start();
  addr = mspace_realloc(msp, addr, size);
  instruction_count_end();
  dl_addrs[idx] = addr;
}
#endif /* INSTRUCTION_COUNTER_ENABLE */

#ifdef ENABLE_TLSF
/* TLSF */
void** tlsf_addrs;
#if MEMORY_TEST
size_t* tlsf_sizes;
void*  tlsf_addr_min = (void*)(-1);
void*  tlsf_addr_max = NULL;
size_t tlsf_id_num;
#endif /* MEMORY_TEST */

static void init_tlsf(size_t mem_min, size_t mem_max,
    size_t id_num, size_t require_size) {
  tlsf_addrs = malloc(sizeof(void*) * id_num);
  if (tlsf_addrs == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  memset(tlsf_addrs, 0, sizeof(void*) * id_num);

#if MEMORY_TEST
  tlsf_id_num = id_num;
  tlsf_sizes = malloc(sizeof(size_t) * id_num);
  if (tlsf_sizes == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  memset(tlsf_sizes, 0, sizeof(size_t) * id_num);
#endif /* MEMORY_TEST */
}

static void allocate_tlsf(size_t idx, size_t size) {
  tlsf_addrs[idx] = tlsf_malloc(size);
#if MEMORY_TEST
  tlsf_sizes[idx] = size;
  tlsf_addr_min = ALLOCATOR_MIN(tlsf_addr_min, tlsf_addrs[idx]);
  tlsf_addr_max = ALLOCATOR_MAX(tlsf_addr_max,
      (void*)((uint8_t*)tlsf_addrs[idx] + size));
#endif
}

static void deallocate_tlsf(size_t idx) {
#if MEMORY_TEST
  size_t i;
  size_t size = tlsf_sizes[idx];

  tlsf_sizes[idx] = 0;
  if (tlsf_addr_max == (uint8_t*)tlsf_addrs[idx] + size) {
    for (i = 0; i < tlsf_id_num; ++i) {
      if (tlsf_sizes[i] > 0) {
        tlsf_addr_max = ALLOCATOR_MAX(tlsf_addr_max,
          (void*)((uint8_t*)tlsf_addrs[idx] + tlsf_sizes[i]));
      }
    }
  }
#endif
  tlsf_free(tlsf_addrs[idx]);
}

static void reallocate_tlsf(size_t idx, size_t size) {
#if MEMORY_TEST
  size_t i;

  tlsf_sizes[idx] = 0;
  if (tlsf_addr_max == (uint8_t*)tlsf_addrs[idx] + tlsf_sizes[idx]) {
    for (i = 0; i < tlsf_id_num; ++i) {
      if (tlsf_sizes[i] > 0) {
        tlsf_addr_max = ALLOCATOR_MAX(tlsf_addr_max,
          (void*)((uint8_t*)tlsf_addrs[idx] + tlsf_sizes[i]));
      }
    }
  }
#endif /* MEMORY_TEST */

  tlsf_addrs[idx] = tlsf_realloc(tlsf_addrs[idx], size);
#if MEMORY_TEST
  tlsf_sizes[idx] = size;
  tlsf_addr_min = ALLOCATOR_MIN(tlsf_addr_min, tlsf_addrs[idx]);
  tlsf_addr_max = ALLOCATOR_MAX(tlsf_addr_max,
    (void*)((uint8_t*)tlsf_addrs[idx] + size));
#endif /* MEMORY_TEST */
}

static void* dereference_tlsf(size_t idx) {
  return tlsf_addrs[idx];
}

static size_t getsize_tlsf(void) {
#if MEMORY_TEST
  return (uint8_t*)tlsf_addr_max - (uint8_t*)tlsf_addr_min;
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
  tlsf_addrs[idx] = addr;
}

static void NO_OPTIMIZE deallocate_measure_tlsf(size_t idx) {
  void* addr = tlsf_addrs[idx];
  instruction_count_start();
  tlsf_free(addr);
  instruction_count_end();
}

static void NO_OPTIMIZE reallocate_measure_tlsf(size_t idx, size_t size) {
  void* addr = tlsf_addrs[idx];
  instruction_count_start();
  addr = tlsf_realloc(tlsf_addrs[idx], size);
  instruction_count_end();
  tlsf_addrs[idx] = addr;
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
  size_t pool_size = (8 * require_size + 0x10000) & ~(0x10000 - 1);

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
