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
#define _GNU_SOURCE
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include "ioctl.h"
#include "virtual_multiheap_fit.h"

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
#    define SIZE_CLASS_MAX 64
#  endif
#  ifndef BINARY_SEARCH_COUNT
#    define BINARY_SEARCH_COUNT 6
#  endif
#  ifndef SIZE_CLASS_CONST
#    define SIZE_CLASS_CONST 0.125
#  endif
#endif

/* If this flag is set, fixed length integer(uint32_t) is used
   insteada of variable length integer. */
#ifndef FIXED_LENGTH_INTEGER
#  define FIXED_LENGTH_INTEGER 0
#endif

/* vmf_main will be allocated at a multiple of MEMORY_ALIGN.
   This value should be a power of 2. */
#ifndef MEMORY_ALIGN
#  if FIXED_LENGTH_INTEGER
#    define MEMORY_ALIGN sizeof(blockid_t)
#  else
#    define MEMORY_ALIGN 1
#  endif
#endif

/* huristic parameter */
#ifndef ENABLE_HEURISTIC
#  define ENABLE_HEURISTIC 1
#endif

#if ENABLE_HEURISTIC
#  ifndef POOL_PAGE_NUM
#    define POOL_PAGE_NUM 8
#  endif
#endif

#define DEVICE_NAME "/dev/vmf_module0"
#define PAGE_SIZE 0x1000ULL
#define ONE_BYTE 8

#define VMF_MIN(a, b) ((a) < (b) ? (a) : (b))
#define VMF_MAX(a, b) ((a) > (b) ? (a) : (b))
#define VMF_INLINE static inline

/* a type used for passing page id */
typedef uint32_t pageid_t;
/* a type used for passing offsets of a page */
typedef uint32_t offset_t;
/* a type used for passing size classes  */
typedef uint32_t size_class_t;
/* a type used for the number of bytes */
typedef uint_fast16_t bytenum_t;

/* ========================================================================== */
/* commonly used functions */
/* ========================================================================== */

/** open 'filename' and exit if 'open' is failed */
VMF_INLINE int file_open(const char* filename);
/** call 'malloc' and exit if 'malloc' is failed */
VMF_INLINE void* safe_malloc(size_t size);

/* align up size to a multiple of MEMORY_ALIGN */
VMF_INLINE size_t align_up(size_t size);

#if !FIXED_LENGTH_INTEGER
/* read 'byte_num' bytes integer */
VMF_INLINE uint64_t get_int(const void* input, bytenum_t byte_num);
/* write 'byte_num' bytes integer */
VMF_INLINE void put_int(void* output, bytenum_t byte_num, uint64_t input);
/* [all one 'byte_num' bytes integer] */
VMF_INLINE uint64_t get_allone_value(size_t byte_num);
/* set_int(input, byte_num, [all one 'byte_num'] bytes integer]) */
VMF_INLINE void put_allone_int(void* output, size_t byte_num);
#endif /* FIXED_LENGTH_INTEGER */

/* fast memcpy(expect compiler optimization) */
VMF_INLINE void my_memcpy(void* __restrict__ dst, const void* __restrict__ src,
  size_t size);

/* (void*)((uint8_t*)addr + diff) */
VMF_INLINE void* ptr_offset(void* ptr, ptrdiff_t diff);

/* ========================================================================== */
/* size_class */
/* ========================================================================== */

#if !EXACT_SIZE_CLASS
struct size_manager {
  size_t sizeof_class[SIZE_CLASS_MAX];
};

static struct size_manager g_size_manager;
#endif /* EXACT_SIZE_CLASS */

/* initialize size_manager */
VMF_INLINE void size_manager_init(void);
/* convert size to size_class */
VMF_INLINE size_t size2sc(size_t size);
/* convert size_class to size */
VMF_INLINE size_t sc2size(size_t size_class);

/* ========================================================================== */
/* kernel module communication */
/* ========================================================================== */

/* Structure that stores information related to the driver */
typedef struct {
  /* driver file */
  int driver_fd;
  /* max reserved address */
  void* addr_max;
  /* min reserved address */
  void* addr_min;

  /* physical pagesize. This value is not necessarily equal to 4096. */
  size_t physical_pagesize;
} module_t;

/** Initialize module_t */
VMF_INLINE module_t* module_init(size_t mem_max, size_t total_sup);
/** Finalize module_t */
VMF_INLINE void module_final(module_t* module);
/** Calculate 'pid' page's address */
VMF_INLINE void* module_get_address(const module_t* module, pageid_t pid);
/** Create nextpage's mapping */
VMF_INLINE void module_set_next(module_t* module,
  pageid_t main_page, pageid_t next_page);
/** Destroy nextpage's mapping */
VMF_INLINE void module_reset_next(module_t* module, pageid_t main_page);
/** Allocate physical page */
VMF_INLINE void module_allocate(module_t* module, pageid_t pid);
/** Deallocate physical page */
VMF_INLINE void module_deallocate(module_t* module, pageid_t pid);
/** Set physical page size */
VMF_INLINE void module_set_pagesize(module_t* module, size_t max_size);
/** Get physical page size */
VMF_INLINE size_t module_get_pagesize(module_t* module);

/** Total using vmf_main in 'module' */
VMF_INLINE size_t module_get_size(const module_t* module);

/* ========================================================================== */
/* pseudo_heap */
/* ========================================================================== */

/* (virtual) heap */
typedef struct {
  /* mmaped addr */
  void*  addr;
  /* the number of mmaped pages */
  size_t page_num;
  /* page size. This value is not necessarily equal to 4096. */
  size_t page_size;
  /* shift num to multiple 'page_size' */
  size_t page_shift;
} pseudo_heap_t;

/** Initialize pseudo_heap */
VMF_INLINE pseudo_heap_t* pheap_init(void);
/** Finalize pseudo_heap */
VMF_INLINE void pheap_final(pseudo_heap_t* pheap);
/** pheap->addr */
VMF_INLINE void* pheap_addr(pseudo_heap_t* pheap);
/** Resize the length of heap to new_length */
VMF_INLINE void pheap_resize(pseudo_heap_t* pheap, size_t new_length);

/** Total using vmf_main in 'pheap' */
VMF_INLINE size_t pheap_get_size(const pseudo_heap_t* pheap);

/* ========================================================================== */
/* block_info */
/* ========================================================================== */

/* image
  struct blockent_block {
    <o byte> ofs;
    <l byte> page;
  };
*/
#if FIXED_LENGTH_INTEGER
typedef struct {
  offset_t ofs;
  pageid_t pid;
} block_data_t;
#else  /* FIXED_LENGTH_INTEGER */
#define ELEMENT_BLOCK_START_OFS(ptr)  (void*)((uint8_t*)ptr)
#define ELEMENT_BLOCK_OFS_OFS(ptr)    ELEMENT_BLOCK_START_OFS(ptr)
#define ELEMENT_BLOCK_PAGE_OFS(ptr, o)\
  (void*)((uint8_t*)ELEMENT_BLOCK_OFS_OFS(ptr) + o)
#define ELEMENT_BLOCK_END_OFS(ptr, o, l)\
  (void*)((uint8_t*)ELEMENT_BLOCK_PAGE_OFS(ptr, o) + l)
#define ELEMENT_BLOCK_SIZE(o, l)\
  ((uint8_t*)ELEMENT_BLOCK_END_OFS(0, o, l) \
    - (uint8_t*)ELEMENT_BLOCK_START_OFS(0))
#endif /* FIXED_LENGTH_INTEGER */

typedef struct {
  /* number of blocks */
  blockid_t block_nr;

#if FIXED_LENGTH_INTEGER
  block_data_t* data_start;
#else  /* FIXED_LENGTH_INTEGER */
  /* data region */
  void* data_start;
  /* byte num to represent page ID */
  bytenum_t page_byte;  /* l */
  /* byte num to represent offset in a page */
  bytenum_t ofs_byte;   /* o */
  /* ELEM_INFO_SIZE(l, o) */
  size_t block_size;
#endif /* FIXED_LENGTH_INTEGER */
} block_info_t;

/** Constructor */
#if FIXED_LENGTH_INTEGER
VMF_INLINE block_info_t* block_info_init(size_t block_nr_max);
#else  /* FIXED_LENGTH_INTEGER */
VMF_INLINE block_info_t* block_info_init(
  bytenum_t ofs_byte, bytenum_t page_byte, size_t block_nr_max);
#endif /* FIXED_LENGTH_INTEGER */
/** block_info->block_size */
VMF_INLINE size_t block_info_block_size(const block_info_t* block_info);
/** Destructor */
VMF_INLINE void block_info_final(block_info_t* block_info);
/** Get all information about the block 'bid' */
VMF_INLINE void* block_info_get_all(block_info_t* block_info,
  blockid_t bid, offset_t* ofs, pageid_t* page_id);
/** Insert 'ofs' and 'page_id' to 'bid's information*/
VMF_INLINE void block_info_push(block_info_t* block_info, blockid_t bid,
  offset_t ofs, pageid_t page_id);
/** Read offset of the block 'bid' */
VMF_INLINE offset_t block_info_get_ofs(
  const block_info_t* block_info, blockid_t bid);
/** Write offset of the block 'bid' */
VMF_INLINE void block_info_put_ofs(
  block_info_t* block_info, blockid_t bid, offset_t new_ofs);
/** Read page of the block 'bid' */
VMF_INLINE pageid_t block_info_get_pid(
  const block_info_t* block_info, blockid_t bid);
/** Write page of the block 'bid' */
VMF_INLINE void block_info_put_pid(
  block_info_t* block_info, blockid_t bid, pageid_t new_page);

/** faster version */
/** Get addr of the information about the block 'bid' */
VMF_INLINE void* block_info_get_block_ptr(
  const block_info_t* block_info, blockid_t bid);
/** Rewrite pid to null_page. 'block_addr' is a addr
  returned by 'block_info_get_block_ptr' */
VMF_INLINE void block_info_fastput_null_page(
  block_info_t* block_info, void* block_addr);

/** Total using vmf_main in block_info */
VMF_INLINE size_t block_info_get_size(const block_info_t* block_info);

/* ========================================================================== */
/* page_info */
/* ========================================================================== */

#if FIXED_LENGTH_INTEGER
typedef struct {
  pageid_t prev_page;
  pageid_t next_page;
  offset_t ofs;
  size_class_t size_class;
} page_data_t;
#else  /* FIXED_LENGTH_INTEGER */
/* Image
  struct page_data {
    <l byte>      prev_page;
    <l byte>      next_page;
    <o byte>      page_offset;
    <o byte>      size_class;
  };
*/
#define PAGE_INFO_START_OFS(ptr)      (void*)((uint8_t*)ptr)
#define PAGE_INFO_PREV_PAGE_OFS(ptr) ((void*)PAGE_INFO_START_OFS(ptr))
#define PAGE_INFO_NEXT_PAGE_OFS(ptr, l)\
  (void*)((uint8_t*)PAGE_INFO_PREV_PAGE_OFS(ptr) + l)
#define PAGE_INFO_OFFSET_OFS(ptr, l)\
  (void*)((uint8_t*)PAGE_INFO_NEXT_PAGE_OFS(ptr, l) + l)
#define PAGE_INFO_SC_OFS(ptr, l, o)\
  (void*)((uint8_t*)PAGE_INFO_OFFSET_OFS(ptr, l) + o)
#define PAGE_INFO_END_OFS(ptr, l, o)\
  (void*)((uint8_t*)PAGE_INFO_SC_OFS(ptr, l, o) + o)
#define PAGE_INFO_SIZE(l, o)\
  ((uint8_t*)PAGE_INFO_END_OFS(0, l, o) - (uint8_t*)PAGE_INFO_START_OFS(0))
#endif /* FIXED_LENGTH_INTEGER */

typedef struct {
  /* pseudo heap to store page information */
  pseudo_heap_t* data_heap;
  /* number of page stored in this struct */
  pageid_t page_num;

#if !FIXED_LENGTH_INTEGER
  /* number of byte to represent page id */
  bytenum_t page_byte;  /* l */
  /* number of byte to represent offset */
  bytenum_t ofs_byte;   /* o */
  /* PAGE_INFO_SIZE(l, o) */
  size_t block_size;
#endif /* !FIXED_LENGTH_INTEGER */

#if ENABLE_HEURISTIC
  /* Stack of pages held without releasing mapping.
     Basically this stack is used in preference to 'id_heap'. */
  size_t pool_stack[POOL_PAGE_NUM];
  /* number of page IDs stacked in 'pool_stack' */
  size_t pool_nr;
#endif /* ENABLE_HEURISTIC */

  /* Heap that manages unused page id */
  pseudo_heap_t* id_heap;
  /* number of page IDs stacked in 'id_heap' */
  size_t stack_size;
} page_info_t;

/** Constructor */
#if FIXED_LENGTH_INTEGER
VMF_INLINE page_info_t* page_info_init(void);
#else  /* FIXED_LENGTH_INTEGER */
VMF_INLINE page_info_t* page_info_init(bytenum_t page_byte, bytenum_t ofs_byte);
#endif /* FIXED_LENGTH_INTEGER */
/** Destructor */
VMF_INLINE void page_info_final(page_info_t* page_info);
/** Push the page ID that are no longer used.
    If return value is true, the page 'pid' is stacked to 'pool_stack',
    so don't destroy mapping of the page.
    If return value is false, the page 'pid' is not stacked to 'pool_stack',
    so function caller should destroy mapping of the page. */
VMF_INLINE bool page_info_push_freeid(page_info_t* page_info,
    pageid_t free_id);
/** Get free page ID */
VMF_INLINE pageid_t page_info_pop_freeid(page_info_t* page_info, bool* mapping);
/** Rewrite all information about the page 'page_id' */
VMF_INLINE void page_info_replace(page_info_t* page_info,
  pageid_t page_id, pageid_t prev_id, pageid_t next_id,
  offset_t ofs, size_class_t size_class);
/** Read previous page of the page 'page_id' */
VMF_INLINE pageid_t page_info_get_prev(const page_info_t* page_info,
  pageid_t page_id);
/** Write previous page of the page 'page_id' */
VMF_INLINE void page_info_put_prev(page_info_t* page_info,
  pageid_t page_id, pageid_t prevpage_id);
/** Read next page of the page 'page_id' */
VMF_INLINE pageid_t page_info_get_next(const page_info_t* page_info,
  pageid_t page_id);
/** Write next page of the page 'page_id'*/
VMF_INLINE void page_info_put_next(page_info_t* page_info,
  pageid_t page_id, pageid_t nextpage_id);
/** Read offset of the page 'page_id' */
VMF_INLINE offset_t page_info_get_offset(const page_info_t* page_info,
  pageid_t page_id);
/** Write offset of the page 'page_id'*/
VMF_INLINE void page_info_put_offset(page_info_t* page_info,
  pageid_t page_id, offset_t page_offset);
/** Read size class of the page 'page_id' */
VMF_INLINE size_class_t page_info_get_sc(const page_info_t* page_info,
  pageid_t page_id);
/** Write size_class of the page 'page_id' */
VMF_INLINE void page_info_put_sc(page_info_t* page_info,
  pageid_t page_id, size_class_t size_class);

/** fast version */
/** Get addr of the information about the page 'page_id' */
VMF_INLINE void* page_info_get_pid(
  const page_info_t* page_info, pageid_t page_id);
/** Fast version of 'page_info_get_prev' */
VMF_INLINE pageid_t page_info_fast_get_prev(const page_info_t* page_info,
  void* block_addr);
/** Fast version of 'page_info_get_next' */
VMF_INLINE pageid_t page_info_fast_get_next(const page_info_t* page_info,
  void* block_addr);
/** Fast version of 'page_info_get_offset' */
VMF_INLINE offset_t page_info_fast_get_offset(const page_info_t* page_info,
  void* block_addr);
/** Fast version of 'page_info_get_sc' */
VMF_INLINE size_class_t page_info_fast_get_sc(const page_info_t* page_info,
  void* block_addr);

/** Total using vmf_main in info_ptr */
VMF_INLINE size_t get_size_page_info(const page_info_t* page_info);

/* ========================================================================== */
/* main structure */
/* ========================================================================== */

typedef struct {
  /* min size of allocated vmf_main */
  size_class_t  mem_min;          /* a */
  /* max size of allocated vmf_main */
  size_class_t  mem_max;          /* b */
  /* max number of vmf_main blocks */
  blockid_t     block_nr_max;   /* m */

#if !FIXED_LENGTH_INTEGER
  /* max total allocated size */
  bytenum_t     blockid_byte;     /* k */
  /* number of byte to represent a page ID */
  bytenum_t     page_byte;        /* l */
  /* number of byte to represent a offset of pages */
  bytenum_t     ofs_byte;         /* o */
  /* block ID which represents nullptr */
  blockid_t     null_block;
  /* page ID which represents nullptr */
  pageid_t      null_page;
#endif /* !FIXED_LENGTH_INTEGER */
  /* Head pages of each size class */
  void*         page_heads;       /* L[b] */

  /* Page size in kernel module */
  size_t physical_pagesize;

  /* {ofs, size_class, index} */
  block_info_t* block_info;
  /* {page2sec, prev_page, next_page, page_offset} */
  page_info_t*  page_info;
  /* kernel module communication */
  module_t* module;
} vmf_main_t;

/* ========================================================================== */
/* Commonly used functions */
/* ========================================================================== */

VMF_INLINE int file_open(const char* filename) {
  int fd = open(filename, O_RDWR);
  if (fd < 0) {
    perror(filename);
    exit(EXIT_FAILURE);
  }
  return fd;
}

VMF_INLINE void* safe_malloc(size_t size) {
  void* ret_value = malloc(size);
  if (ret_value == NULL) exit(EXIT_FAILURE);
  return ret_value;
}

#if !FIXED_LENGTH_INTEGER
VMF_INLINE uint64_t get_int(const void* input, bytenum_t byte_num) {
  uint64_t ret = 0;
  bytenum_t i = 0;
  const uint8_t* in_u8 = (const uint8_t*) input;

#if ENABLE_HEURISTIC
  if (byte_num == 1) {
    return *in_u8;
  } else if (byte_num == 2) {
    ret = *in_u8++;
    return (ret << 8) | *in_u8;
  } else if (byte_num == 3) {
    ret = *in_u8++;
    ret = (ret << 8) | *in_u8++;
    return (ret << 8) | *in_u8;
  } else if (byte_num == 4) {
    ret = *in_u8++;
    ret = (ret << 8) | *in_u8++;
    ret = (ret << 8) | *in_u8++;
    return (ret << 8) | *in_u8;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    for (i = 0; i < byte_num; ++i) {
      ret = (ret << 8) | *in_u8++;
    }
    return ret;
  }
}

VMF_INLINE void put_int(void* output, bytenum_t byte_num, uint64_t input) {
  bytenum_t i = 0;
  uint8_t* out_u8 = (uint8_t*) output + byte_num - 1;

#if ENABLE_HEURISTIC
  if (byte_num == 1) {
    *out_u8   = input & 0xff;
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

VMF_INLINE uint64_t get_allone_value(size_t byte_num) {
  uint64_t ret_value = 0;
  size_t i;
  for (i = 0; i < byte_num; ++i) {
    ret_value = (ret_value << 8) | 0xff;
  }
  return ret_value;
}

VMF_INLINE void put_allone_int(void* output, size_t byte_num) {
  size_t i = 0;
  uint8_t* out_u8 = (uint8_t*) output + byte_num - 1;
  for (i = 0; i < byte_num; ++i) {
    *out_u8-- = 0xff;
  }
}
#endif /* FIXED_LENGTH_INTEGER */

VMF_INLINE size_t align_up(size_t size) {
  return (size + MEMORY_ALIGN - 1) & ~(MEMORY_ALIGN - 1);
}

VMF_INLINE void my_memcpy(void* __restrict__ dst, const void* __restrict__ src,
    size_t size) {
  uint8_t* __restrict__ dst_u8 = (uint8_t* __restrict__) dst;
  const uint8_t* __restrict__ src_u8 = (const uint8_t* __restrict__) src;
  do {
    *dst_u8++ = *src_u8++;
  } while (--size);
}

VMF_INLINE void* ptr_offset(void* ptr, ptrdiff_t diff) {
  return (void*)((uint8_t*)ptr + diff);
}

/* ========================================================================== */
/* size_class */
/* ========================================================================== */

VMF_INLINE void size_manager_init(void) {
#if !EXACT_SIZE_CLASS
  size_t i;
  double curr_size = 8.0;
  for (i = 0; i < SIZE_CLASS_MAX; ++i) {
    g_size_manager.sizeof_class[i] = (size_t)curr_size;
    curr_size *= 1.0 + SIZE_CLASS_CONST;
    curr_size  = align_up(ceil(curr_size));
  }
#endif  /* EXACT_SIZE_CLASS */
}

VMF_INLINE size_t size2sc(size_t size) {
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

VMF_INLINE size_t sc2size(size_t size_class) {
#if EXACT_SIZE_CLASS
  return size_class * MEMORY_ALIGN;
#else  /* EXACT_SIZE_CLASS */
  return g_size_manager.sizeof_class[size_class];
#endif /* EXACT_SIZE_CLASS */
}

/* ========================================================================== */
/* kernel module communication */
/* ========================================================================== */

/** Create mapping from `index`(virtual) to `page_id`(physical) */
VMF_INLINE void my_mmap(module_t* info, size_t index, pageid_t page_id);
/** Destroy mapping from `index` */
VMF_INLINE void my_munmap(module_t* info, size_t index);

/** Convert page_id to virtual page index */
VMF_INLINE size_t main_index(pageid_t pid) {
  return 2 * pid;
}
/** Convert page_id to next to main_index(pid) */
VMF_INLINE size_t sub_index(pageid_t pid) {
  return 2 * pid + 1;
}

VMF_INLINE void* get_address_by_index(const module_t* module,
    pageid_t pid) {
  return ptr_offset(module->addr_min, pid * module->physical_pagesize);
}

VMF_INLINE module_t* module_init(size_t mem_max, size_t total_sup) {
  int err;
  size_t mmap_size;
  size_t physical_pagesize;
  module_t* module;
  unsigned long page_nr_max;

  module = (module_t*) safe_malloc(sizeof(module_t));

  module->driver_fd = file_open(DEVICE_NAME);
  module_set_pagesize(module, mem_max);

  physical_pagesize = module->physical_pagesize;
  mmap_size = (total_sup * 4 + physical_pagesize - 1)
    & ~(physical_pagesize - 1);
  module->addr_min = mmap64(0, 2 * mmap_size, PROT_NONE,
    MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
  if (module->addr_min == MAP_FAILED) {
    perror("initialize");
    exit(EXIT_FAILURE);
  }

  page_nr_max = mmap_size / module->physical_pagesize;
  err = ioctl(module->driver_fd, ALLOCATOR_IOC_RESIZE, &page_nr_max);
  if (err < 0) {
    perror("ioctl alloc");
    exit(EXIT_FAILURE);
  }

  module->addr_max = ptr_offset(module->addr_min, 2 * mmap_size);

  return module;
}

VMF_INLINE void module_final(module_t* module) {
  close(module->driver_fd);
  free(module);
}

VMF_INLINE void* module_get_address(const module_t* module, pageid_t pid) {
  return get_address_by_index(module, main_index(pid));
}

VMF_INLINE void module_set_next(module_t* module,
    pageid_t main_page, pageid_t next_page) {
  my_mmap(module, sub_index(main_page), next_page);
}

VMF_INLINE void module_reset_next(module_t* module, pageid_t main_page) {
  my_munmap(module, sub_index(main_page));
}

VMF_INLINE void module_allocate(module_t* module, pageid_t pid) {
  int err;
  unsigned long page_id_arg = pid;

  err = ioctl(module->driver_fd, ALLOCATOR_IOC_ALLOC, &page_id_arg);
  if (err < 0) {
    perror(__FUNCTION__);
    exit(EXIT_FAILURE);
  }

  my_mmap(module, main_index(pid), pid);
}

VMF_INLINE void module_deallocate(module_t* module, pageid_t pid) {
  unsigned long page_id_arg = pid;
  int err;

  my_munmap(module, main_index(pid));
  err = ioctl(module->driver_fd, ALLOCATOR_IOC_DEALLOC, &page_id_arg);
  if (err < 0) {
    perror(__FUNCTION__);
    exit(EXIT_FAILURE);
  }
}

VMF_INLINE void module_set_pagesize(module_t* module, size_t max_size) {
  unsigned order = 0;
  size_t physical_pagesize = PAGE_SIZE;

  max_size /= PAGE_SIZE;
  while (max_size > 0) {
    order++;
    max_size /= 2;
    physical_pagesize *= 2;
  }
  ioctl(module->driver_fd, ALLOCATOR_IOC_SET_PAGESIZE_ORDER, &order);
  module->physical_pagesize = physical_pagesize;
}

VMF_INLINE size_t module_get_pagesize(module_t* module) {
  return module->physical_pagesize;
}


VMF_INLINE size_t module_get_size(const module_t* module) {
  unsigned long driver_using_size;

  ioctl(module->driver_fd, ALLOCATOR_IOC_TOTAL_SIZE, &driver_using_size);
  return sizeof(module_t) + driver_using_size;
}

VMF_INLINE void my_mmap(module_t* module, size_t index, pageid_t pid) {
  void* addr = mmap64(
      get_address_by_index(module, index),
      module->physical_pagesize, PROT_READ | PROT_WRITE,
      MAP_SHARED | MAP_FIXED, module->driver_fd,
      pid * module->physical_pagesize);
  if (addr == MAP_FAILED) {
    perror(__FUNCTION__);
    exit(EXIT_FAILURE);
  }
}

VMF_INLINE void my_munmap(module_t* module, size_t index) {
  void* ret_addr =
    mmap64(
      get_address_by_index(module, index), module->physical_pagesize,
      PROT_NONE, MAP_PRIVATE | MAP_FIXED | MAP_ANONYMOUS | MAP_NORESERVE,
      -1, 0);
  if (ret_addr == MAP_FAILED) {
    perror(__FUNCTION__);
    exit(EXIT_FAILURE);
  }
}

/* ========================================================================== */
/* pseudo_heap */
/* ========================================================================== */

VMF_INLINE size_t calc_page_num(const pseudo_heap_t* pheap, size_t length) {
  return (length >> pheap->page_shift) + 1;
}

VMF_INLINE pseudo_heap_t* pheap_init(void) {
  pseudo_heap_t* pheap;
  size_t page_size;
  size_t page_shift;

  pheap = (pseudo_heap_t*)safe_malloc(sizeof(pseudo_heap_t));

  page_size = getpagesize();
  pheap->addr = mmap(0, page_size, PROT_READ | PROT_WRITE,
    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (pheap->addr == MAP_FAILED) {
    perror("pheap_init init");
    exit(EXIT_FAILURE);
  }

  pheap->page_size = page_size;
  pheap->page_num  = 1;

  page_shift = 0;
  while (page_size > 1) {
    page_shift++;
    page_size /= 2;
  }
  pheap->page_shift = page_shift;

  return pheap;
}

VMF_INLINE void pheap_final(pseudo_heap_t* pheap) {
  munmap(pheap->addr, pheap->page_num << pheap->page_shift);
  free(pheap);
}

VMF_INLINE void* pheap_addr(pseudo_heap_t* pheap) {
  return pheap->addr;
}

VMF_INLINE void pheap_resize(pseudo_heap_t* pheap, size_t new_length) {
  void* heap_addr = pheap->addr;
  size_t page_shift = pheap->page_shift;
  size_t old_page_num = pheap->page_num;
  size_t new_page_num = calc_page_num(pheap, new_length);

  if (new_page_num == old_page_num) return;
  pheap->addr = mremap(heap_addr, old_page_num << page_shift,
      new_page_num << page_shift, MREMAP_MAYMOVE);
  pheap->page_num = new_page_num;
  if (pheap->addr == MAP_FAILED) {
    perror("pheap_resize mremap");
    exit(EXIT_FAILURE);
  }
}

VMF_INLINE size_t pheap_get_size(const pseudo_heap_t* pheap) {
  return sizeof(*pheap) + (pheap->page_num << pheap->page_shift);
}

/* ========================================================================== */
/* block_info */
/* ========================================================================== */

#if FIXED_LENGTH_INTEGER
VMF_INLINE block_info_t* block_info_init(size_t block_nr_max)
#else  /* FIXED_LENGTH_INTEGER */
VMF_INLINE block_info_t* block_info_init(bytenum_t ofs_byte,
    bytenum_t page_byte, size_t block_nr_max)
#endif /* FIXED_LENGTH_INTEGER */
{
  block_info_t* block_info;
  size_t data_size;

  block_info = (block_info_t*) malloc(sizeof(block_info_t));
  if (block_info == NULL) exit(EXIT_FAILURE);

  block_info->block_nr = block_nr_max;
#if FIXED_LENGTH_INTEGER
  data_size = block_nr_max * sizeof(block_data_t);
#else  /* FIXED_LENGTH_INTEGER */
  data_size = block_nr_max * ELEMENT_BLOCK_SIZE(ofs_byte, page_byte);
  block_info->page_byte   = page_byte;
  block_info->ofs_byte    = ofs_byte;
  block_info->block_size  = ELEMENT_BLOCK_SIZE(ofs_byte, page_byte);
#endif /* FIXED_LENGTH_INTEGER */
  block_info->data_start = safe_malloc(data_size);
  /* Initialize page to 'null_page' */
  memset(block_info->data_start, 0xff, data_size);

  return block_info;
}

VMF_INLINE size_t block_info_block_size(const block_info_t* block_info) {
#if FIXED_LENGTH_INTEGER
  return sizeof(block_data_t);
#else /* FIXED_LENGTH_INTEGER */
  return block_info->block_size;
#endif /* FIXED_LENGTH_INTEGER */
}

VMF_INLINE void block_info_final(block_info_t* block_info) {
  free(block_info->data_start);
  block_info->data_start = NULL;
  free(block_info);
}

#if FIXED_LENGTH_INTEGER
VMF_INLINE void* block_info_get_all(block_info_t* block_info,
    blockid_t bid, offset_t* ofs, pageid_t* page_id) {
  block_data_t* block_data = &block_info->data_start[bid];
  *ofs = block_data->ofs;
  *page_id = block_data->pid;
  return block_data;
}

VMF_INLINE void block_info_push(block_info_t* block_info, blockid_t bid,
    offset_t ofs, pageid_t page_id) {
  block_data_t* block_data = &block_info->data_start[bid];
  block_data->ofs = ofs;
  block_data->pid = page_id;
}

VMF_INLINE offset_t block_info_get_ofs(
    const block_info_t* block_info, blockid_t bid) {
  return block_info->data_start[bid].ofs;
}

VMF_INLINE void block_info_put_ofs(
    block_info_t* block_info, blockid_t bid, offset_t new_ofs) {
  block_info->data_start[bid].ofs = new_ofs;
}

VMF_INLINE pageid_t block_info_get_pid(
    const block_info_t* block_info, blockid_t bid) {
  return block_info->data_start[bid].pid;
}

VMF_INLINE void block_info_put_pid(
    block_info_t* block_info, blockid_t bid, pageid_t new_page) {
  block_info->data_start[bid].pid = new_page;
}

VMF_INLINE void* block_info_get_block_ptr(
    const block_info_t* block_info, blockid_t bid) {
  return &block_info->data_start[bid];
}

VMF_INLINE void block_info_fastput_null_page(
    block_info_t* block_info, void* block_addr) {
  ((block_data_t*)block_addr)->pid = (pageid_t)(-1);
}

VMF_INLINE size_t block_info_get_size(const block_info_t* block_info) {
  return sizeof(block_info_t) + block_info->block_nr * sizeof(block_data_t);
}

#else  /* FIXED_LENGTH_INTEGER */
VMF_INLINE void* block_info_get_all(block_info_t* block_info,
    blockid_t bid, offset_t* ofs, pageid_t* page_id) {
  void* block_addr = block_info_get_block_ptr(block_info, bid);
  const uint8_t* in_u8 = (const uint8_t*)block_addr;
  bytenum_t page_byte = block_info->page_byte;
  bytenum_t ofs_byte  = block_info->ofs_byte;
  bytenum_t i;

  assert(bid < block_info->block_nr);

  /* ofs_byte is always greater than 2 */
#if ENABLE_HEURISTIC
  if (ofs_byte == 2) {
    *ofs = *in_u8++;
    *ofs = (*ofs << 8) | *in_u8++;
  } else if (ofs_byte == 3) {
    *ofs = *in_u8++;
    *ofs = (*ofs << 8) | *in_u8++;
    *ofs = (*ofs << 8) | *in_u8++;
  } else if (ofs_byte == 4) {
    *ofs = *in_u8++;
    *ofs = (*ofs << 8) | *in_u8++;
    *ofs = (*ofs << 8) | *in_u8++;
    *ofs = (*ofs << 8) | *in_u8++;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    *ofs = 0;
    for (i = 0; i < ofs_byte; ++i) {
      *ofs = (*ofs << 8) | *in_u8++;
    }
  }

#if ENABLE_HEURISTIC
  if (page_byte == 1) {
    *page_id = *in_u8;
  } else if (page_byte == 2) {
    *page_id = *in_u8++;
    *page_id = (*page_id << 8) | *in_u8;
  } else if (page_byte == 3) {
    *page_id = *in_u8++;
    *page_id = (*page_id << 8) | *in_u8++;
    *page_id = (*page_id << 8) | *in_u8;
  } else if (page_byte == 4) {
    *page_id = *in_u8++;
    *page_id = (*page_id << 8) | *in_u8++;
    *page_id = (*page_id << 8) | *in_u8++;
    *page_id = (*page_id << 8) | *in_u8;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    *page_id = 0;
    for (i = 0; i < page_byte; ++i) {
      *page_id = (*page_id << 8) | *in_u8++;
    }
  }
  return (void*)block_addr;
}

VMF_INLINE void block_info_push(block_info_t* block_info,
    blockid_t bid, offset_t ofs, pageid_t page_id) {
  void* block_addr = block_info_get_block_ptr(block_info, bid + 1);
  uint8_t* out_u8 = (uint8_t*)block_addr - 1;
  bytenum_t page_byte = block_info->page_byte;
  bytenum_t ofs_byte = block_info->ofs_byte;
  bytenum_t i;

  assert(bid < block_info->block_nr);

#if ENABLE_HEURISTIC
  if (page_byte == 1) {
    *out_u8-- = page_id & 0xff;
  } else if (page_byte == 2) {
    *out_u8-- = page_id & 0xff;  page_id >>= 8;
    *out_u8-- = page_id;
  } else if (page_byte == 3) {
    *out_u8-- = page_id & 0xff;  page_id >>= 8;
    *out_u8-- = page_id & 0xff;  page_id >>= 8;
    *out_u8-- = page_id;
  } else if (page_byte == 4) {
    *out_u8-- = page_id & 0xff;  page_id >>= 8;
    *out_u8-- = page_id & 0xff;  page_id >>= 8;
    *out_u8-- = page_id & 0xff;  page_id >>= 8;
    *out_u8-- = page_id;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    for (i = 0; i < page_byte; ++i) {
      *out_u8-- = page_id & 0xff;  page_id >>= 8;
    }
  }

#if ENABLE_HEURISTIC
  if (ofs_byte == 2) {
    *out_u8-- = ofs & 0xff;     ofs >>= 8;
    *out_u8   = ofs & 0xff;
  } else if (ofs_byte == 3) {
    *out_u8-- = ofs & 0xff;     ofs >>= 8;
    *out_u8-- = ofs & 0xff;     ofs >>= 8;
    *out_u8   = ofs & 0xff;
  } else if (ofs_byte == 4) {
    *out_u8-- = ofs & 0xff;     ofs >>= 8;
    *out_u8-- = ofs & 0xff;     ofs >>= 8;
    *out_u8-- = ofs & 0xff;     ofs >>= 8;
    *out_u8   = ofs & 0xff;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    for (i = 0; i < ofs_byte; ++i) {
      *out_u8-- = ofs & 0xff;  ofs >>= 8;
    }
  }
}

VMF_INLINE offset_t block_info_get_ofs(
    const block_info_t* block_info, blockid_t bid) {
  assert(bid < block_info->block_nr);
  return get_int(
    ELEMENT_BLOCK_OFS_OFS(block_info_get_block_ptr(block_info, bid)),
    block_info->ofs_byte);
}

VMF_INLINE void block_info_put_ofs(
    block_info_t* block_info, blockid_t bid, offset_t new_ofs) {
  assert(bid < block_info->block_nr);
  put_int(
    ELEMENT_BLOCK_OFS_OFS(block_info_get_block_ptr(block_info, bid)),
    block_info->ofs_byte,
    new_ofs);
}

VMF_INLINE pageid_t block_info_get_pid(
    const block_info_t* block_info, blockid_t bid) {
  assert(bid < block_info->block_nr);
  return get_int(
    ELEMENT_BLOCK_PAGE_OFS(block_info_get_block_ptr(block_info, bid),
      block_info->ofs_byte),
    block_info->page_byte);
}

VMF_INLINE void block_info_put_pid(
    block_info_t* block_info, blockid_t bid, pageid_t new_page_id) {
  assert(bid < block_info->block_nr);
  put_int(
    ELEMENT_BLOCK_PAGE_OFS(block_info_get_block_ptr(block_info, bid),
      block_info->ofs_byte),
    block_info->page_byte,
    new_page_id);
}

VMF_INLINE void block_info_fastput_null_page(
    block_info_t* block_info, void* block_addr) {
  put_allone_int(
    ELEMENT_BLOCK_PAGE_OFS(block_addr,
      block_info->ofs_byte),
    block_info->page_byte);
}

VMF_INLINE void* block_info_get_block_ptr(
    const block_info_t* block_info, blockid_t bid) {
  return ptr_offset(block_info->data_start, block_info->block_size * bid);
}

VMF_INLINE size_t block_info_get_size(const block_info_t* block_info) {
  return sizeof(block_info_t)
    + block_info->block_size * block_info->block_nr;
}
#endif /* FIXED_LENGTH_INTEGER */

/* ========================================================================== */
/* page_info */
/* ========================================================================== */

VMF_INLINE void* page_info_get_pid(
    const page_info_t* page_info, pageid_t page_id) {
  ptrdiff_t offset;
#if FIXED_LENGTH_INTEGER
  offset = page_id * sizeof(page_data_t);
#else  /* FIXED_LENGTH_INTEGER */
  offset = page_id * page_info->block_size;
#endif /* FIXED_LENGTH_INTEGER */
  return ptr_offset(pheap_addr(page_info->data_heap), offset);
}

VMF_INLINE pageid_t pop_stacktop(page_info_t* page_info) {
  void* stacktop_addr;
  pageid_t ret_id;
  size_t new_heap_size;

  page_info->stack_size--;
#if FIXED_LENGTH_INTEGER
  new_heap_size = page_info->stack_size * sizeof(pageid_t);
  stacktop_addr = ptr_offset(pheap_addr(page_info->id_heap), new_heap_size);
  ret_id = *(pageid_t*)stacktop_addr;
#else  /* FIXED_LENGTH_INTEGER */
  new_heap_size = page_info->page_byte * page_info->stack_size;
  stacktop_addr = ptr_offset(pheap_addr(page_info->id_heap), new_heap_size);
  ret_id = get_int(stacktop_addr, page_info->page_byte);
#endif /* FIXED_LENGTH_INTEGER */

  pheap_resize(page_info->id_heap, new_heap_size);
  return ret_id;
}

#if FIXED_LENGTH_INTEGER
VMF_INLINE page_info_t* page_info_init(void)
#else  /* FIXED_LENGTH_INTEGER */
VMF_INLINE page_info_t* page_info_init(bytenum_t page_byte,
    bytenum_t ofs_byte)
#endif /* FIXED_LENGTH_INTEGER */
{
  page_info_t* page_info;

  page_info = (page_info_t*) malloc(sizeof(page_info_t));
  if (page_info == NULL) exit(EXIT_FAILURE);

  page_info->data_heap   = pheap_init();
  page_info->page_num    = 0;

#if !FIXED_LENGTH_INTEGER
  page_info->page_byte   = page_byte;
  page_info->ofs_byte    = ofs_byte;
  page_info->block_size  = PAGE_INFO_SIZE(page_byte, ofs_byte);
#endif /* FIXED_LENGTH_INTEGER */

#if ENABLE_HEURISTIC
  page_info->pool_nr     = 0;
#endif /* ENABLE_HEURISTIC */

  page_info->id_heap     = pheap_init();
  page_info->stack_size  = 0;

  return page_info;
}

VMF_INLINE void page_info_final(page_info_t* page_info) {
  pheap_final(page_info->data_heap);
  pheap_final(page_info->id_heap);
  free(page_info);
}

VMF_INLINE bool page_info_push_freeid(page_info_t* page_info,
    pageid_t free_id) {
  size_t new_heap_size;
  void* stacktop_addr;

#if ENABLE_HEURISTIC
  if (page_info->pool_nr < POOL_PAGE_NUM) {
    page_info->pool_stack[page_info->pool_nr++] = free_id;
    return true;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    page_info->stack_size++;
#if FIXED_LENGTH_INTEGER
    new_heap_size = page_info->stack_size * sizeof(pageid_t);
    pheap_resize(page_info->id_heap, new_heap_size);
    stacktop_addr = ptr_offset(pheap_addr(page_info->id_heap),
      new_heap_size - sizeof(pageid_t));
    *(pageid_t*)stacktop_addr = free_id;
#else  /* FIXED_LENGTH_INTEGER */
    new_heap_size = page_info->stack_size * page_info->page_byte;
    pheap_resize(page_info->id_heap, new_heap_size);
    stacktop_addr = ptr_offset(pheap_addr(page_info->id_heap),
      new_heap_size - page_info->page_byte);
    put_int(stacktop_addr, page_info->page_byte, free_id);
#endif /* FIXED_LENGTH_INTEGER */

    return false;
  }
}

VMF_INLINE pageid_t page_info_pop_freeid(page_info_t* page_info,
    bool* mapping) {
  pageid_t ret_id;
#if ENABLE_HEURISTIC
  if (page_info->pool_nr > 0) {
    *mapping = true;
    return page_info->pool_stack[--page_info->pool_nr];
  } else
#endif /* ENABLE_HEURISTIC */
  if (page_info->stack_size > 0) {
    *mapping = false;
    return pop_stacktop(page_info);
  } else {
    *mapping = false;
    ret_id = page_info->page_num++;
#if FIXED_LENGTH_INTEGER
    pheap_resize(page_info->data_heap,
      page_info->page_num * sizeof(page_data_t));
#else  /* FIXED_LENGTH_INTEGER */
    pheap_resize(page_info->data_heap,
      page_info->page_num * page_info->block_size);
#endif /* FIXED_LENGTH_INTEGER */
    return ret_id;
  }
}

#if FIXED_LENGTH_INTEGER
VMF_INLINE void page_info_replace(page_info_t* page_info,
    pageid_t page_id, pageid_t prev_id, pageid_t next_id,
    offset_t ofs, size_class_t size_class) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  page_data->prev_page  = prev_id;
  page_data->next_page  = next_id;
  page_data->ofs        = ofs;
  page_data->size_class = size_class;
}

VMF_INLINE pageid_t page_info_get_prev(const page_info_t* page_info,
    pageid_t page_id) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  return page_data->prev_page;
}

VMF_INLINE void page_info_put_prev(page_info_t* page_info,
    pageid_t page_id, pageid_t prevpage_id) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  page_data->prev_page = prevpage_id;
}

VMF_INLINE pageid_t page_info_get_next(const page_info_t* page_info,
    pageid_t page_id) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  return page_data->next_page;
}

VMF_INLINE void page_info_put_next(page_info_t* page_info,
    pageid_t page_id, pageid_t nextpage_id) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  page_data->next_page = nextpage_id;
}

VMF_INLINE offset_t page_info_get_offset(const page_info_t* page_info,
    pageid_t page_id) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  return page_data->ofs;
}

VMF_INLINE void page_info_put_offset(page_info_t* page_info,
    pageid_t page_id, offset_t page_offset) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  page_data->ofs = page_offset;
}

VMF_INLINE size_class_t page_info_get_sc(const page_info_t* page_info,
    pageid_t page_id) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  return page_data->size_class;
}

VMF_INLINE void page_info_put_sc(page_info_t* page_info,
    pageid_t page_id, size_class_t size_class) {
  page_data_t* page_data = (page_data_t*) page_info_get_pid(page_info, page_id);
  page_data->size_class = size_class;
}

VMF_INLINE pageid_t page_info_fast_get_prev(const page_info_t* page_info,
    void* block_addr) {
  return ((page_data_t*)block_addr)->prev_page;
}

VMF_INLINE pageid_t page_info_fast_get_next(const page_info_t* page_info,
    void* block_addr) {
  return ((page_data_t*)block_addr)->next_page;
}

VMF_INLINE offset_t page_info_fast_get_offset(const page_info_t* page_info,
    void* block_addr) {
  return ((page_data_t*)block_addr)->ofs;
}

VMF_INLINE size_class_t page_info_fast_get_sc(const page_info_t* page_info,
    void* block_addr) {
  return ((page_data_t*)block_addr)->size_class;
}

#else  /* FIXED_LENGTH_INTEGER */
VMF_INLINE void page_info_replace(page_info_t* page_info,
    pageid_t page_id, pageid_t prev_id, pageid_t next_id,
    offset_t ofs, size_class_t size_class) {
  void* block_addr = page_info_get_pid(page_info, page_id + 1);
  uint8_t* out_u8 = (uint8_t*)block_addr - 1;
  bytenum_t page_byte = page_info->page_byte;
  bytenum_t ofs_byte  = page_info->ofs_byte;
  bytenum_t i;
  assert(page_id < page_info->page_num);

#if ENABLE_HEURISTIC
  if (ofs_byte == 2) {
    *out_u8-- = size_class & 0xff;  size_class >>= 8;
    *out_u8-- = size_class & 0xff;
    *out_u8-- = ofs    & 0xff;  ofs >>= 8;
    *out_u8-- = ofs    & 0xff;
  } else if (ofs_byte == 3) {
    *out_u8-- = size_class & 0xff;  size_class >>= 8;
    *out_u8-- = size_class & 0xff;  size_class >>= 8;
    *out_u8-- = size_class & 0xff;
    *out_u8-- = ofs    & 0xff;  ofs >>= 8;
    *out_u8-- = ofs    & 0xff;  ofs >>= 8;
    *out_u8-- = ofs    & 0xff;
  } else if (ofs_byte == 4) {
    *out_u8-- = size_class & 0xff;  size_class >>= 8;
    *out_u8-- = size_class & 0xff;  size_class >>= 8;
    *out_u8-- = size_class & 0xff;  size_class >>= 8;
    *out_u8-- = size_class & 0xff;
    *out_u8-- = ofs    & 0xff;  ofs >>= 8;
    *out_u8-- = ofs    & 0xff;  ofs >>= 8;
    *out_u8-- = ofs    & 0xff;  ofs >>= 8;
    *out_u8-- = ofs    & 0xff;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    for (i = 0; i < ofs_byte; ++i) {
      *out_u8-- = size_class & 0xff;  size_class >>= 8;
    }
    for (i = 0; i < ofs_byte; ++i) {
      *out_u8-- = ofs    & 0xff;  ofs >>= 8;
    }
  }

#if ENABLE_HEURISTIC
  if (page_byte == 1) {
    *out_u8-- = next_id & 0xff;
    *out_u8   = prev_id & 0xff;
  } else if (page_byte == 2) {
    *out_u8-- = next_id & 0xff;  next_id >>= 8;
    *out_u8-- = next_id & 0xff;
    *out_u8-- = prev_id & 0xff;  prev_id >>= 8;
    *out_u8   = prev_id & 0xff;
  } else if (page_byte == 3) {
    *out_u8-- = next_id & 0xff;  next_id >>= 8;
    *out_u8-- = next_id & 0xff;  next_id >>= 8;
    *out_u8-- = next_id & 0xff;
    *out_u8-- = prev_id & 0xff;  prev_id >>= 8;
    *out_u8-- = prev_id & 0xff;  prev_id >>= 8;
    *out_u8   = prev_id & 0xff;
  } else if (page_byte == 4) {
    *out_u8-- = next_id & 0xff;  next_id >>= 8;
    *out_u8-- = next_id & 0xff;  next_id >>= 8;
    *out_u8-- = next_id & 0xff;  next_id >>= 8;
    *out_u8-- = next_id & 0xff;
    *out_u8-- = prev_id & 0xff;  prev_id >>= 8;
    *out_u8-- = prev_id & 0xff;  prev_id >>= 8;
    *out_u8-- = prev_id & 0xff;  prev_id >>= 8;
    *out_u8   = prev_id & 0xff;
  } else
#endif /* ENABLE_HEURISTIC */
  {
    for (i = 0; i < page_byte; ++i) {
      *out_u8-- = next_id & 0xff;  next_id >>= 8;
    }
    for (i = 0; i < page_byte; ++i) {
      *out_u8-- = prev_id & 0xff;  prev_id >>= 8;
    }
  }
}

VMF_INLINE pageid_t page_info_get_prev(const page_info_t* page_info,
    pageid_t page_id) {
  bytenum_t page_byte = page_info->page_byte;
  assert(page_id < page_info->page_num);
  return get_int(
    PAGE_INFO_PREV_PAGE_OFS(page_info_get_pid(page_info, page_id)),
    page_byte);
}

VMF_INLINE void page_info_put_prev(page_info_t* page_info,
    pageid_t page_id, pageid_t prevpage_id) {
  bytenum_t page_byte = page_info->page_byte;
  assert(page_id < page_info->page_num);
  put_int(
    PAGE_INFO_PREV_PAGE_OFS(page_info_get_pid(page_info, page_id)),
    page_byte,
    prevpage_id);
}

VMF_INLINE pageid_t page_info_get_next(const page_info_t* page_info,
    pageid_t page_id) {
  bytenum_t page_byte = page_info->page_byte;
  assert(page_id < page_info->page_num);
  return get_int(
    PAGE_INFO_NEXT_PAGE_OFS(page_info_get_pid(page_info, page_id), page_byte),
    page_byte);
}

VMF_INLINE void page_info_put_next(page_info_t* page_info,
    pageid_t page_id, pageid_t nextpage_id) {
  bytenum_t page_byte = page_info->page_byte;
  assert(page_id < page_info->page_num);
  put_int(
    PAGE_INFO_NEXT_PAGE_OFS(page_info_get_pid(page_info, page_id), page_byte),
    page_byte,
    nextpage_id);
}

VMF_INLINE offset_t page_info_get_offset(const page_info_t* page_info,
    pageid_t page_id) {
  bytenum_t page_byte = page_info->page_byte;
  bytenum_t ofs_byte  = page_info->ofs_byte;
  assert(page_id < page_info->page_num);
  return get_int(
    PAGE_INFO_OFFSET_OFS(page_info_get_pid(page_info, page_id), page_byte),
    ofs_byte);
}

VMF_INLINE void page_info_put_offset(page_info_t* page_info,
    pageid_t page_id, offset_t page_offset) {
  bytenum_t page_byte = page_info->page_byte;
  bytenum_t ofs_byte  = page_info->ofs_byte;
  assert(page_id < page_info->page_num);
  put_int(
    PAGE_INFO_OFFSET_OFS(page_info_get_pid(page_info, page_id), page_byte),
    ofs_byte,
    page_offset);
}

VMF_INLINE size_class_t page_info_get_sc(const page_info_t* page_info,
    pageid_t page_id) {
  bytenum_t page_byte = page_info->page_byte;
  bytenum_t ofs_byte  = page_info->ofs_byte;
  assert(page_id < page_info->page_num);
  return get_int(
    PAGE_INFO_SC_OFS(page_info_get_pid(page_info, page_id),
      page_byte, ofs_byte),
    ofs_byte);
}

VMF_INLINE void page_info_put_sc(page_info_t* page_info,
    pageid_t page_id, size_class_t size_class) {
  bytenum_t page_byte = page_info->page_byte;
  bytenum_t ofs_byte  = page_info->ofs_byte;
  assert(page_id < page_info->page_num);
  put_int(
    PAGE_INFO_SC_OFS(page_info_get_pid(page_info, page_id),
      page_byte, ofs_byte),
    ofs_byte,
    size_class);
}


VMF_INLINE pageid_t page_info_fast_get_prev(const page_info_t* page_info,
    void* block_addr) {
  bytenum_t page_byte = page_info->page_byte;
  return get_int(PAGE_INFO_PREV_PAGE_OFS(block_addr), page_byte);
}

VMF_INLINE pageid_t page_info_fast_get_next(const page_info_t* page_info,
    void* block_addr) {
  bytenum_t page_byte = page_info->page_byte;
  return get_int(PAGE_INFO_NEXT_PAGE_OFS(block_addr, page_byte), page_byte);
}

VMF_INLINE offset_t page_info_fast_get_offset(const page_info_t* page_info,
    void* block_addr) {
  bytenum_t page_byte = page_info->page_byte;
  bytenum_t ofs_byte  = page_info->ofs_byte;
  return get_int(PAGE_INFO_OFFSET_OFS(block_addr, page_byte), ofs_byte);
}

VMF_INLINE size_class_t page_info_fast_get_sc(const page_info_t* page_info,
    void* block_addr) {
  bytenum_t page_byte = page_info->page_byte;
  bytenum_t ofs_byte  = page_info->ofs_byte;
  return get_int(PAGE_INFO_SC_OFS(block_addr, page_byte, ofs_byte), ofs_byte);
}
#endif /* FIXED_LENGTH_INTEGER */

VMF_INLINE size_t get_size_page_info(const page_info_t* page_info) {
  return sizeof(*page_info)
    + pheap_get_size(page_info->data_heap)
    + pheap_get_size(page_info->id_heap);
}

/* ========================================================================== */
/* main structure */
/* ========================================================================== */

#if !FIXED_LENGTH_INTEGER
/** Required number of bytes to represent integer in [0, num) */
VMF_INLINE bytenum_t required_byte(uint64_t num);
#endif /* !FIXED_LENGTH_INTEGER */
/** The head page of size_class */
VMF_INLINE void* get_page_head_address(vmf_main_t* vmf_main,
    size_class_t size_class);
/** Insert new page to the head */
VMF_INLINE pageid_t insert_page(
    vmf_main_t* vmf_main,
    void* head_addr, pageid_t old_head_id,
    offset_t page_offset, size_t size_class);
/** Delete head page */
VMF_INLINE void remove_page(vmf_main_t* vmf_main,
    pageid_t removepage_id, void* headpage_addr, void* headpage_block);
/** Calculate the address at the specified offset */
VMF_INLINE void* get_data_address(vmf_main_t* vmf_main,
    pageid_t page_id, offset_t ofs);
/** Read block ID at the specified offset */
VMF_INLINE blockid_t get_datahead_id(vmf_main_t* vmf_main,
    pageid_t page_id, offset_t ofs);
/** Write block ID at the specified offset */
VMF_INLINE void put_datahead_id(
    vmf_main_t* vmf_main, pageid_t page_id,
    offset_t ofs, blockid_t bid);
/** Check whether the block 'bid' is allocated or not */
VMF_INLINE bool vmf_is_null(vmf_main_t* vmf_main, blockid_t bid);

vmf_t vmf_init(size_t mem_min, size_t mem_max,
    size_t block_nr_max, size_t total_sup) {
  unsigned range_length;
  size_t spell_size;
  vmf_main_t* vmf_main;
#if !FIXED_LENGTH_INTEGER
  bytenum_t blockid_byte = required_byte(block_nr_max + 1);
  bytenum_t page_byte = required_byte(
    (blockid_byte * block_nr_max + total_sup + (PAGE_SIZE - 1)) / PAGE_SIZE);
  bytenum_t ofs_byte;
  page_byte = VMF_MAX(page_byte, blockid_byte);
#endif /* !FIXED_LENGTH_INTEGER */

  size_manager_init();
  assert(mem_min <= mem_max);
  mem_min = size2sc(mem_min);
  mem_max = size2sc(mem_max);
  vmf_main = (vmf_main_t*) safe_malloc(sizeof(vmf_main_t));

  vmf_main->mem_min        = mem_min;
  vmf_main->mem_max        = mem_max;
  vmf_main->block_nr_max   = block_nr_max;

  range_length = mem_max - mem_min + 1;
#if FIXED_LENGTH_INTEGER
  vmf_main->page_heads = (void*) safe_malloc(sizeof(pageid_t) * range_length);
  memset(vmf_main->page_heads, 0xff, sizeof(pageid_t) * range_length);
#else  /* FIXED_LENGTH_INTEGER */
  vmf_main->blockid_byte = blockid_byte;
  vmf_main->page_byte    = page_byte;
  vmf_main->null_block   = get_allone_value(blockid_byte);
  vmf_main->null_page    = get_allone_value(page_byte);
  vmf_main->page_heads   = (void*) safe_malloc(page_byte * range_length);
  /* Initialize head pages to null page */
  memset(vmf_main->page_heads, 0xff, page_byte * range_length);
#endif /* FIXED_LENGTH_INTEGER */

  vmf_main->module  = module_init(sc2size(mem_max) + sizeof(blockid_t), total_sup);

  /* block_info cannot be initialized unless ofs_byte is determined */
  vmf_main->physical_pagesize = module_get_pagesize(vmf_main->module);
#if FIXED_LENGTH_INTEGER
  vmf_main->block_info = block_info_init(block_nr_max);
  vmf_main->page_info  = page_info_init();
#else  /* FIXED_LENGTH_INTEGER */
  ofs_byte = required_byte(vmf_main->physical_pagesize);
  vmf_main->ofs_byte = ofs_byte;
  vmf_main->block_info = block_info_init(ofs_byte, page_byte, block_nr_max);
  vmf_main->page_info = page_info_init(page_byte, ofs_byte);
#endif /* FIXED_LENGTH_INTEGER */

#ifdef ENABLE_HEURISTIC
  if (vmf_main->block_nr_max > 1) {
    /* The first memcpy is very time consuming, so input
       is given such that memcpy occurs. */
    spell_size = sc2size(vmf_main->mem_max);
    vmf_allocate(vmf_main, 0, spell_size);
    vmf_allocate(vmf_main, 1, spell_size);
    vmf_deallocate(vmf_main, 0);
    vmf_deallocate(vmf_main, 1);
  }
#endif /* ENABLE_HEURISTIC */

  return (vmf_t) vmf_main;
}

void vmf_final(vmf_t vmf) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;

  free(vmf_main->page_heads);
  block_info_final(vmf_main->block_info);
  page_info_final(vmf_main->page_info);
  module_final(vmf_main->module);
}

void vmf_allocate(vmf_t vmf, blockid_t bid, size_t length) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;
  void* head_addr;
  pageid_t page_id;
  offset_t page_offset;
  size_t size_class = size2sc(length);
#if FIXED_LENGTH_INTEGER
  size_t real_size = sc2size(size_class) + sizeof(blockid_t);
#else /* FIXED_LENGTH_INTEGER */
  size_t real_size = sc2size(size_class) + vmf_main->blockid_byte;
#endif /* FIXED_LENGTH_INTEGER */

  head_addr = get_page_head_address(vmf_main, size_class);
#if FIXED_LENGTH_INTEGER
  page_id = *(pageid_t*)head_addr;
#else /* FIXED_LENGTH_INTEGER */
  page_id = get_int(head_addr, vmf_main->page_byte);
#endif /* FIXED_LENGTH_INTEGER */

#if FIXED_LENGTH_INTEGER
  if (page_id == (pageid_t)(-1))
#else /* FIXED_LENGTH_INTEGER */
  if (page_id == vmf_main->null_page)
#endif /* FIXED_LENGTH_INTEGER */
  {
    /* If head page is null page, new page should be inserted there */
    page_offset = vmf_main->physical_pagesize - real_size;
    page_id = insert_page(vmf_main, head_addr,
      page_id, page_offset, size_class);
  } else {
    page_offset = page_info_get_offset(vmf_main->page_info, page_id);
    if (page_offset >= real_size) {
      page_offset -= real_size;
      page_info_put_offset(vmf_main->page_info, page_id, page_offset);
    } else {
      page_offset = page_offset + vmf_main->physical_pagesize - real_size;
      page_id = insert_page(vmf_main, head_addr, page_id,
          page_offset, size_class);
    }
  }

  block_info_push(vmf_main->block_info, bid, page_offset, page_id);
  put_datahead_id(vmf_main, page_id, page_offset, bid);
}

void vmf_deallocate(vmf_t vmf, blockid_t bid) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;
  void* headpage_addr;
  void* headpage_block_addr;
  void* dst_block_addr;
  void* block_data_addr;
  void* head_block_data_addr;
  void* headpage_block;
  size_class_t block_sc;
  offset_t block_ofs;
  pageid_t page_id;
  pageid_t headpage_id;
  offset_t headpage_ofs;
  blockid_t headbid;
  size_class_t real_length;

  block_data_addr = block_info_get_all(vmf_main->block_info, bid,
      &block_ofs, &page_id);
  /* This assert takes much time */
  assert(get_datahead_id(vmf_main, page_id, block_ofs) == bid);
  dst_block_addr = get_data_address(vmf_main, page_id, block_ofs);
  block_sc = page_info_get_sc(vmf_main->page_info, page_id);
  headpage_addr = get_page_head_address(vmf_main, block_sc);
#if FIXED_LENGTH_INTEGER
  headpage_id = *(pageid_t*) headpage_addr;
#else /* FIXED_LENGTH_INTEGER */
  headpage_id = get_int(headpage_addr, vmf_main->page_byte);
#endif /* FIXED_LENGTH_INTEGER */
  headpage_block = page_info_get_pid(vmf_main->page_info, headpage_id);
  headpage_ofs = page_info_fast_get_offset(vmf_main->page_info, headpage_block);
  headpage_block_addr = get_data_address(vmf_main, headpage_id, headpage_ofs);
#if FIXED_LENGTH_INTEGER
  real_length = sc2size(block_sc) + sizeof(blockid_t);
#else /* FIXED_LENGTH_INTEGER */
  real_length = sc2size(block_sc) + vmf_main->blockid_byte;
#endif /* FIXED_LENGTH_INTEGER */

#if FIXED_LENGTH_INTEGER
  assert(headpage_id != (pageid_t)(-1));
#else /* FIXED_LENGTH_INTEGER */
  assert(headpage_id != vmf_main->null_page);
#endif /* FIXED_LENGTH_INTEGER */
  if (dst_block_addr != headpage_block_addr) {
#if FIXED_LENGTH_INTEGER
    headbid = *(pageid_t*)headpage_block_addr;
#else /* FIXED_LENGTH_INTEGER */
    headbid = get_int(headpage_block_addr, vmf_main->blockid_byte);
#endif /* FIXED_LENGTH_INTEGER */
    head_block_data_addr =
      block_info_get_block_ptr(vmf_main->block_info, headbid);

#if COPYLESS
#if FIXED_LENGTH_INTEGER
    *(blockid_t*)dst_block_addr = *(blockid_t*)headpage_block_addr;
#else /* FIXED_LENGTH_INTEGER */
    my_memcpy(dst_block_addr, headpage_block_addr, vmf_main->blockid_byte);
#endif /* FIXED_LENGTH_INTEGER */
#else  /* COPYLESS */
    my_memcpy(dst_block_addr, headpage_block_addr, real_length);
#endif
    my_memcpy(head_block_data_addr, block_data_addr,
        block_info_block_size(vmf_main->block_info));
  }

  /* Memorize the block ID 'bid' is no longer used */
  block_info_fastput_null_page(vmf_main->block_info, block_data_addr);
  if (headpage_ofs + real_length >= vmf_main->physical_pagesize) {
    remove_page(vmf_main, headpage_id, headpage_addr, headpage_block);
  } else {
    page_info_put_offset(vmf_main->page_info, headpage_id,
      headpage_ofs + real_length);
  }
}


void vmf_reallocate(vmf_t vmf, blockid_t bid, size_t size) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;
  pageid_t page_id;
  size_class_t block_sc;
  size_class_t copy_size;
  void* buffer;

  if (size == 0) {
    vmf_deallocate(vmf_main, bid);
  } else if (
    (page_id = block_info_get_pid(vmf_main->block_info, bid))
      == (pageid_t)(-1)) {
    vmf_allocate(vmf_main, bid, size);
  } else {
    size = sc2size(size2sc(size));
    block_sc = sc2size(page_info_get_sc(vmf_main->page_info, page_id));
    if (size == block_sc) return;

    copy_size = VMF_MIN(size, block_sc);
    buffer = malloc(copy_size);
    my_memcpy(buffer, vmf_dereference(vmf_main, bid), copy_size);
    vmf_deallocate(vmf_main, bid);
    vmf_allocate(vmf_main, bid, size);
    my_memcpy(vmf_dereference(vmf_main, bid), buffer, copy_size);
    free(buffer);
  }
}

void* vmf_dereference(vmf_t vmf, blockid_t bid) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;
  offset_t ofs;
  pageid_t    page_id;

  if (vmf_is_null(vmf_main, bid)) return NULL;
  block_info_get_all(vmf_main->block_info, bid, &ofs, &page_id);
#if FIXED_LENGTH_INTEGER
  return ptr_offset(get_data_address(vmf_main, page_id, ofs),
    sizeof(blockid_t));
#else /* FIXED_LENGTH_INTEGER */
  return ptr_offset(get_data_address(vmf_main, page_id, ofs),
    vmf_main->blockid_byte);
#endif /* FIXED_LENGTH_INTEGER */
}

const void* vmf_dereference_c(const vmf_t vmf, blockid_t bid) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;
  offset_t ofs;
  pageid_t page_id;

  if (vmf_is_null(vmf_main, bid)) return NULL;
  block_info_get_all(vmf_main->block_info, bid, &ofs, &page_id);
#if FIXED_LENGTH_INTEGER
  return ptr_offset(get_data_address(vmf_main, page_id, ofs),
    sizeof(blockid_t));
#else /* FIXED_LENGTH_INTEGER */
  return ptr_offset(get_data_address(vmf_main, page_id, ofs),
    vmf_main->blockid_byte);
#endif /* FIXED_LENGTH_INTEGER */
}

size_t vmf_length(vmf_t vmf, blockid_t bid) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;
  pageid_t page_id;

  page_id = block_info_get_pid(vmf_main->block_info, bid);
  return sc2size(page_info_get_sc(vmf_main->page_info, page_id));
}

size_t vmf_using_mem(vmf_t vmf) {
  vmf_main_t* vmf_main = (vmf_main_t*) vmf;
  return sizeof(vmf_main_t)
#if FIXED_LENGTH_INTEGER
    + sizeof(pageid_t) * (vmf_main->mem_max - vmf_main->mem_min + 1)
#else /* FIXED_LENGTH_INTEGER */
    + vmf_main->page_byte * (vmf_main->mem_max - vmf_main->mem_min + 1)
#endif /* FIXED_LENGTH_INTEGER */
    + block_info_get_size(vmf_main->block_info)
    + get_size_page_info(vmf_main->page_info)
    + module_get_size(vmf_main->module);
}

#if !FIXED_LENGTH_INTEGER
VMF_INLINE bytenum_t required_byte(uint64_t num) {
  uint64_t bit_size;
  uint64_t byte_size;

  if (num > 1) {
#ifdef __GNUC__
    bit_size  = sizeof(uint64_t) * ONE_BYTE - __builtin_clzll(num - 1);
    byte_size = (bit_size + (ONE_BYTE - 1)) / ONE_BYTE;
#else  /* __GNU_C__ */
    byte_size = 1;
    --num;
    while (num > 0) {
      ++byte_size;
      num >>= 8;
    }
#endif /* __GNU_C__ */
  } else {
    byte_size = 1;
  }
  return (bytenum_t) byte_size;
}
#endif /* !FIXED_LENGTH_INTEGER */

VMF_INLINE void* get_page_head_address(vmf_main_t* vmf_main,
    size_class_t size_class) {
#if FIXED_LENGTH_INTEGER
  ptrdiff_t offset = (size_class - vmf_main->mem_min) * sizeof(pageid_t);
#else /* FIXED_LENGTH_INTEGER */
  ptrdiff_t offset = (size_class - vmf_main->mem_min) * vmf_main->page_byte;
#endif /* FIXED_LENGTH_INTEGER */
  return ptr_offset(vmf_main->page_heads, offset);
}

VMF_INLINE pageid_t insert_page(vmf_main_t* vmf_main,
    void* head_addr, pageid_t old_head_id,
    offset_t page_offset, size_t size_class) {
  pageid_t new_head_id;
  bool mapping;
  void* page_head = head_addr;

  new_head_id = page_info_pop_freeid(vmf_main->page_info, &mapping);
  if (!mapping) {
    module_allocate(vmf_main->module, new_head_id);
  }

  page_info_replace(vmf_main->page_info, new_head_id,
#if FIXED_LENGTH_INTEGER
    (pageid_t)(-1),
#else /* FIXED_LENGTH_INTEGER */
    vmf_main->null_page,
#endif /* FIXED_LENGTH_INTEGER */
    old_head_id, page_offset, size_class);

#if FIXED_LENGTH_INTEGER
  *(pageid_t*)page_head = new_head_id;
#else /* FIXED_LENGTH_INTEGER */
  put_int(page_head, vmf_main->page_byte, new_head_id);
#endif /* FIXED_LENGTH_INTEGER */

#if FIXED_LENGTH_INTEGER
  if (old_head_id != (pageid_t)(-1))
#else /* FIXED_LENGTH_INTEGER */
  if (old_head_id != vmf_main->null_page)
#endif /* FIXED_LENGTH_INTEGER */
  {
    module_set_next(vmf_main->module, new_head_id, old_head_id);
    page_info_put_prev(vmf_main->page_info, old_head_id, new_head_id);
  }

  return new_head_id;
}

VMF_INLINE void remove_page(vmf_main_t* vmf_main,
    pageid_t removepage_id, void* headpage_addr, void* headpage_block) {
  void* removepage_block = headpage_block;
  pageid_t    removenext_id;

  removenext_id =
    page_info_fast_get_next(vmf_main->page_info, removepage_block);

#if FIXED_LENGTH_INTEGER
  if (removenext_id != (pageid_t)(-1)) {
    page_info_put_prev(vmf_main->page_info, removenext_id, (pageid_t)(-1));
  }
  *(pageid_t*)headpage_addr = removenext_id;
  if (removenext_id != (pageid_t)(-1)) {
    module_reset_next(vmf_main->module, removepage_id);
  }
#else /* FIXED_LENGTH_INTEGER */
  if (removenext_id != vmf_main->null_page) {
    page_info_put_prev(vmf_main->page_info, removenext_id, vmf_main->null_page);
  }
  put_int(headpage_addr, vmf_main->page_byte, removenext_id);
  if (removenext_id != vmf_main->null_page) {
    module_reset_next(vmf_main->module, removepage_id);
  }
#endif /* FIXED_LENGTH_INTEGER */

  if (!page_info_push_freeid(vmf_main->page_info, removepage_id)) {
    module_deallocate(vmf_main->module, removepage_id);
  }
}

VMF_INLINE void* get_data_address(
    vmf_main_t* vmf_main, pageid_t page_id, offset_t ofs) {
  return ptr_offset(module_get_address(vmf_main->module, page_id), ofs);
}

VMF_INLINE blockid_t get_datahead_id(
    vmf_main_t* vmf_main, pageid_t page_id, offset_t ofs) {
#if FIXED_LENGTH_INTEGER
  return *(blockid_t*)get_data_address(vmf_main, page_id, ofs);
#else /* FIXED_LENGTH_INTEGER */
  return get_int(
    get_data_address(vmf_main, page_id, ofs),
    vmf_main->blockid_byte);
#endif /* FIXED_LENGTH_INTEGER */
}

VMF_INLINE void put_datahead_id(
    vmf_main_t* vmf_main, pageid_t page_id,
    offset_t ofs, blockid_t bid) {
#if FIXED_LENGTH_INTEGER
  *(blockid_t*)get_data_address(vmf_main, page_id, ofs) = bid;
#else /* FIXED_LENGTH_INTEGER */
  put_int(
    get_data_address(vmf_main, page_id, ofs),
    vmf_main->blockid_byte,
    bid);
#endif /* FIXED_LENGTH_INTEGER */
}

VMF_INLINE bool vmf_is_null(vmf_main_t* vmf_main, blockid_t bid) {
#if FIXED_LENGTH_INTEGER
  return bid == (blockid_t)(-1);
#else /* FIXED_LENGTH_INTEGER */
  return bid == vmf_main->null_block;
#endif /* FIXED_LENGTH_INTEGER */
}
