/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _RTAI_NUCLEUS_HEAP_H
#define _RTAI_NUCLEUS_HEAP_H

#include <nucleus/queue.h>

/*
 * CONSTRAINTS:
 *
 * Minimum page size is 2 ** XNHEAP_MINLOG2 (must be large enough to
 * hold a pointer).
 *
 * Maximum page size is 2 ** XNHEAP_MAXLOG2.
 *
 * Minimum block size equals the minimum page size.
 *
 * Requested block size smaller than the minimum block size is
 * rounded to the minimum block size.
 *
 * Requested block size larger than 2 times the page size is rounded
 * to the next page boundary and obtained from the free page
 * list. So we need a bucket for each power of two between
 * XNHEAP_MINLOG2 and XNHEAP_MAXLOG2 inclusive, plus one to honor
 * requests ranging from the maximum page size to twice this size.
 */

#if defined(__KERNEL__) || defined(__RTAI_UVM__) || defined(__RTAI_SIM__)

#define	XNHEAP_MINLOG2    3
#define	XNHEAP_MAXLOG2    22
#define	XNHEAP_MINALLOCSZ (1 << XNHEAP_MINLOG2)
#define	XNHEAP_MINALIGNSZ (1 << (XNHEAP_MINLOG2 + 1))
#define	XNHEAP_NBUCKETS   (XNHEAP_MAXLOG2 - XNHEAP_MINLOG2 + 2)
#define	XNHEAP_MAXEXTSZ   (1 << 24) /* i.e. 16Mb */

#define XNHEAP_PFREE   0
#define XNHEAP_PCONT   1
#define XNHEAP_PLIST   2

typedef struct xnextent {

    xnholder_t link;

#define link2extent(laddr) \
((xnextent_t *)(((char *)laddr) - (int)(&((xnextent_t *)0)->link)))

    caddr_t membase,	/* Base address of the page array */
	    memlim,	/* Memory limit of page array */
	    freelist;	/* Head of the free page list */

    u_char pagemap[1];	/* Beginning of page map */

} xnextent_t;

typedef struct xnheap {

#ifdef CONFIG_SMP
    xnlock_t lock;
#endif /* CONFIG_SMP */

    xnholder_t link;

#define link2heap(laddr) \
((xnheap_t *)(((char *)laddr) - (int)(&((xnheap_t *)0)->link)))

    u_long extentsize,
           pagesize,
           pageshift,
	   hdrsize,
	   npages,	/* Number of pages per extent */
	   ubytes,
           maxcont;

    xnqueue_t extents;

    caddr_t buckets[XNHEAP_NBUCKETS];

    xnarch_heapcb_t archdep;

    XNARCH_DECL_DISPLAY_CONTEXT();

} xnheap_t;

extern xnheap_t kheap;

#define xnheap_page_size(heap)   ((heap)->pagesize)
#define xnheap_page_count(heap)  ((heap)->npages)
#define xnheap_used_mem(heap)    ((heap)->ubytes)

#define xnmalloc(size)  xnheap_alloc(&kheap,size)
#define xnfree(ptr)     xnheap_free(&kheap,ptr)

#ifdef __cplusplus
extern "C" {
#endif

/* Private interface. */

#ifdef __KERNEL__

#define XNHEAP_DEV_MINOR 254

int xnheap_mount(void);

void xnheap_umount(void);

int xnheap_init_shared(xnheap_t *heap,
		       u_long heapsize,
		       int memflags);

int xnheap_destroy_shared(xnheap_t *heap);

#define xnheap_shared_offset(heap,ptr) \
(((caddr_t)(ptr)) - ((caddr_t)(heap)->archdep.shmbase))

#define xnheap_shared_address(heap,off) \
(((caddr_t)(heap)->archdep.shmbase) + (off))

#endif /* __KERNEL__ */

/* Public interface. */

int xnheap_init(xnheap_t *heap,
		void *heapaddr,
		u_long heapsize,
		u_long pagesize);

int xnheap_destroy(xnheap_t *heap,
		   void (*flushfn)(xnheap_t *heap,
				   void *extaddr,
				   u_long extsize,
				   void *cookie),
		   void *cookie);

int xnheap_extend(xnheap_t *heap,
		  void *extaddr,
		  u_long extsize);

void *xnheap_alloc(xnheap_t *heap,
		   u_long size);

int xnheap_free(xnheap_t *heap,
		void *block);

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __RTAI_UVM__ || __RTAI_SIM__ */

#define XNHEAP_DEV_NAME  "/dev/rtheap"

#endif /* !_RTAI_NUCLEUS_HEAP_H */
