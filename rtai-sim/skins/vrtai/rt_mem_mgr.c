//////////////////////////////////////////////////////////////////////////////
//
//      Copyright (©) 2000 Pierre Cloutier (Poseidon Controls Inc.),
//                         Steve Papacharalambous (Lineo Inc.),
//                         All rights reserved
//
// Authors:             Pierre Cloutier (pcloutier@poseidoncontrols.com)
//                      Steve Papacharalambous (stevep@lineo.com)
//
// Original date:       Mon 14 Feb 2000
//
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
//
// Dynamic Memory Management for Real Time Linux.
//
///////////////////////////////////////////////////////////////////////////////
static char id_rt_mem_mgr_c[] __attribute__ ((unused)) = "@(#)$Id: rt_mem_mgr.c,v 1.1 2004/06/06 14:15:56 rpm Exp $";

#ifdef __MVM__

#include "vrtai/rtai.h"
#include "vrtai/rt_mem_mgr.h"

#else  /* !__MVM__ */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#ifndef CONFIG_RTAI_MM_VMALLOC
  #include <linux/slab.h>
#else
  #include <linux/vmalloc.h>
  #include "../shmem/kvmem.h"
#endif
#include <linux/errno.h>

#include <linux/smp_lock.h>
#include <linux/interrupt.h>

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#endif

#include <rtai.h>
#include <rtai_proc_fs.h>
#include <rtai_fifos.h>
#include <rt_mem_mgr.h>

#endif /* __MVM__ */

MODULE_LICENSE("GPL");

// ------------------------------< definitions >-------------------------------
#ifndef NULL
#define NULL ((void *) 0)
#endif

#define OVERHEAD (sizeof(struct chkhdr) + sizeof(struct blkhdr))
#define MINSIZE 16					// Need 16 bytes alignment for PIII floating point support.

#ifdef DEBUG
#define DPRINTK(format,args...) printk(format,## args)
#else
#define DPRINTK(format,args...)
#endif

// These defines set whether kmalloc() or vmalloc() is used
// to allocate the memory chunks used by the rt memory manager.
//
// Reasons for using vmalloc:
// - Simpler to share allocated buffers with user space.
// - Doesn't have the size restriction of kmalloc.
//
// Reasons for using kmalloc:
// - Faster
// - Contiguous buffer address, needed for DMA controllers which don't
//   have scatter/gather capability.
// The default is vmalloc()
//

#if defined(CONFIG_RTAI_MM_VMALLOC)
  #define alloc_chunk(size) vmalloc((size))
  #define free_chunk(addr) vfree((addr))
#else
  #define alloc_chunk(size) kmalloc((size), GFP_KERNEL)
  #define free_chunk(addr) kfree((addr))
#endif

// Chunk of contiguous memory.
struct chkhdr {
  void *chk_addr;                    // Address of chunk.
  struct chkhdr *next_chk;           // 0 if none. 
  unsigned int chk_limit;            // Size of chunk.
  struct blkhdr *free_list;          // 1st free object in chunk, NULL if none.
  char data[0];
};


// Block header for management within a chunk.
struct blkhdr {
  void *chk_addr;                    // Address of owner chunk.
  struct blkhdr *next_free;          // SELF if not free, NULL if end of list.
  unsigned int free_size;
  int align16;						 // Unused variable for 16 bytes alignment.	  
  char data[0];
};

enum mem_cmd { chk_alloc, chk_free };


// ----------------------------------------------------------------------------
//      Local Definitions.
// ----------------------------------------------------------------------------
static void *alloc_in_chk(unsigned int size, struct chkhdr *chk);
void rt_mmgr_dump(void);


// ----------------------------------------------------------------------------
//      Package Global Data.
// ----------------------------------------------------------------------------
char rtmmgr_version[] = "1.3";
unsigned int granularity    = 0x10000;
int low_chk_ref 			= 4;
unsigned int low_data_mark  = 0x4000; // Enough to allocate a dynamic task.
int extra_chks;
unsigned int max_allocated = 0;
unsigned int total_available = 0;
unsigned int sys_malloc_calls = 0;
unsigned int sys_free_calls = 0;
unsigned int memory_leak = 0;
unsigned int memory_overhead = 0;
int sysrq_cmd_err = 0;
int rt_mem_mgr_init = 0;
struct chkhdr *chk_list = NULL;
struct { int srq, no_chk; spinlock_t srq_lock; } alloc_sysrq;
struct { int srq, no_chk; spinlock_t srq_lock; } free_sysrq;
spinlock_t rt_mem_lock;


// ----------------------------------------------------------------------------

// -----------------------< Memory management section >------------------------
void *rt_malloc(unsigned int size)
{

  unsigned long flags;
  void *mem_ptr = NULL;
  struct chkhdr *chk_list_ptr;
  char *func __attribute__ ((unused)) = "rt_malloc";

  size = (size + (MINSIZE - 1)) & ~(MINSIZE - 1);
  if(size > granularity - OVERHEAD) {
    DBG("%s - Cannot allocate %d bytes, too large.\n", func, size);
    DBG("\n");
    return(NULL);
  }

  chk_list_ptr = chk_list;
  flags = rt_spin_lock_irqsave(&rt_mem_lock);
  for(chk_list_ptr = chk_list; chk_list_ptr != NULL;
                     chk_list_ptr = chk_list_ptr->next_chk) {

    if(chk_list_ptr->free_list != NULL) {

      if((mem_ptr = alloc_in_chk(size, chk_list_ptr)) != NULL) {
        break;
      }  // End if - Allocated a block okay.

      DBG("%s - Insufficient free memory in chunk %p, trying %p\n",
                               func, chk_list_ptr, chk_list_ptr->next_chk);
    }  // End if - Chunk is not full

  }  // End for loop - search all chunks

  if((total_available - (memory_leak + memory_overhead)) <= low_data_mark) {
    if(alloc_sysrq.no_chk == 0) {
      alloc_sysrq.no_chk++;
      DBG("%s - Requesting an extra chunk.\n", func);
      rt_pend_linux_srq(alloc_sysrq.srq);
    }  // End if - No other allocation sys requests pending.
  }  // End if - Request another chunk if < low water level.

  rt_spin_unlock_irqrestore(flags, &rt_mem_lock);
  return(mem_ptr);

}  // End function - rt_malloc


void rt_free(void *addr)
{

  unsigned int flags;
  struct chkhdr *chk_ptr;
  struct blkhdr *blk_ptr, *cur_blk_ptr, *prev_blk_ptr;
  char *func __attribute__ ((unused)) = "rt_free";

// Get the block and chunk headers for the block.
  blk_ptr = (struct blkhdr *)(addr - sizeof(struct blkhdr));
  chk_ptr = blk_ptr->chk_addr;

// Make sure that it is valid block.
  if(blk_ptr->chk_addr != chk_ptr->chk_addr ||
     blk_ptr->next_free != blk_ptr ||
     ((unsigned)(blk_ptr->free_size) == 0) ||
     ((unsigned)(blk_ptr->free_size) > (granularity - OVERHEAD)) ||
     (unsigned)(blk_ptr->free_size) % MINSIZE != 0) {

    DBG("%s - Attempt to free corrupt heap at 0x%p.\n", func, addr);
  } else {

    DBG("%s - Request to free block at 0x%p from chunk 0x%p.\n",
                                                       func, addr, chk_ptr);
    DBG("%s - 1st free block in chunk is 0x%p\n", func, chk_ptr->free_list);
    flags = rt_spin_lock_irqsave(&rt_mem_lock);
    cur_blk_ptr = chk_ptr->free_list;
    prev_blk_ptr = NULL;

// Find a previous free block to link into the free list.
    while(cur_blk_ptr != NULL && cur_blk_ptr->data < (char *)addr) {
      prev_blk_ptr = cur_blk_ptr;
      cur_blk_ptr = cur_blk_ptr->next_free;
    }  // End while loop - find previous free block
    DBG("%s - Exit from while loop - cur_blk_ptr 0x%p prev_blk_ptr 0x%p\n",
                                             func, cur_blk_ptr, prev_blk_ptr);
    memory_leak -= blk_ptr->free_size;

// If the previous free block is contiguous merge them.
    if((prev_blk_ptr != NULL) && ((struct blkhdr *)(prev_blk_ptr->data +
                                      prev_blk_ptr->free_size) == blk_ptr)) {

      prev_blk_ptr->free_size += blk_ptr->free_size + sizeof(struct blkhdr);
      blk_ptr = prev_blk_ptr;
      memory_overhead -= sizeof(struct blkhdr);
      DBG("%s - Merging free block with previous block at 0x%p.\n",
                                                          func, prev_blk_ptr);

    // Or, insert a new free block.
    } else if(prev_blk_ptr != NULL) {
      blk_ptr->next_free = prev_blk_ptr->next_free;
      prev_blk_ptr->next_free = blk_ptr;

    // Or, add first free block.
    } else {
      blk_ptr->next_free = chk_ptr->free_list;
      chk_ptr->free_list = blk_ptr;
    }  // End if else - previous block is not contiguous

// If the following free block is contiguous merge them.
    if((struct blkhdr *)(blk_ptr->data + blk_ptr->free_size)
                                         == blk_ptr->next_free) {

      DBG("%s - Merging free block with following block at 0x%p.\n",
                                             func, blk_ptr->next_free);
      memory_overhead -= sizeof(struct blkhdr);
      blk_ptr->free_size += blk_ptr->next_free->free_size +
                                                    sizeof(struct blkhdr);
      blk_ptr->next_free = blk_ptr->next_free->next_free;

    }  // End if - following block is free

    if((total_available - (granularity + memory_leak + memory_overhead)) >=
                                                             low_data_mark) {
      if(free_sysrq.no_chk == 0) {
        free_sysrq.no_chk = 1;
        rt_pend_linux_srq(free_sysrq.srq);
        DBG("%s - Released a memory chunk.\n", func);
      }  // End if - Only pend free srq if none are in progress.
    }  // End if - Free chunks exceed low water resevoir.

    rt_spin_unlock_irqrestore(flags, &rt_mem_lock);

  }  // End if else - Valid block to free

}  // End function - rt_free


// This function tries to find size free bytes in the current chunk.
static void *alloc_in_chk(unsigned int size, struct chkhdr *chk)
{

  void *mem_ptr = NULL;
  struct blkhdr *cur, *prev = NULL, *next;
  char *func __attribute__ ((unused)) = "alloc_in_chk";

  for(cur = chk->free_list; cur != NULL; prev = cur, cur = cur->next_free) {

// Check the free block is large enough.
    if(cur->free_size >= size) {

      DBG("%s - Allocating block at %p\n", func, cur->data);

// Calculate the next free block in the chunk.
      if(cur->free_size - size >= sizeof(struct blkhdr) + MINSIZE) {
        next = (struct blkhdr *)(cur->data + size);
        next->chk_addr = chk->chk_addr;
        next->next_free = cur->next_free;
        next->free_size = cur->free_size - (size + sizeof(struct blkhdr));
        memory_overhead += sizeof(struct blkhdr);
      } else {

// We get here if there isn't enough free contiguous free
// memory left in this chunk for another block.
        next = cur->next_free;  // NB: This is NULL for last block in list
        DBG("%s - No space left in chunk 0x%p\n", func, chk);
      }  // End if else - determine next free block

// Remove the block from the free list.
      if(prev == NULL) {
        chk->free_list = next;
      } else {
        prev->next_free = next;
      }  // End if else - First time through the loop.

      DBG("%s - Next free block at %p\n", func, next);
      cur->next_free = cur;
      cur->free_size = size;
      mem_ptr = cur->data;
      break;
    }  // End if - free block large enough
  }  // End for loop - find free block in the chunk.
  if(mem_ptr != NULL) {
    memory_leak += size;
    if(memory_leak > max_allocated) {
      max_allocated = memory_leak;
    }
  }
  return(mem_ptr);

}  // End function - alloc_in_chk


// ---------------------< End memory management section >----------------------

// ----------------------< sysrq (Linux kernel) section >----------------------
void rt_alloc_sysrq_handler(void)
{

  unsigned long flags;
  struct chkhdr *chk_ptr, *chk_list_ptr;
  struct blkhdr *blk_ptr;
  char *func = "rt_alloc_sysrq_handler";

// Allocate "alloc_sysrq.no_chk" memory chunks, initialise them, and
// insert them into the chunk list.
  if((chk_ptr = (struct chkhdr *)alloc_chunk(granularity)) == NULL ) {
    printk("%s - Memory allocation failure.\n", func);
  } else {
#ifdef CONFIG_RTAI_MM_VMALLOC
	{
	unsigned long adr, page, size;
	char *pt;
	
	size  = granularity;
	pt = (char *) adr = (unsigned long) chk_ptr;
	for( ; size > 0 ; size -= PAGE_SIZE, adr += PAGE_SIZE, pt += PAGE_SIZE) { 
#if LINUX_VERSION_CODE < 0x020300
		page = kvirt_to_phys(adr);
		mem_map_reserve(MAP_NR(phys_to_virt(page)));
#else
		page  = kvirt_to_pa(adr);
		mem_map_reserve(virt_to_page(__va(page)));
#endif
		*pt = 0; // I'm paranoid, but let's have trap 14's (if any) while in Linux context. 
		}
	}
#endif
	chk_ptr->chk_addr = (void *)chk_ptr;
    chk_ptr->chk_limit = granularity;
    chk_ptr->next_chk = NULL;
    blk_ptr = chk_ptr->free_list = (struct blkhdr *)chk_ptr->data;
    blk_ptr->next_free = NULL;
    blk_ptr->chk_addr = (void *)chk_ptr;
    blk_ptr->free_size = chk_ptr->chk_limit - OVERHEAD;

// Hmm.. maybe this should go outside the while loop :-??
    flags = rt_spin_lock_irqsave(&rt_mem_lock);
    sys_malloc_calls++;
    total_available += granularity;
    memory_overhead += OVERHEAD;

// Paranoid, but just in case there are no chunks already allocated. :->>
    if( chk_list == NULL ) {
      chk_list = chk_ptr;
    } else {
      for(chk_list_ptr = chk_list; chk_list_ptr->next_chk != NULL;
                             chk_list_ptr = chk_list_ptr->next_chk) {
          ;
      }  // End for loop - find end of chunk list.
      chk_list_ptr->next_chk = chk_ptr;
    }  // End else - Not the 1st chunk in the list.
    alloc_sysrq.no_chk = 0;
    extra_chks += 1;
    rt_spin_unlock_irqrestore(flags, &rt_mem_lock);
    DPRINTK("%s - New chunk, Address: %p, size: %d\n",
        func, chk_ptr->chk_addr, chk_ptr->chk_limit);
    DPRINTK("%s - Extra chunk count: %d \n", func, extra_chks);

  }  // End if else - system malloc failed.
} // End function - rt_alloc_sysrq_handler



void rt_free_sysrq_handler(void)
{

  unsigned int do_free;
  unsigned long flags;
  struct chkhdr *chk_ptr, *prev_chk_ptr;
  struct blkhdr *blk_ptr;
  char *func __attribute__ ((unused)) = "rt_free_sysrq_handler";


  do_free = 0;
  prev_chk_ptr = NULL;
  if(extra_chks > 0) {
    flags = rt_spin_lock_irqsave(&rt_mem_lock);
    for(chk_ptr = chk_list; chk_ptr != NULL;
                            chk_ptr = chk_ptr->next_chk) {

      blk_ptr = chk_ptr->free_list;
      if(blk_ptr->free_size == chk_ptr->chk_limit - OVERHEAD) {

// Remove chunk to be freed from list.
        if(prev_chk_ptr == NULL) {
          chk_list = chk_ptr->next_chk;
        } else {
          prev_chk_ptr->next_chk = chk_ptr->next_chk;
        }  // End if else - handle first chunk in the list.
        extra_chks--;
        do_free = 1;
        break;
      }  // End if - chunk is completely free && more free chunks than needed

      prev_chk_ptr = chk_ptr;

    }  // End for loop - Loop through all chunks in the list

    free_sysrq.no_chk = 0;
    if(do_free) {

      sys_free_calls++;
      total_available -= granularity;
      memory_overhead -= OVERHEAD;
rt_printk("rt_free_sysrq_handler()\n");
#ifdef CONFIG_RTAI_MM_VMALLOC
	{
	unsigned long adr, page, size;

	size  = granularity;
	adr   =(unsigned long) chk_ptr;
    for( ; size > 0 ; size -= PAGE_SIZE, adr += PAGE_SIZE ) {
#if LINUX_VERSION_CODE < 0x020300
		page = kvirt_to_phys(adr);
		mem_map_unreserve(MAP_NR(phys_to_virt(page)));
#else
		page = kvirt_to_pa(adr);
		mem_map_unreserve(virt_to_page(__va(page)));
#endif
		}
	}					
#endif
	free_chunk(chk_ptr);  // Assumes that chk_ptr contains the address to be freed.

      DPRINTK("%s - freed chunk, Address: %p\n", func, chk_ptr);
      DPRINTK("%s - extra chunk count: %d\n", func, extra_chks);

    }  // End if - free the chunk memory.
    rt_spin_unlock_irqrestore(flags, &rt_mem_lock);
  } else {  // End if - extra chunks have been previously allocated.
    free_sysrq.no_chk = 0;
  }  // End if - Clear the free request count.
}  // End function - rt_free_sysrq_handler.


// --------------------< end sysrq (Linux kernel) section >--------------------


// ------------------------< proc filesystem section >-------------------------
#ifdef CONFIG_PROC_FS
static int mmgr_read_proc(char *page, char **start, off_t off, int count,
                          int *eof, void *data)
{

  PROC_PRINT_VARS;
  int i = 0;
  unsigned int chk_free;
  struct chkhdr *chk_ptr;

  PROC_PRINT("\nRTAI Dynamic Memory Management Status.\n");
  PROC_PRINT("----------------------------------------\n\n");
  PROC_PRINT("Chunk Size  Address    1st free block  Block size\n");
  PROC_PRINT("-------------------------------------------------\n");

  for(chk_ptr = chk_list; chk_ptr != NULL; chk_ptr = chk_ptr->next_chk) {
    if(chk_ptr->free_list == NULL) { // Needed to avoid possible Oops :-(
      chk_free = 0;
    } else {
      chk_free = chk_ptr->free_list->free_size;
    }
    PROC_PRINT("%-5d %-5d 0x%-8p 0x%-15p %-5d\n",
                   i,
                   chk_ptr->chk_limit,
                   chk_ptr->chk_addr,
                   chk_ptr->free_list,
                   chk_free);
    i += 1;
  } // End for loop - Display data for all chunks.

  PROC_PRINT_DONE;

}       // End function - mmgr_read_proc


static int mmgr_proc_register(void)
{

	static struct proc_dir_entry *proc_rtai_mmgr;

	proc_rtai_mmgr = create_proc_entry("memory_manager",
                                            S_IFREG | S_IRUGO | S_IWUSR,
                                            rtai_proc_root);
	if(!proc_rtai_mmgr) {
        	rt_printk("Unable to initialize: /proc/rtai/memory_manager\n");
		return(-1);
	}

	proc_rtai_mmgr->read_proc = mmgr_read_proc;

	return 0;
}       /* End function - rtai_proc_register */


static void mmgr_proc_unregister(void)
{
	remove_proc_entry("memory_manager", rtai_proc_root);
}       /* End function - rtai_proc_unregister */

// --------------------< end of proc filesystem section >--------------------
#endif  // CONFIG_PROC_FS

// ------------------------< Debug functions section >------------------------

// This function displays the allocation details of a chunk.
// The chunk to be displayed is determined from the address
// of a data block within the chunk.
void display_chunk(void *addr)
{

  struct chkhdr *chk_ptr;
  struct blkhdr *blk_ptr, *last;
  char free[5];
  char *func __attribute__ ((unused)) = "display_chunk";


  blk_ptr = (struct blkhdr *)(addr - (sizeof(struct blkhdr)));
  chk_ptr = (struct chkhdr *)blk_ptr->chk_addr;
  (void *)last = (void *)chk_ptr + (chk_ptr->chk_limit - sizeof(struct blkhdr) - MINSIZE);


  rt_printk("\n\n%s - Allocation for chunk: 0x%p\n", func, chk_ptr);
  rt_printk("----------------------------------------------------\n");
  blk_ptr = (struct blkhdr *)chk_ptr->data;
  do {

    if(blk_ptr == blk_ptr->next_free) {
      strncpy(free, "Used", strlen("Used") + 1);
    } else { // End if - Don't display free blocks.
      strncpy(free, "Free", strlen("Free") + 1);
    }

    rt_printk("%s - Block header: 0x%p, Status: %s, data address: 0x%p, block size: %d\n",
                                 func, blk_ptr, free, &blk_ptr->data, blk_ptr->free_size);

    (void *)blk_ptr = (void *)blk_ptr + sizeof(struct blkhdr) +
                                              blk_ptr->free_size;

  } while(blk_ptr < last); // End do loop - display all allocated blocks in the chunk.
  rt_printk("----------------------------------------------------\n\n");

}  // End function - display_chunk


void rt_mmgr_stats(void)
{
  char kv;

#ifdef CONFIG_RTAI_MM_VMALLOC
  kv = 'v';
#else
  kv = 'k';
#endif  
  printk("==== RT memory manager max allocated: %u bytes.\n", max_allocated);
  printk("==== RT memory manager kernel %cmalloc()'s:   %u\n", kv, sys_malloc_calls);
  printk("==== RT memory manager kernel %cfree()'s:     %u\n", kv, sys_free_calls);
  printk("==== RT memory manager overhead:      %u bytes.\n", memory_overhead);
  printk("==== RT memory manager leaks:         %u bytes.\n", memory_leak);

}  // End function - rt_mmgr_stats




// --------------------< End of debug functions section >---------------------


// ----------------------------------------------------------------------------
// Module Initialisation/Finalisation
// ----------------------------------------------------------------------------

int __rt_mem_init(void)
{

  int i;
  struct chkhdr *chk_ptr, *chk_list_ptr;
  struct blkhdr *blk_ptr;

// Stop the initialization function from being called more than once.
// Useful when this is used as an object module and not a stand alone
// kernel module.
  if(rt_mem_mgr_init != 0) {
    return(0);
  }

// Register the dynamic allocate and free srqs.
  if(( alloc_sysrq.srq = rt_request_srq(0, rt_alloc_sysrq_handler, 0)) < 0 ) {
    printk("rt_mem_mgr - Error allocating alloc sysrq: %d\n", alloc_sysrq.srq);
    return(alloc_sysrq.srq);
  }
  DPRINTK("rt_mem_mgr - Registered alloc sysrq no: %d\n", alloc_sysrq.srq);
  alloc_sysrq.no_chk = 0;
  free_sysrq.no_chk = 0;
  spin_lock_init(&alloc_sysrq.srq_lock);
  extra_chks = 0;
  if(( free_sysrq.srq = rt_request_srq(0, rt_free_sysrq_handler, 0)) < 0 ) {
    printk("rt_mem_mgr - Error allocating free sysrq: %d\n", free_sysrq.srq);
    return(free_sysrq.srq);
  }
  DPRINTK("rt_mem_mgr - Registered free sysrq no: %d\n", free_sysrq.srq);
  spin_lock_init(&free_sysrq.srq_lock);


// Allocate "low water" memory chunks, initialise them, and
// insert them into the chunk list.
  for( i = 0; i < low_chk_ref; i++ ) {
    if((chk_ptr = (struct chkhdr *)alloc_chunk(granularity)) == NULL ) {
      printk("rt_mem_mgr - Initial memory allocation failure.\n");
      return(-ENOMEM);
    }  // End for loop - Allocate low_chk_ref memory chunks.

    sys_malloc_calls++;
    total_available += granularity;
    memory_overhead += OVERHEAD;
    chk_ptr->chk_addr = (void *)chk_ptr;
    chk_ptr->chk_limit = granularity;
    chk_ptr->next_chk = NULL;
    blk_ptr = chk_ptr->free_list = (struct blkhdr *)chk_ptr->data;
    blk_ptr->next_free = NULL;
    blk_ptr->free_size = chk_ptr->chk_limit - OVERHEAD;
    blk_ptr->chk_addr = (void *)chk_ptr;
    if( chk_list == NULL ) {
      chk_list = chk_ptr;
    } else {
      for(chk_list_ptr = chk_list; chk_list_ptr->next_chk != NULL;
                         chk_list_ptr = chk_list_ptr->next_chk) {
              ;
      }  // End for loop - find end of chunk list.
      chk_list_ptr->next_chk = chk_ptr;
    }  // End else - Not the 1st chunk in the list.

    DPRINTK("rt_mem_mgr - Allocated chunk, Address: %p, size: %d\n",
           chk_ptr->chk_addr, chk_ptr->chk_limit);

  }  // End for loop - Allocate "low water" memory chunks.

// Register the proc system for this module.
#ifdef CONFIG_PROC_FS
        mmgr_proc_register();
#endif

  spin_lock_init(&rt_mem_lock);
  rt_mem_mgr_init = 1;

  printk("\n\n==== RT memory manager v%s Loaded. ====\n\n", rtmmgr_version);
  return(0);

} // End function - init_module


// -----------------------------------------------------------------------------
void __rt_mem_end(void)
{

  int oldsize;
  struct chkhdr *chk_ptr;
  void *tmp_chkptr;

  for(chk_ptr = chk_list; chk_ptr != NULL; ) {
    oldsize = chk_ptr->chk_limit;
    tmp_chkptr = chk_ptr->chk_addr;
    chk_ptr = chk_ptr->next_chk;
    sys_free_calls++;
    total_available -= granularity;
    memory_overhead -= OVERHEAD;
    free_chunk(tmp_chkptr);
    DPRINTK("rt_mem_mgr - Freed chunk, Address: %p, size: %d\n",
                                                   tmp_chkptr, oldsize);

  } // End for loop - Free up all allocated chunks.

// Unregister the dynamic allocate and free srqs.
  if( rt_free_srq(alloc_sysrq.srq) < 0 ) {
    printk("rt_mem_mgr - Attempt to free srq: %d failed.\n", alloc_sysrq.srq);
#ifdef DEBUG
  } else {
    DPRINTK("rt_mem_mgr - Released sysrq no: %d\n", alloc_sysrq.srq);
#endif
  }

  if( rt_free_srq(free_sysrq.srq) < 0 ) {
    printk("rt_mem_mgr - Attempt to free srq: %d failed.\n", free_sysrq.srq);
#ifdef DEBUG
  } else {
    DPRINTK("rt_mem_mgr - Released sysrq no: %d\n", free_sysrq.srq);
#endif
  }

// Unregister the proc system for this module.
#ifdef CONFIG_PROC_FS
        mmgr_proc_unregister();
#endif

  rt_mmgr_stats();
  printk("\n\n==== RT memory manager v%s Unloaded. ====\n\n", rtmmgr_version);

} // End function - cleanup_module

void rt_mem_end(void)
{
	__rt_mem_end();
}

#ifdef CONFIG_RTAI_DYN_MM_MODULE
int init_module(void)
{
	return __rt_mem_init();
}

void cleanup_module(void)
{
	__rt_mem_end();
}

int rt_mem_init(void)
{
	return 0;
}

void rt_mem_cleanup(void)
{

}
#else
int rt_mem_init(void)
{
	return __rt_mem_init();
}

void rt_mem_cleanup(void)
{
	__rt_mem_end();
}
#endif

#ifdef CONFIG_RTAI_DYN_MM_MODULE
MODULE_PARM(granularity,"i");
MODULE_PARM(low_chk_ref,"i");
MODULE_PARM(low_data_mark,"i");

EXPORT_SYMBOL(rt_malloc);
EXPORT_SYMBOL(rt_free);
EXPORT_SYMBOL(display_chunk);
EXPORT_SYMBOL(rt_mem_init);
EXPORT_SYMBOL(rt_mem_end);
EXPORT_SYMBOL(rt_mmgr_stats);
#endif

// ---------------------------------< eof >------------------------------------
