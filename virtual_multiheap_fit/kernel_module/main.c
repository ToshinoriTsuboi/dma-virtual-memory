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
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>   /* dev_t */
#include <linux/kdev_t.h>  /* MAJOR/MINOR/MKDEV */
#include <linux/cdev.h>
#include <linux/fs.h>      /* chrdev */
#include <linux/errno.h>
#include <asm/uaccess.h>   /* access_ok */
#include <linux/mm.h>      /* vm_operations */

#include "ioctl.h"
#include "address_vector.h"
#include "page_manage.h"

/* Debug flag */
#ifndef ENABLE_DEBUG
#  define ENABLE_DEBUG 0
#endif

#if ENABLE_DEBUG
/* write out the log file */
#  define PRINT_ENTERING() printk(KERN_ALERT "Entering %s\n", __FUNCTION__)
#else  /* ENABLE_DEBUG */
#  define PRINT_ENTERING()
#endif /* ENABLE_DEBUG */

/* name to register on /proc/devices */
#define DEVICE_NAME  "vmf_module"
/* Module information */
MODULE_LICENSE("MIT");
MODULE_AUTHOR("Toshinori Tsuboi");

/* declare and register 'init' and 'exit' */
__init int  page_allocator_init(void);
__exit void page_allocator_exit(void);
module_init(page_allocator_init);
module_exit(page_allocator_exit);

/* method used for cdev's operation */
static int cdev_open(struct inode* inode_ptr, struct file* file_ptr);
static int cdev_release(struct inode* inode_ptr, struct file* file_ptr);
static long cdev_unlocked_ioctl(struct file* file_ptr,
  unsigned int cmd, unsigned long arg);
static int cdev_mmap(struct file* file_ptr, struct vm_area_struct* vma_ptr);

/* determine whether the command(cmd) is legal (as a argument of 'ioctl').
  If the command is legal, it returns 0 and if not, it returns non-0 value. */
static int cmmand_varify(unsigned int cmd, void __user* arg_ptr);

/* declare global variables */
/* device numver */
dev_t g_dev;
/* structure storing information on the character device */
struct cdev g_cdev;
/* operator functions to manipulate the character device */
struct file_operations g_fops = {
  .owner   = THIS_MODULE,
  .open    = cdev_open,
  .release = cdev_release,
  .mmap    = cdev_mmap,
  .unlocked_ioctl = cdev_unlocked_ioctl,
};

/* The rough flow of processing is as follows
     1. register the device
     2. initialize cdev structure
     3. register cdev structure
   If failed, rewinde process and return.
 */
__init int page_allocator_init(void) {
  int err_code;

  PRINT_ENTERING();
  /* 1. register the device */
  err_code = alloc_chrdev_region(&g_dev, 0, 1, DEVICE_NAME);
  if (err_code < 0) {
    printk(KERN_ALERT "%s: register chrdev failed(0x%x)\n",
      __FUNCTION__, err_code);
    return err_code;
  }
  /* 2. initialize cdev structure */
  cdev_init(&g_cdev, &g_fops);
  g_cdev.owner = THIS_MODULE;
  g_cdev.ops   = &g_fops;

  /* 3. register cdev structure */
  err_code = cdev_add(&g_cdev, g_dev, 1);
  if (err_code < 0) {
    printk(KERN_ALERT "%s: cdev_add failed(0x%x)\n",
      __FUNCTION__, err_code);
    unregister_chrdev_region(g_dev, 1);
    return err_code;
  }

  printk(KERN_ALERT "init success\n");
  return 0;
}

__exit void page_allocator_exit(void) {
  PRINT_ENTERING();

  cdev_del(&g_cdev);
  unregister_chrdev_region(g_dev, 1);
}

static int cdev_open(struct inode* inode_ptr, struct file* file_ptr) {
  address_vector_t* vector_ptr;

  PRINT_ENTERING();
  vector_ptr = alloc_addr_vec(0);
  if (vector_ptr == NULL) return -ENOMEM;

  /* store 'vector_ptr' in 'private_data' to respond to
    'mmap' and 'ioctl' requests. */
  file_ptr->private_data = (void*) vector_ptr;
  return 0;
}

static int cdev_release(struct inode* inode_ptr, struct file* file_ptr) {
  address_vector_t* vector_ptr;

  PRINT_ENTERING();
  vector_ptr = (address_vector_t*) file_ptr->private_data;

  freeall_page(vector_ptr);
  free_addr_vec(vector_ptr);
  return 0;
}

static long cdev_unlocked_ioctl(struct file* file_ptr,
    unsigned int cmd, unsigned long arg) {
  int err_code = 0;
  address_vector_t* vector_ptr;
  unsigned long long index;
  unsigned long page_size;
  unsigned pagesize_order;

  /* 'ioctl' is frequently called so that it is not written to the log. */
  /* PRINT_ENTERING(); */

  /* judge whether the command passed by the user could be executed. */
  err_code = cmmand_varify(cmd, (void __user*)arg);
  if (err_code < 0) {
    printk(KERN_ALERT "%s: illegal ioctl command\n", __FUNCTION__);
    return err_code;
  }

  vector_ptr = (address_vector_t*) file_ptr->private_data;
  if (vector_ptr == NULL) return -ENODATA;

  switch (cmd) {
  case ALLOCATOR_IOC_ALLOC:
    err_code = __get_user(index, (unsigned long __user*)arg);
    if (err_code >= 0) {
      allocate_page(vector_ptr, index);
    }
    break;

  case ALLOCATOR_IOC_DEALLOC:
    err_code = __get_user(index, (unsigned long __user*)arg);
    if (err_code >= 0) {
      deallocate_page(vector_ptr, index);
    }
    break;

  case ALLOCATOR_IOC_RESIZE:
    err_code = __get_user(page_size, (unsigned long __user*)arg);
    if (err_code >= 0) {
      err_code = resize_addr_vec(vector_ptr, page_size);
    }
    break;

  case ALLOCATOR_IOC_TOTAL_SIZE:
    err_code = __put_user(get_size_addr_vec(vector_ptr),
      (unsigned long __user*)arg);
    break;

  case ALLOCATOR_IOC_SET_PAGESIZE_ORDER:
    err_code = __get_user(pagesize_order, (unsigned __user*) arg);
    if (err_code >= 0) {
      err_code = set_pagesize_order(vector_ptr, pagesize_order);
    }
    break;
  }

  return err_code;
}

static int cdev_mmap(struct file* file_ptr, struct vm_area_struct* vma_ptr) {
  /* The offset value of the file matches the page number */
  unsigned long page_id;
  address_vector_t* vector_ptr
    = (address_vector_t*) file_ptr->private_data;
  void* page_addr;
  unsigned long vm_start = vma_ptr->vm_start;
  unsigned long vm_end   = vma_ptr->vm_end;
  unsigned long pseudo_pagesize;
  int err_code;

  if (vector_ptr == NULL) {
    printk(KERN_ALERT "%s: vector_ptr is nullptr\n", __FUNCTION__);
    return -ENODATA;
  }
  pseudo_pagesize = get_pseudo_pagesize(vector_ptr);
  page_id = vma_ptr->vm_pgoff >> vector_ptr->pagesize_order;

  while (vm_start < vm_end) {
    page_addr = get_addr_vec(vector_ptr, page_id);
    if (page_addr == NULL) {
      printk(KERN_ALERT "%s: page not found(page %ld)\n",
          __FUNCTION__, page_id);
      return -ENODATA;
    }

    err_code = remap_pfn_range(vma_ptr, vm_start,
      (unsigned long)virt_to_phys(page_addr) >> PAGE_SHIFT,
      pseudo_pagesize, vma_ptr->vm_page_prot);
    if (err_code < 0) {
      printk(KERN_ALERT "%s: remap_pfn_range failed\n", __FUNCTION__);
      return -EAGAIN;
    }

    ++page_id;
    vm_start += pseudo_pagesize;
  }

  return 0;
}

static int cmmand_varify(unsigned int cmd, void __user* arg_ptr) {
  if (_IOC_TYPE(cmd) != ALLOCATOR_IOC_MAGIC) return -ENOTTY;
  if (_IOC_NR(cmd) >= ALLOCATOR_IOC_MAXNR)    return -ENOTTY;

  /* judge whether the address is readable / writable */
  if ((_IOC_DIR(cmd) & _IOC_READ)
      && !access_ok(VERIFY_WRITE, arg_ptr, _IOC_SIZE(cmd))) {
    return -EFAULT;
  }

  if ((_IOC_DIR(cmd) & _IOC_READ)
      && !access_ok(VERIFY_READ, arg_ptr, _IOC_SIZE(cmd))) {
    return -EFAULT;
  }
  return 0;
}
