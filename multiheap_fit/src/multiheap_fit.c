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
#ifndef __GNUC__
#  warning We have not tested with compilers other than GCC
#endif /* __GNUC__ */

#ifdef __linux__
#  define _GNU_SOURCE
#endif

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <fcntl.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/mman.h>

#include "multiheap_fit.h"

#ifdef _GNU_SOURCE
#  define MMAP_WRAPPER(...) mmap64(__VA_ARGS__)
#else
#  define MMAP_WRAPPER(...) mmap(__VA_ARGS__)
#endif

/* If COPYLESS is set, block copying is omitted in 'mf_dereference' */
#ifndef COPYLESS
#  define COPYLESS 0
#endif

/* If EXACT_SIZE_CLASS is set,
     S_{i} = i byte.
   If EXACT_SIZE_CLASS is not set,
     S_{i} = S_{i-1} * SIZE_CLASS_CONST. */
#ifndef EXACT_SIZE_CLASS
#  define EXACT_SIZE_CLASS 0
#endif
#if !EXACT_SIZE_CLASS
#  ifndef SIZE_CLASS_MAX
#    define SIZE_CLASS_MAX 128
#  endif
#  ifndef BINARY_SEARCH_COUNT
#    define BINARY_SEARCH_COUNT 7
#  endif
#  ifndef SIZE_CLASS_CONST
#    define SIZE_CLASS_CONST 0.1232
#  endif
#endif

/* If this flag is set, fixed length integer(uint32_t) is used
   insteada of variable length integer. */
#ifndef FIXED_LENGTH_INTEGER
#  define FIXED_LENGTH_INTEGER 0
#endif

/* Memory will be allocated at a multiple of MEMORY_ALIGN.
   This value should be a power of 2. */
#ifndef MEMORY_ALIGN
#  if FIXED_LENGTH_INTEGER
#    define MEMORY_ALIGN sizeof(blockid_t)
#  else
#    define MEMORY_ALIGN 1
#  endif
#endif

/* number of bits of one byte */
#define ONE_BYTE 8

/* Huristic parameter */
#ifndef ENABLE_HEURISTIC
#  define ENABLE_HEURISTIC 1
#endif

#if ENABLE_HEURISTIC
#  ifndef POOL_NUM_THRESHOLD
#    define POOL_NUM_THRESHOLD 16
#  endif
#  ifndef GARBAGE_NUM_MAX
#    define GARBAGE_NUM_MAX 6
#  endif
#  ifndef EXTRA_PAGE_RATE
#    define EXTRA_PAGE_RATE 9 / 8
#  endif
#endif

/* Type used for passing positions of a pseudo heap */
typedef uint32_t offset_t;
/* Type used for passing size classes  */
typedef uint32_t size_class_t;
/* Type used for the number of bytes */
typedef uint_fast16_t bytenum_t;

#define MF_MIN(x, y) ((x) < (y) ? (x) : (y))
#define MF_INLINE static inline


/* ========================================================================== */
/* OS memory management wrapper */
/* ========================================================================== */
/** Create anonymous mapping from addr to addr + size */
MF_INLINE void safe_anon_mmap(void* addr, size_t size);
/** Destroy anonymous mapping from addr to addr + size */
MF_INLINE void safe_zero_mmap(void* addr, size_t size);
/** Call 'malloc' and exit if 'malloc' is failed */
MF_INLINE void* safe_malloc(size_t size);


/* ========================================================================== */
/* commonly used functions */
/* ========================================================================== */
/** Calculate the number of bits to represent num */
MF_INLINE bytenum_t required_bit(uint64_t num);
/** Calculate the number of bytes to represent num */
MF_INLINE bytenum_t required_byte(uint64_t num);

/* Page size of the system */
static size_t g_page_size = 0;
/* Used to round up g_page_size */
static size_t g_page_mask = 0;
/* Shift amount corresponding to g_page_size */
static bytenum_t g_page_shift = 0;
/** Convert required heap size to the number of pages */
MF_INLINE size_t length2page_num(size_t length);
/** Align up size to a multiple of MEMORY_ALIGN */
MF_INLINE size_t align_up(size_t size);

#if !FIXED_LENGTH_INTEGER
/** Read 'byte_num' bytes integer */
MF_INLINE uint64_t get_int(const void* input, bytenum_t byte_num);
/** Write 'byte_num' bytes integer */
MF_INLINE void put_int(void* output, bytenum_t byte_num, uint64_t input);
#endif  /* FIXED_LENGTH_INTEGER */
/** Fast memcpy(expect compiler optimization) */
MF_INLINE void my_memcpy(void* __restrict__ dst, const void* __restrict__ src,
    size_t size);
/** (void*)((uint8_t*)addr + diff) */
MF_INLINE void* ptr_offset(void* addr, ptrdiff_t diff);


/* ========================================================================== */
/* size_class */
/* ========================================================================== */
#if !EXACT_SIZE_CLASS
struct size_manager {
  size_t sizeof_class[SIZE_CLASS_MAX];
};

static struct size_manager g_size_manager;
#endif /* EXACT_SIZE_CLASS */

/** Initialize size_manager */
MF_INLINE void size_manager_init(void);
/** Convert size to size_class */
MF_INLINE size_t size2sc(size_t size);
/** Convert size_class to size */
MF_INLINE size_t sc2size(size_t size_class);


/* ========================================================================== */
/* pseudo_heap */
/* ========================================================================== */
/* (virtual) heap */
typedef struct {
  /* mmaped addr */
  void*  addr;
  /* The number of mmaped pages */
  uint32_t page_num;
#if ENABLE_HEURISTIC
  /* The number of not released pages */
  uint32_t extra_num;
#endif
} pseudo_heap_t;

/** Reserve virtual memory space */
MF_INLINE void pheap_first_reserve(size_t max_nr);
/** Constructor */
MF_INLINE void pheap_init(pseudo_heap_t* pheap_ptr);
/** Destructor */
MF_INLINE void pheap_final(pseudo_heap_t* pheap_ptr);
/** Bluge the length of heap to new_sc(faster than 'pheap_resize') */
MF_INLINE void pheap_bulge(pseudo_heap_t* pheap_ptr, size_t new_size);
/** Change the length of heap to new_sc(faster than 'pheap_resize') */
MF_INLINE void pheap_shrink(pseudo_heap_t* pheap_ptr, size_t new_size);
/** Head address of pseudo heap */
MF_INLINE void* pheap_address(pseudo_heap_t* pheap_ptr);
/** Total using memory in pheap_ptr */
MF_INLINE size_t pheap_using_mem(const pseudo_heap_t* pheap_ptr);
#if ENABLE_HEURISTIC
/** Free extra reserved page */
MF_INLINE void pheap_delete_extra(pseudo_heap_t* pheap_ptr);
#endif


/* ========================================================================== */
/* block_manager */
/* ========================================================================== */
/** A structure that performs memory allocation of a single size.
 *  The memory is reserved for the minimum necessary amount per page
 *  (via pseudo_heap). When deleting an element, it corresponds by bringing
 *  the final element to the position of the element to be deleted.
 *  There is a possibility that the address may move with deletion or
 *  element addition.
 */
typedef struct {
  /* Number of memory blocks */
  size_t obj_num;
  /* Size of memory blocks */
  size_t obj_size;
  /* pseudo heap to allocate memory blocks */
  pseudo_heap_t pseudo_heap;
} block_manager_t;

/** Constructor */
MF_INLINE block_manager_t* block_manager_init(size_t obj_size);
/** Destructor */
MF_INLINE void   block_manager_final(block_manager_t* bm_ptr);
/** Address of index-block */
MF_INLINE void*  block_manager_addr(block_manager_t* bm_ptr, size_t index);
/** Block_manager_addr(bm_ptr, bm_ptr->obj_num - 1) */
MF_INLINE void*  block_manager_last_addr(block_manager_t* bm_ptr);
/** Append new memory block to tail */
MF_INLINE size_t block_manager_append(block_manager_t* bm_ptr);
/** Remove tail memory block. */
MF_INLINE void block_manager_remove(block_manager_t* bm_ptr);
/** Get obj_num */
MF_INLINE size_t block_manager_obj_num(block_manager_t* bm_ptr);
/** Total using memory in bm_ptr */
MF_INLINE size_t block_manager_using_mem(const block_manager_t* bm_ptr);


/* ========================================================================== */
/* block_info */
/* ========================================================================== */

/* image
  struct elem_info {
    <l byte> size_class;
    <o byte> offset;
  };
*/
#if FIXED_LENGTH_INTEGER
typedef struct elem_info {
  size_class_t size_class;
  offset_t     offset;
} elem_info_t;
#else  /* FIXED_LENGTH_INTEGER */
#  define ELEM_INFO_BEGIN(ptr) ((void*)ptr)
#  define ELEM_INFO_SC(ptr) ((void*)ELEM_INFO_BEGIN(ptr))
#  define ELEM_INFO_OFFSET(ptr, l)\
    ((void*)((uint8_t*)ELEM_INFO_SC(ptr) + l))
#  define ELEM_INFO_END(ptr, l, o)\
    ((void*)((uint8_t*)ELEM_INFO_OFFSET(ptr, l) + o))
#  define ELEM_INFO_SIZE(l, o)\
    ((size_t)(ELEM_INFO_END(0, l, o) - ELEM_INFO_BEGIN(0)))
#endif /* FIXED_LENGTH_INTEGER */

typedef struct {
  /* Max number of blocks */
  blockid_t  nr_max;

#if FIXED_LENGTH_INTEGER
  /* Data region */
  elem_info_t* data_addr;
#else  /* FIXED_LENGTH_INTEGER */
  /* Data region */
  void* data_addr;
  /* ELEM_INFO_SIZE(l, o) */
  size_t block_size;
  /* Byte num to represent 'length' */
  bytenum_t  sc_byte;  /* l */
  /* Byte num to represent 'offset' */
  bytenum_t  ofs_byte;  /* o */
#endif /* FIXED_LENGTH_INTEGER */
} block_info_t;

/** Constructor */
#if FIXED_LENGTH_INTEGER
MF_INLINE block_info_t* block_info_init(blockid_t element_nr_max);
#else /* FIXED_LENGTH_INTEGER */
MF_INLINE block_info_t* block_info_init(bytenum_t offset_byte,
    bytenum_t length_byte, blockid_t element_nr_max);
#endif /* FIXED_LENGTH_INTEGER */
/** Destructor */
MF_INLINE void block_info_final(block_info_t* block_info_ptr);
/** Read offset of the id-block */
MF_INLINE offset_t block_info_get_offset(
    const block_info_t* block_info_ptr, blockid_t id);
/** Write offset of the id-block */
MF_INLINE void block_info_put_offset(block_info_t* block_info_ptr,
    blockid_t id, offset_t ofs);
/** Read length of the id-block */
MF_INLINE size_class_t block_info_get_sc(
    const block_info_t* block_info_ptr, blockid_t id);
/** Write length of the id-block */
MF_INLINE void block_info_put_sc(block_info_t* block_info_ptr,
    blockid_t id, size_class_t sc);
/** Write length and offset of the id-block */
MF_INLINE void block_info_put_sc_and_ofs(block_info_t* block_info_ptr,
    blockid_t id, size_class_t sc, offset_t ofs);
/** Total using memory in block_info_ptr */
MF_INLINE size_t block_info_using_mem(const block_info_t* block_info_ptr);


/* ========================================================================== */
/* main structure */
/* ========================================================================== */
typedef struct {
  /* Min size of allocated memory */
  size_class_t  sc_min;
  /* Max size of allocated memory */
  size_class_t  sc_max;
  /* Max number of memory blocks */
  blockid_t elem_nr_max;
  /* Max total allocated size */
  offset_t  max_byte;
  /* Structure containing information necessary for each block */
  block_info_t* block_info_ptr;
  /* Structures that store blocks corresponding to each size class */
  block_manager_t** block_managers;

#if !FIXED_LENGTH_INTEGER
  /* ID byte to represent 'bid' */
  bytenum_t  id_byte;
  /* Byte num to represent 'offset' */
  bytenum_t ofs_byte;
  /* Byte num to represent 'length' */
  bytenum_t sc_byte;
#endif /* FIXED_LENGTH_INTEGER */
} mf_main_t;


/* ========================================================================== */
/* OS memory management wrapper */
/* ========================================================================== */

MF_INLINE void safe_anon_mmap(void* addr, size_t size) {
  void* ret_addr = MMAP_WRAPPER(addr, size, PROT_READ | PROT_WRITE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1, 0);
  if (ret_addr == MAP_FAILED) {
    perror("MMAP_WRAPPER(anon)");
    exit(EXIT_FAILURE);
  }
}

MF_INLINE void safe_zero_mmap(void* addr, size_t size) {
  void* ret_addr = MMAP_WRAPPER(addr, size, PROT_NONE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED | MAP_NORESERVE, -1, 0);
  if (ret_addr == MAP_FAILED) {
    perror("MMAP_WRAPPER(zero)");
    exit(EXIT_FAILURE);
  }
}

MF_INLINE void* safe_malloc(size_t size) {
  void* addr = malloc(size);
  if (addr == NULL) {
    perror("malloc");
    exit(EXIT_FAILURE);
  }
  return addr;
}


/* ========================================================================== */
/* commonly used functions */
/* ========================================================================== */

MF_INLINE bytenum_t required_bit(uint64_t num) {
  bytenum_t bit_size;

  if (num > 1) {
#ifdef __GNUC__
    bit_size  = sizeof(uint64_t) * ONE_BYTE - __builtin_clzll(num - 1);
#else  /* __GNU_C__ */
    bit_size = 1;
    --num;
    while (num > 0) {
      ++bit_size;
      num >>= 1;
    }
#endif /* __GNU_C__ */
  } else {
    bit_size = 0;
  }
  return bit_size;
}

MF_INLINE bytenum_t required_byte(uint64_t num) {
  return (required_bit(num) + (ONE_BYTE - 1)) / ONE_BYTE;
}

MF_INLINE size_t length2page_num(size_t length) {
  return (length + g_page_mask) >> g_page_shift;
}

MF_INLINE size_t align_up(size_t size) {
  return (size + MEMORY_ALIGN - 1) & ~(MEMORY_ALIGN - 1);
}

#if !FIXED_LENGTH_INTEGER
MF_INLINE uint64_t get_int(const void* input, bytenum_t byte_num) {
  uint64_t ret = 0;
  bytenum_t i;
  const uint8_t* in_u8 = (const uint8_t*) input;

#if ENABLE_HEURISTIC
  if (byte_num == 1) {
    return in_u8[0];
  } else if (byte_num == 2) {
    return (in_u8[0] << 8) | in_u8[1];
  } else if (byte_num == 3) {
    return (in_u8[0] << 16) | (in_u8[1] << 8) | in_u8[2];
  } else if (byte_num == 4) {
    return (in_u8[0] << 24) | (in_u8[1] << 16) | (in_u8[2] << 8) | in_u8[3];
  } else
#endif /* ENABLE_HEURISTIC */
  {
    for (i = 0; i < byte_num; ++i) {
      ret = (ret << 8) | *in_u8++;
    }
    return ret;
  }
}

MF_INLINE void put_int(void* output, bytenum_t byte_num, uint64_t input) {
  bytenum_t i;
  uint8_t* out_u8 = (uint8_t*) output + (bytenum_t)(byte_num - 1);

#if ENABLE_HEURISTIC
  if (byte_num == 1) {
    *out_u8 = input & 0xff;
  } else if (byte_num == 2) {
    *out_u8-- = input & 0xff;  input >>= 8;
    *out_u8   = input & 0xff;
  } else if (byte_num == 3) {
    *out_u8-- = input & 0xff;  input >>= 8;
    *out_u8-- = input & 0xff;  input >>= 8;
    *out_u8   = input & 0xff;
  } else if (byte_num == 4) {
    *out_u8-- = input & 0xff;  input >>= 8;
    *out_u8-- = input & 0xff;  input >>= 8;
    *out_u8-- = input & 0xff;  input >>= 8;
    *out_u8   = input & 0xff;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    for (i = 0; i < byte_num; ++i) {
      *out_u8-- = input & 0xff;
      input >>= 8;
    }
  }
}
#endif /* FIXED_LENGTH_INTEGER */

MF_INLINE void my_memcpy(void* __restrict__ dst, const void *__restrict__ src,
    size_t size) {
  uint8_t* __restrict__ dst_u8 = (uint8_t* __restrict__)dst;
  const uint8_t* __restrict__ src_u8 = (const uint8_t* __restrict__)src;

  do {
    *dst_u8++ = *src_u8++;
  } while (--size);
}

MF_INLINE void* ptr_offset(void* addr, ptrdiff_t diff) {
  return (void*)((uint8_t*)addr + diff);
}


/* ========================================================================== */
/* size_class */
/* ========================================================================== */

MF_INLINE void size_manager_init(void) {
#if !EXACT_SIZE_CLASS
  size_t i;
  double curr_size = 8.0;
  for (i = 0; i < SIZE_CLASS_MAX; ++i) {
    g_size_manager.sizeof_class[i] = (size_t)curr_size;
    curr_size *= 1.0 + SIZE_CLASS_CONST;
    curr_size = align_up(ceil(curr_size));
  }
#endif /* EXACT_SIZE_CLASS */
}

MF_INLINE size_t size2sc(size_t size) {
#if EXACT_SIZE_CLASS
  return (size + MEMORY_ALIGN - 1) / MEMORY_ALIGN;
#else  /* EXACT_SIZE_CLASS */
  /* binary search in (left, right] */
  int left =  - 1;
  int right = SIZE_CLASS_MAX - 1;
  int middle;
  unsigned i;

  /* Because SIZE_CLASS_MAX is constant value,
    binary search's iteration is finit. */
  /* while (right - left > 1) { */
  for (i = 0; i < BINARY_SEARCH_COUNT; ++i) {
    middle = (left + right) / 2;
    if (size <= g_size_manager.sizeof_class[middle]) {
      right = middle;
    } else {
      left = middle;
    }
  }
  return right;
#endif /* EXACT_SIZE_CLASS */
}

MF_INLINE size_t sc2size(size_t size_class) {
#if EXACT_SIZE_CLASS
  return size_class * MEMORY_ALIGN;
#else  /* EXACT_SIZE_CLASS */
  return g_size_manager.sizeof_class[size_class];
#endif /* EXACT_SIZE_CLASS */
}


/* ========================================================================== */
/* pseudo_heap */
/* ========================================================================== */

#if ENABLE_HEURISTIC
/* Heaper to manipulate unused pseudo_heap */
struct pool_header {
  /* Previous pool header */
  struct pool_header* prev;
  /* Next pool header */
  struct pool_header* next;
  /* The number of allocated pages. */
  size_t page_num;
};

/* Header to manipulate unused page */
struct garbage_header {
  /* Previous garbage header */
  struct garbage_header* prev;
  /* Next garbage header */
  struct garbage_header* next;
  /* The pseudo_heap which administrates this garbage */
  pseudo_heap_t* pseudo_heap;
  /* The number of garbage pages */
  size_t page_num;
};
#endif /* ENABLE_HEURISTIC */

/* This structure is used to reserve and manage virtual memory spaces. */
struct virt_space {
  /* Head addreses of reserved virturl memory space for pseudo_heap */
  void** addrs;
  /* The size reserved first */
  size_t reserved_size;
  /* The number of addresses in addrs */
  size_t addr_nr;
  /* Max size of pseudo_heap */
  size_t size_per_space;

#if ENABLE_HEURISTIC
  /* pool_header list(sentinel) */
  struct pool_header pool_head_buf[2];
  /* Pool header list */
  struct pool_header* pool_sentinel;
  /* Total pool page num */
  size_t pool_num;

  /* Garbage_header list(sentinel) */
  struct garbage_header garbage_head_buf[2];
  /* Garbage header list */
  struct garbage_header* garbage_sentinel;
  /* Total garbage page num */
  size_t garbage_num;
#endif /* ENABLE_HEURISTIC */

  /* Head addr reserved for the first time */
  void* addr_start;
  /* Max number of pseudo_heap structures */
  size_t max_nr;

  /* The flag which represents initialized or not */
  bool initialized;
};

static struct virt_space g_virt_space = {.initialized = false};
/** Finalize g_virt_space */
MF_INLINE void virt_space_final(void);

#if ENABLE_HEURISTIC
#define IS_POOL_EMPTY() \
  (g_virt_space.pool_sentinel->next == g_virt_space.pool_sentinel->prev)

/** Insert pool */
MF_INLINE void pool_push(struct pool_header* inserted);
/** Pop pool */
MF_INLINE struct pool_header* pool_top(void);
/** Get total size of pool */
MF_INLINE size_t pool_get_size(void);
/** Insert garbage */
MF_INLINE void garbage_push(struct garbage_header* inserted);
/** Remove garbage */
MF_INLINE void garbage_remove(struct garbage_header* removed);
/** Remove and deallocate garbage */
MF_INLINE void garbage_delete(struct garbage_header* deleted);
/** Get total size of pool */
MF_INLINE size_t garbage_get_size(void);
#endif /* ENABLE_HEURISTIC */

MF_INLINE void pheap_first_reserve(size_t max_nr) {
  size_t i;
  size_t size_per_space;
  size_t mmap_size;
  void*  addr;

  if (g_page_size == 0) {
    g_page_size  = getpagesize();
    g_page_mask  = g_page_size - 1;
    g_page_shift = 8 * sizeof(unsigned long long) - __builtin_clzll(g_page_size) - 1;
  }

  if (g_virt_space.initialized) {
    virt_space_final();
  }
  g_virt_space.initialized = true;

  /* align up max_nr to a power of 2 */
  max_nr = (1ULL << required_bit(max_nr));

  g_virt_space.addrs = (void**) safe_malloc(sizeof(void*) * max_nr);

  mmap_size = g_page_size << 1;
  /* reserve virtual memory as much as possible */
  addr = MMAP_WRAPPER(0, mmap_size, PROT_NONE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  while (addr != MAP_FAILED) {
    munmap(addr, mmap_size);
    mmap_size <<= 1;
    addr = MMAP_WRAPPER(0, mmap_size, PROT_NONE,
      MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  }
  mmap_size >>= 1;
  size_per_space = mmap_size / max_nr;
  g_virt_space.addr_start = MMAP_WRAPPER(0, mmap_size, PROT_NONE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (g_virt_space.addr_start == MAP_FAILED) {
    perror("LIMIT MMAP failed");
    exit(EXIT_FAILURE);
  }

  /* initialize sentinel */
#if ENABLE_HEURISTIC
  g_virt_space.pool_sentinel = &g_virt_space.pool_head_buf[1];
  g_virt_space.pool_head_buf[0].prev = &g_virt_space.pool_head_buf[1];
  g_virt_space.pool_head_buf[0].next = &g_virt_space.pool_head_buf[1];
  g_virt_space.pool_head_buf[1].prev = &g_virt_space.pool_head_buf[0];
  g_virt_space.pool_head_buf[1].next = &g_virt_space.pool_head_buf[0];
  g_virt_space.pool_num = 0;
  g_virt_space.garbage_sentinel = &g_virt_space.garbage_head_buf[1];
  g_virt_space.garbage_head_buf[0].prev = &g_virt_space.garbage_head_buf[1];
  g_virt_space.garbage_head_buf[0].next = &g_virt_space.garbage_head_buf[1];
  g_virt_space.garbage_head_buf[1].prev = &g_virt_space.garbage_head_buf[0];
  g_virt_space.garbage_head_buf[1].next = &g_virt_space.garbage_head_buf[0];
  g_virt_space.garbage_num = 0;
#endif /* ENABLE_HEURISTIC */
  g_virt_space.size_per_space = size_per_space;
  g_virt_space.addr_nr = max_nr;
  g_virt_space.max_nr  = max_nr;
  g_virt_space.reserved_size = mmap_size;
  for (i = 0; i < max_nr; ++i) {
    g_virt_space.addrs[i] =
      ptr_offset(g_virt_space.addr_start, i * size_per_space);
  }
}

MF_INLINE void virt_space_final(void) {
  free(g_virt_space.addrs);
  g_virt_space.addr_nr = 0;
  /* munmap virtual space involve pools */
  munmap(g_virt_space.addr_start, g_virt_space.reserved_size);
  g_virt_space.max_nr = 0;
  g_virt_space.initialized = false;
}

#if ENABLE_HEURISTIC
MF_INLINE void pool_push(struct pool_header* inserted) {
  struct pool_header* last_pool_sentinel = g_virt_space.pool_sentinel->prev;

  if (g_virt_space.pool_num > POOL_NUM_THRESHOLD) {
    safe_zero_mmap(inserted, inserted->page_num << g_page_shift);
    g_virt_space.addrs[g_virt_space.addr_nr++] = (void*)inserted;
  } else {
    inserted->prev = last_pool_sentinel->prev;
    last_pool_sentinel->prev->next = inserted;
    last_pool_sentinel->prev = inserted;
    inserted->next = last_pool_sentinel;
    g_virt_space.pool_num += inserted->page_num;
  }
}

MF_INLINE struct pool_header* pool_top(void) {
  struct pool_header* first_pool_sentinel = g_virt_space.pool_sentinel;
  struct pool_header* ret_pool = first_pool_sentinel->next;

  assert(!IS_POOL_EMPTY());
  first_pool_sentinel->next = ret_pool->next;
  ret_pool->next->prev = first_pool_sentinel;
  g_virt_space.pool_num -= ret_pool->page_num;
  return ret_pool;
}

MF_INLINE size_t pool_get_size(void) {
  return g_virt_space.pool_num << g_page_shift;
}

MF_INLINE void garbage_push(struct garbage_header* inserted) {
  struct garbage_header* sentinel = g_virt_space.garbage_sentinel;
  struct garbage_header* removed;

  /* If there are too many garbage pages, remove the head garbage page */
  if (g_virt_space.garbage_num + inserted->page_num > GARBAGE_NUM_MAX) {
    removed = sentinel->prev->prev;
    if (removed != sentinel) {
      garbage_delete(removed);
    }
  }

  /* insert to the list */
  sentinel->next->prev = inserted;
  inserted->next = sentinel->next;
  inserted->prev = sentinel;
  sentinel->next = inserted;
  g_virt_space.garbage_num += inserted->page_num;
}

MF_INLINE void garbage_remove(struct garbage_header* removed) {
  struct garbage_header* prev = removed->prev;
  struct garbage_header* next = removed->next;

  /* short circuit */
  prev->next = next;
  next->prev = prev;

  g_virt_space.garbage_num -= removed->page_num;
}

MF_INLINE void garbage_delete(struct garbage_header* deleted) {
  garbage_remove(deleted);
  pheap_delete_extra(deleted->pseudo_heap);
}

MF_INLINE size_t garbage_get_size(void) {
  return g_virt_space.garbage_num << g_page_shift;
}
#endif /* ENABLE_HEURISTIC */

MF_INLINE void pheap_init(pseudo_heap_t* pheap_ptr) {
  assert(g_virt_space.initialized);

  pheap_ptr->addr = NULL;
  pheap_ptr->page_num  = 0;

#if ENABLE_HEURISTIC
  pheap_ptr->extra_num = 0;
#endif
}

MF_INLINE void pheap_final(pseudo_heap_t* pheap_ptr) {
  if (pheap_ptr->page_num > 0) {
    pheap_shrink(pheap_ptr, 0);
  }
}

MF_INLINE void pheap_bulge(pseudo_heap_t* pheap_ptr,
    size_t new_size) {
  size_t old_page_num = pheap_ptr->page_num;
  size_t new_page_num = length2page_num(new_size);
  void* addr = pheap_ptr->addr;

  if (old_page_num >= new_page_num) return;
  if (addr == NULL) {
#if ENABLE_HEURISTIC
    if (!IS_POOL_EMPTY()) {
      {
        struct pool_header* assigned = pool_top();
        addr = (void*) assigned;
        pheap_ptr->addr = addr;
        old_page_num = assigned->page_num;
      }
      if (old_page_num >= new_page_num) {
        pheap_ptr->page_num = old_page_num;
        return;
      }
    } else
#endif /* ENABLE_HEURISTIC */
    {
      addr = g_virt_space.addrs[--g_virt_space.addr_nr];
      pheap_ptr->addr = addr;
    }
  }
#if ENABLE_HEURISTIC
  else if (pheap_ptr->extra_num > 0) {
    struct garbage_header* extra_head = (struct garbage_header*)
      ptr_offset(addr, old_page_num << g_page_shift);
    garbage_remove(extra_head);
    old_page_num += pheap_ptr->extra_num;
    pheap_ptr->extra_num = 0;
    if (old_page_num >= new_page_num) {
      pheap_ptr->page_num = old_page_num;
      return;
    }
  }
#endif /* ENABLE_HEURISTIC */
  safe_anon_mmap(ptr_offset(addr, old_page_num << g_page_shift),
    (new_page_num - old_page_num) << g_page_shift);
  pheap_ptr->page_num = new_page_num;
}

MF_INLINE void pheap_shrink(pseudo_heap_t* pheap_ptr,
    size_t new_size) {
  size_t old_page_num = pheap_ptr->page_num;
  size_t new_page_num = length2page_num(new_size);
  void* addr = pheap_ptr->addr;

#if ENABLE_HEURISTIC
  new_page_num = new_page_num * EXTRA_PAGE_RATE;
#endif
  if (old_page_num <= new_page_num) return;

#if ENABLE_HEURISTIC
  if (new_page_num == 0) {
    {
      if (pheap_ptr->extra_num > 0) {
        struct garbage_header* deleted = (struct garbage_header*)
          ptr_offset(addr, old_page_num << g_page_shift);
        garbage_delete(deleted);
      }
      struct pool_header* push = (struct pool_header*) addr;
      push->page_num = old_page_num;
      pool_push(push);
    }
    pheap_ptr->addr = NULL;
    pheap_ptr->page_num = 0;
  } else {
    if (pheap_ptr->extra_num > 0) {
      struct garbage_header* deleted = (struct garbage_header*)
        (ptr_offset(addr, old_page_num << g_page_shift));
      garbage_delete(deleted);
    }

    struct garbage_header* push = (struct garbage_header*)
        (ptr_offset(addr, new_page_num << g_page_shift));
    push->pseudo_heap = pheap_ptr;
    push->page_num  = old_page_num - new_page_num;
    garbage_push(push);
    pheap_ptr->extra_num = old_page_num - new_page_num;
    pheap_ptr->page_num  = new_page_num;
  }
#else  /* ENABLE_HEURISTIC */
  safe_zero_mmap(ptr_offset(addr, new_page_num << g_page_shift),
    (old_page_num - new_page_num) << g_page_shift);
  pheap_ptr->page_num = new_page_num;
  if (new_page_num == 0) {
    g_virt_space.addrs[g_virt_space.addr_nr++] = addr;
    pheap_ptr->addr = NULL;
  }
#endif /* ENABLE_HEURISTIC */
}

MF_INLINE void* pheap_address(pseudo_heap_t* pheap_ptr) {
  return pheap_ptr->addr;
}

MF_INLINE size_t pheap_using_mem(const pseudo_heap_t* pheap_ptr) {
  size_t ret_size = 0;

  /* In the current implementation, pseudo_heap is not malloced. */
  /* ret_size += sizeof(pseudo_heap_t); */
  ret_size += pheap_ptr->page_num << g_page_shift;
  return ret_size;
}

#if ENABLE_HEURISTIC
MF_INLINE void pheap_delete_extra(pseudo_heap_t* pheap_ptr) {
  void* addr = pheap_ptr->addr;
  size_t page_num  = pheap_ptr->page_num;
  size_t extra_num = pheap_ptr->extra_num;

  safe_zero_mmap(ptr_offset(addr, page_num << g_page_shift),
      extra_num << g_page_shift);
  pheap_ptr->extra_num = 0;
}
#endif /* ENABLE_HEURISTIC */


/* ========================================================================== */
/* block manager */
/* ========================================================================== */

MF_INLINE block_manager_t* block_manager_init(size_t obj_size) {
  block_manager_t* bm_ptr;

  bm_ptr = safe_malloc(sizeof(block_manager_t));
  pheap_init(&bm_ptr->pseudo_heap);
  bm_ptr->obj_size  = obj_size;
  bm_ptr->obj_num   = 0;
  return bm_ptr;
}

MF_INLINE void block_manager_final(block_manager_t* bm_ptr) {
  pheap_final(&bm_ptr->pseudo_heap);
  free(bm_ptr);
}

MF_INLINE void* block_manager_addr(block_manager_t* bm_ptr,
    size_t index) {
  pseudo_heap_t* pseudo_heap = &bm_ptr->pseudo_heap;
  ptrdiff_t offset = index * bm_ptr->obj_size;

  assert(index < bm_ptr->obj_num);
  return ptr_offset(pheap_address(pseudo_heap), offset);
}

MF_INLINE void* block_manager_last_addr(block_manager_t* bm_ptr) {
  assert(bm_ptr->obj_num > 0);
  return block_manager_addr(bm_ptr, bm_ptr->obj_num - 1);
}

MF_INLINE size_t block_manager_append(block_manager_t* bm_ptr) {
  size_t appended_index = bm_ptr->obj_num++;
  size_t new_heap_size;
  pseudo_heap_t* pseudo_heap = &bm_ptr->pseudo_heap;

  new_heap_size = bm_ptr->obj_num * bm_ptr->obj_size;
  pheap_bulge(pseudo_heap, new_heap_size);
  return appended_index;
}

MF_INLINE void block_manager_remove(block_manager_t* bm_ptr) {
  size_t new_heap_size;
  pseudo_heap_t* pseudo_heap  = &bm_ptr->pseudo_heap;

  bm_ptr->obj_num--;
  new_heap_size = bm_ptr->obj_num * bm_ptr->obj_size;
  pheap_shrink(pseudo_heap, new_heap_size);
}

MF_INLINE size_t block_manager_obj_num(block_manager_t* bm_ptr) {
  return bm_ptr->obj_num;
}

MF_INLINE size_t block_manager_using_mem(const block_manager_t* bm_ptr) {
  size_t ret_size = 0;
  ret_size += sizeof(block_manager_t);
  ret_size += pheap_using_mem(&bm_ptr->pseudo_heap);
  return ret_size;
}


/* ========================================================================== */
/* block_info */
/* ========================================================================== */

#if !FIXED_LENGTH_INTEGER
/* addr of id-block information */
MF_INLINE void* elem_block_addr(block_info_t* elem_info, blockid_t id);
/* constant version of 'elem_block_addr' */
MF_INLINE const void* elem_block_addr_c(const block_info_t* elem_info,
  blockid_t id);
#endif /* FIXED_LENGTH_INTEGER */

#if FIXED_LENGTH_INTEGER
MF_INLINE block_info_t* block_info_init(blockid_t element_nr_max)
#else  /* FIXED_LENGTH_INTEGER */
MF_INLINE block_info_t* block_info_init(bytenum_t offset_byte,
    bytenum_t length_byte, blockid_t element_nr_max)
#endif /* FIXED_LENGTH_INTEGER */
{
  block_info_t* block_info_ptr;
  size_t block_size;

  block_info_ptr = safe_malloc(sizeof(block_info_t));
#if FIXED_LENGTH_INTEGER
  block_size = sizeof(elem_info_t);
#else
  block_size = ELEM_INFO_SIZE(length_byte, offset_byte);
  block_info_ptr->block_size = block_size;
  block_info_ptr->ofs_byte   = offset_byte;
  block_info_ptr->sc_byte    = length_byte;
#endif /* FIXED_LENGTH_INTEGER */
  block_info_ptr->data_addr  = safe_malloc(element_nr_max * block_size);
  /* in order to detect unused block, initialize len to 0 */
  memset(block_info_ptr->data_addr, 0, element_nr_max * block_size);
  block_info_ptr->nr_max = element_nr_max;

  return block_info_ptr;
}

MF_INLINE void block_info_final(block_info_t* block_info_ptr) {
  free(block_info_ptr->data_addr);
  /* overwrite NULL to avoid accidents */
  block_info_ptr->data_addr = NULL;
  free(block_info_ptr);
}

#if FIXED_LENGTH_INTEGER
MF_INLINE offset_t block_info_get_offset(const block_info_t* block_info_ptr,
    blockid_t id) {
  return block_info_ptr->data_addr[id].offset;
}

MF_INLINE void block_info_put_offset(block_info_t* block_info_ptr,
    blockid_t id, offset_t ofs) {
  block_info_ptr->data_addr[id].offset = ofs;
}

MF_INLINE size_class_t block_info_get_sc(const block_info_t* block_info_ptr,
    blockid_t id) {
  return block_info_ptr->data_addr[id].size_class;
}

MF_INLINE void block_info_put_sc(block_info_t* block_info_ptr,
    blockid_t id, size_class_t sc) {
  block_info_ptr->data_addr[id].size_class = sc;
}

MF_INLINE void block_info_put_sc_and_ofs(block_info_t* block_info_ptr,
    blockid_t id, size_class_t sc, offset_t ofs) {
  block_info_ptr->data_addr[id].offset = ofs;
  block_info_ptr->data_addr[id].size_class = sc;
}
#else  /* FIXED_LENGTH_INTEGER */
MF_INLINE offset_t block_info_get_offset(const block_info_t* block_info_ptr,
    blockid_t id) {
  const void* block_addr = elem_block_addr_c(block_info_ptr, id);
  return get_int(ELEM_INFO_OFFSET(block_addr, block_info_ptr->sc_byte),
    block_info_ptr->ofs_byte);
}

MF_INLINE void block_info_put_offset(block_info_t* block_info_ptr,
    blockid_t id, offset_t ofs) {
  void* block_addr = elem_block_addr(block_info_ptr, id);
  put_int(ELEM_INFO_OFFSET(block_addr, block_info_ptr->sc_byte),
    block_info_ptr->ofs_byte, ofs);
}

MF_INLINE size_class_t block_info_get_sc(const block_info_t* block_info_ptr,
    blockid_t id) {
  const void* block_addr = elem_block_addr_c(block_info_ptr, id);
  return get_int(ELEM_INFO_SC(block_addr), block_info_ptr->sc_byte);
}

MF_INLINE void block_info_put_sc(block_info_t* block_info_ptr,
    blockid_t id, size_class_t sc) {
  void* block_addr = elem_block_addr(block_info_ptr, id);
  put_int(ELEM_INFO_SC(block_addr), block_info_ptr->sc_byte, sc);
}

MF_INLINE void block_info_put_sc_and_ofs(block_info_t* block_info_ptr,
    blockid_t id, size_class_t sc, offset_t ofs) {
  void* block_addr = elem_block_addr(block_info_ptr, id);

  put_int(ELEM_INFO_SC(block_addr), block_info_ptr->sc_byte, sc);
  put_int(ELEM_INFO_OFFSET(block_addr, block_info_ptr->sc_byte),
    block_info_ptr->ofs_byte, ofs);
}
#endif /* FIXED_LENGTH_INTEGER */

MF_INLINE size_t block_info_using_mem(const block_info_t* block_info_ptr) {
  size_t ret_size = 0;

  ret_size += sizeof(block_info_t);
#if FIXED_LENGTH_INTEGER
  ret_size += sizeof(block_info_t) * block_info_ptr->nr_max;
#else
  ret_size += block_info_ptr->block_size * block_info_ptr->nr_max;
#endif
  return ret_size;
}

#if !FIXED_LENGTH_INTEGER
MF_INLINE void* elem_block_addr(block_info_t* elem_info, blockid_t id) {
  assert(id < elem_info->nr_max);
  return ptr_offset(elem_info->data_addr, elem_info->block_size * id);
}

MF_INLINE const void* elem_block_addr_c(const block_info_t* elem_info,
    blockid_t id) {
  assert(id < elem_info->nr_max);
  return (const uint8_t*)elem_info->data_addr + elem_info->block_size * id;
}
#endif /* FIXED_LENGTH_INTEGER */


/* ========================================================================== */
/* main structure */
/* ========================================================================== */

mf_t mf_init(size_t mem_min, size_t mem_max, size_t elem_nr_max,
    size_t max_byte) {
  mf_main_t* mf_main;
  size_t block_manager_nr;
  size_class_t sc;
  size_t spell_size;
  size_t sc_min, sc_max;
  bytenum_t id_byte;
#if !FIXED_LENGTH_INTEGER
  bytenum_t ofs_byte, sc_byte;
#endif /* FIXED_LENGTH_INTEGER */

  assert(mem_min > 0);
  assert(mem_min <= mem_max);

  size_manager_init();
  sc_max = size2sc(mem_max);
  sc_min = size2sc(mem_min);
  block_manager_nr = sc_max - sc_min + 1;
  pheap_first_reserve(block_manager_nr);
  mf_main = (mf_main_t*) safe_malloc(sizeof(mf_main_t));
#if FIXED_LENGTH_INTEGER
  mf_main->block_info_ptr = block_info_init(elem_nr_max);
  id_byte = sizeof(blockid_t);
#else /* FIXED_LENGTH_INTEGER */
  /* The number of bytes to represent a block ID */
  id_byte = align_up(required_byte(elem_nr_max));
  /* The number of bytes to represent position of a page */
  ofs_byte = align_up(required_byte(max_byte + id_byte * elem_nr_max));
  /* The number of byte to represent a size class or zero length */
  sc_byte = align_up(required_byte(block_manager_nr + 1));
  mf_main->ofs_byte       = ofs_byte;
  mf_main->id_byte        = id_byte;
  mf_main->sc_byte        = sc_byte;
  mf_main->block_info_ptr = block_info_init(ofs_byte, sc_byte, elem_nr_max);
#endif /* FIXED_LENGTH_INTEGER */
  mf_main->sc_min         = sc_min;
  mf_main->sc_max         = sc_max;
  mf_main->elem_nr_max    = elem_nr_max;
  mf_main->max_byte       = max_byte;
  mf_main->block_managers =
    (block_manager_t**) safe_malloc(sizeof(block_manager_t*) * block_manager_nr);
  for (sc = sc_min; sc <= sc_max; ++sc) {
    mf_main->block_managers[sc - sc_min] = block_manager_init(sc2size(sc) + id_byte);
  }

#if ENABLE_HEURISTIC
  if (mf_main->elem_nr_max > 1) {
    /* The first memcpy is very time consuming, so input
       is given such that memcpy occurs. */
    spell_size = sc2size(mf_main->sc_max);
    mf_allocate(mf_main, 0, spell_size);
    mf_allocate(mf_main, 1, spell_size);
    mf_deallocate(mf_main, 0);
    mf_deallocate(mf_main, 1);
  }
#endif /* ENABLE_HEURISTIC */

  return (mf_t) mf_main;
}

void mf_final(mf_t mf) {
  mf_main_t* mf_main = (mf_main_t*)mf;
  size_class_t i;
  size_t block_manager_nr = mf_main->sc_max - mf_main->sc_min + 1;

  for (i = 0; i < block_manager_nr; ++i) {
    if (mf_main->block_managers[i] != NULL) {
      block_manager_final(mf_main->block_managers[i]);
    }
  }
  block_info_final(mf_main->block_info_ptr);
  free(mf_main->block_managers);
  free(mf_main);
}

void mf_allocate(mf_t mf, blockid_t bid, size_t length) {
  mf_main_t* mf_main = (mf_main_t*)mf;
  offset_t ofs;
  size_class_t size_class = size2sc(length);
  size_class_t bmanager_idx = size_class - mf_main->sc_min;
  block_manager_t* block_manager = mf_main->block_managers[bmanager_idx];

  ofs = block_manager_append(block_manager);
#if FIXED_LENGTH_INTEGER
  *(blockid_t*)block_manager_last_addr(block_manager) = bid;
#else /* FIXED_LENGTH_INTEGER */
  put_int(block_manager_last_addr(block_manager), mf_main->id_byte, bid);
#endif /* FIXED_LENGTH_INTEGER */
  block_info_put_sc_and_ofs(mf_main->block_info_ptr, bid,
    bmanager_idx + 1, ofs);
}

void mf_deallocate(mf_t mf, blockid_t bid) {
  mf_main_t* mf_main = (mf_main_t*)mf;
  uint8_t *src_addr, *dst_addr;
  block_info_t* block_info_ptr = mf_main->block_info_ptr;
  offset_t ofs;
  blockid_t moved_id;
  block_manager_t* block_manager;
  size_class_t size_class = block_info_get_sc(mf_main->block_info_ptr, bid);
  size_t obj_num;
#if !FIXED_LENGTH_INTEGER
  bytenum_t id_byte = mf_main->id_byte;
#endif /* FIXED_LENGTH_INTEGER */

  block_manager = mf_main->block_managers[size_class - 1];
  ofs = block_info_get_offset(block_info_ptr, bid);
  /* This assert is heavy processing */
#if FIXED_LENGTH_INTEGER
  assert(*(blockid_t*)block_manager_addr(block_manager, ofs) == bid);
#else /* FIXED_LENGTH_INTEGER */
  assert(get_int(block_manager_addr(block_manager, ofs), id_byte) == bid);
#endif /* FIXED_LENGTH_INTEGER */

  /* To indicate that it is not in use, set the size class to 0. */
  block_info_put_sc(block_info_ptr, bid, 0);

  src_addr = block_manager_last_addr(block_manager);
  dst_addr = block_manager_addr(block_manager, ofs);
  obj_num = block_manager_obj_num(block_manager);
  if (ofs != obj_num - 1) {
#if FIXED_LENGTH_INTEGER
    moved_id = *(blockid_t*)src_addr;
#else  /* FIXED_LENGTH_INTEGER */
    moved_id = get_int(src_addr, id_byte);
#endif /* FIXED_LENGTH_INTEGER */
    block_info_put_offset(block_info_ptr, moved_id, ofs);

#if COPYLESS
#if FIXED_LENGTH_INTEGER
    *(blockid_t*)dst_addr = *(blockid_t*)src_addr;
#else  /* FIXED_LENGTH_INTEGER */
    my_memcpy(dst_addr, src_addr, id_byte);
#endif /* FIXED_LENGTH_INTEGER */
#else  /* COPYLESS */
    my_memcpy(dst_addr, src_addr, block_manager->obj_size);
#endif /* COPYLESS */
  }

  block_manager_remove(block_manager);
}

void mf_reallocate(mf_t mf, blockid_t bid, size_t new_length) {
  mf_main_t* mf_main = (mf_main_t*) mf;
  size_class_t old_sc, new_sc;
  offset_t old_ofs, new_ofs;
  block_manager_t* old_block_manager, *new_block_manager;

  old_sc = block_info_get_sc(mf_main->block_info_ptr, bid);
  new_sc = size2sc(new_length) - mf_main->sc_min + 1;
  if (new_sc == old_sc) return;

  old_block_manager = mf_main->block_managers[old_sc - 1];
  new_block_manager = mf_main->block_managers[new_sc - 1];

  old_ofs = block_info_get_offset(mf_main->block_info_ptr, bid);
  new_ofs = block_manager_append(new_block_manager);

  memcpy(block_manager_addr(new_block_manager, new_ofs),
    block_manager_addr(old_block_manager, old_ofs),
    MF_MIN(new_block_manager->obj_size, old_block_manager->obj_size));
  mf_deallocate(mf_main, bid);

  block_info_put_sc(mf_main->block_info_ptr, bid, new_sc);
  block_info_put_offset(mf_main->block_info_ptr, bid, new_ofs);
}

void* mf_dereference(mf_t mf, blockid_t bid) {
  mf_main_t* mf_main = (mf_main_t*)mf;
  size_class_t size_class;
  offset_t ofs;
  block_manager_t* block_manager;

  size_class = block_info_get_sc(mf_main->block_info_ptr, bid);
  if (size_class == 0) return NULL;

  ofs = block_info_get_offset(mf_main->block_info_ptr, bid);
  assert(size_class > 0);
  block_manager = mf_main->block_managers[size_class - 1];
#if FIXED_LENGTH_INTEGER
  return ptr_offset(block_manager_addr(block_manager, ofs), sizeof(blockid_t));
#else  /* FIXED_LENGTH_INTEGER */
  return ptr_offset(block_manager_addr(block_manager, ofs), mf_main->id_byte);
#endif /* FIXED_LENGTH_INTEGER */
}

const void* mf_dereference_c(const mf_t mf, blockid_t bid) {
  const mf_main_t* mf_main = (const mf_main_t*)mf;
  block_manager_t* block_manager;
  size_class_t size_class;
  offset_t ofs;

  size_class = block_info_get_sc(mf_main->block_info_ptr, bid);
  if (size_class == 0) return NULL;

  ofs = block_info_get_offset(mf_main->block_info_ptr, bid);
  assert(size_class > 0);
  block_manager = mf_main->block_managers[size_class - 1];
#if FIXED_LENGTH_INTEGER
  return ptr_offset(block_manager_addr(block_manager, ofs), sizeof(blockid_t));
#else  /* FIXED_LENGTH_INTEGER */
  return ptr_offset(block_manager_addr(block_manager, ofs), mf_main->id_byte);
#endif /* FIXED_LENGTH_INTEGER */
}

size_t mf_length(const mf_t mf, blockid_t bid) {
  const mf_main_t* mf_main = (const mf_main_t*)mf;
  size_class_t size_class = block_info_get_sc(mf_main->block_info_ptr, bid);
  return size_class == 0 ? 0 : sc2size(size_class - 1 + mf_main->sc_min);
}

size_t mf_dereference_and_length(mf_t mf, blockid_t bid, void** elem_addr) {
  mf_main_t* mf_main = (mf_main_t*)mf;
  size_class_t size_class;
  offset_t ofs;
  block_manager_t* block_manager;

  size_class = block_info_get_sc(mf_main->block_info_ptr, bid);
  ofs = block_info_get_offset(mf_main->block_info_ptr, bid);
  if (size_class == 0) {
    *elem_addr = NULL;
    return 0;
  } else {
    block_manager = mf_main->block_managers[size_class - 1];
    assert(block_manager != NULL);
    *elem_addr =
#if FIXED_LENGTH_INTEGER
      ptr_offset(block_manager_addr(block_manager, ofs), sizeof(blockid_t));
#else  /* FIXED_LENGTH_INTEGER */
      ptr_offset(block_manager_addr(block_manager, ofs), mf_main->id_byte);
#endif /* FIXED_LENGTH_INTEGER */
    return sc2size(size_class - 1 + mf_main->sc_min);
  }
}

size_t mf_using_mem(const mf_t mf) {
  const mf_main_t* mf_main = (const mf_main_t*)mf;
  size_t ret_size = 0;
  size_class_t size_class;
  block_manager_t* objs;
  size_t block_manager_nr = mf_main->sc_max - mf_main->sc_min + 1;

  ret_size += sizeof(mf_main_t);
  ret_size += sizeof(block_manager_t*) * block_manager_nr;
  for (size_class = 0; size_class < block_manager_nr; ++size_class) {
    objs = mf_main->block_managers[size_class];
    ret_size += block_manager_using_mem(objs);
  }
  ret_size += block_info_using_mem(mf_main->block_info_ptr);
#if ENABLE_HEURISTIC
  ret_size += pool_get_size();
  ret_size += garbage_get_size();
#endif /* ENABLE_HEURISTIC */
  return ret_size;
}
