/*!\file malloc.c
 * \brief Dynamic memory allocation services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Dynamic memory allocation services lifted and adapted from the
 * Xenomai nucleus.
 *
 * This file implements the RTAI dynamic memory allocator based on the
 * algorithm described in "Design of a General Purpose Memory
 * Allocator for the 4.3BSD Unix Kernel" by Marshall K. McKusick and
 * Michael J. Karels.
 *
 * \ingroup shm
 */

/*!
 * @addtogroup shm
 *@{*/

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <rtai_config.h>
#include <asm/rtai.h>
#ifdef CONFIG_RTAI_MALLOC_VMALLOC
#include <rtai_shm.h>
#else /* !CONFIG_RTAI_MALLOC_VMALLOC */
#include <linux/slab.h>
#endif /* CONFIG_RTAI_MALLOC_VMALLOC */
#include <rtai_malloc.h>

MODULE_PARM(rtai_global_heap_size,"i");

int rtai_global_heap_size = RTHEAP_GLOBALSZ;

void *rtai_global_heap_adr = NULL;

rtheap_t rtai_global_heap;	/* Global system heap */

static void *alloc_extent (u_long size)
{
	caddr_t p;
#ifdef CONFIG_RTAI_MALLOC_VMALLOC
	caddr_t _p;

	p = _p = (caddr_t)vmalloc(size);
	if (p) {
		printk("RTAI[malloc]: vmalloced extent %p, size %lu.\n", p, size);
		for (; size > 0; size -= PAGE_SIZE, _p += PAGE_SIZE) {
			mem_map_reserve(virt_to_page(__va(kvirt_to_pa((u_long)_p))));
		}
	}
#else /* !CONFIG_RTAI_MALLOC_VMALLOC */
	p = (caddr_t)kmalloc(size,GFP_KERNEL);
	printk("RTAI[malloc]: kmalloced extent %p, size %lu.\n", p, size);
#endif /* CONFIG_RTAI_MALLOC_VMALLOC */
	if (p) {
		memset(p, 0, size);
	}
	return p;
}

static void free_extent (void *p, u_long size)
{
#ifdef CONFIG_RTAI_MALLOC_VMALLOC
	caddr_t _p = (caddr_t)p;

	printk("RTAI[malloc]: vfreed extent %p, size %lu.\n", p, size);
	for (; size > 0; size -= PAGE_SIZE, _p += PAGE_SIZE) {
		mem_map_unreserve(virt_to_page(__va(kvirt_to_pa((u_long)_p))));
	}
	vfree(p);
#else /* !CONFIG_RTAI_MALLOC_VMALLOC */
	printk("RTAI[malloc]: kfreed extent %p, size %lu.\n", p, size);
	kfree(p);
#endif /* CONFIG_RTAI_MALLOC_VMALLOC */
}

static void init_extent (rtheap_t *heap, rtextent_t *extent)
{
	caddr_t freepage;
	int n, lastpgnum;

	INIT_LIST_HEAD(&extent->link);

	/* The page area starts right after the (aligned) header. */
	extent->membase = (caddr_t)extent + heap->hdrsize;
	lastpgnum = heap->npages - 1;

	/* Mark each page as free in the page map. */
	for (n = 0, freepage = extent->membase; n < lastpgnum; n++, freepage += heap->pagesize) {
		*((caddr_t *)freepage) = freepage + heap->pagesize;
		extent->pagemap[n] = RTHEAP_PFREE;
	}
	*((caddr_t *)freepage) = NULL;
	extent->pagemap[lastpgnum] = RTHEAP_PFREE;
	extent->memlim = freepage + heap->pagesize;

	/* The first page starts the free list of a new extent. */
	extent->freelist = extent->membase;
}

/*! 
 * \fn int rtheap_init(rtheap_t *heap,
                       void *heapaddr,
		       u_long heapsize,
		       u_long pagesize);
 * \brief Initialize a memory heap.
 *
 * Initializes a memory heap suitable for dynamic memory allocation
 * requests.  The heap manager can operate in two modes, whether
 * time-bounded if the heap storage area and size are statically
 * defined at initialization time, or dynamically extendable at the
 * expense of a less deterministic behaviour.
 *
 * @param heap The address of a heap descriptor the memory manager
 * will use to store the allocation data.  This descriptor must always
 * be valid while the heap is active therefore it must be allocated in
 * permanent memory.
 *
 * @param heapaddr The address of a statically-defined heap storage
 * area. If this parameter is non-zero, all allocations will be made
 * from the given area in fully time-bounded mode. In such a case, the
 * heap is non-extendable. If a null address is passed, the heap
 * manager will attempt to extend the heap each time a memory
 * starvation is encountered. In the latter case, the heap manager
 * will request additional chunks of core memory to Linux when needed,
 * voiding the real-time guarantee for the caller.
 *
 * @param heapsize If heapaddr is non-zero, heapsize gives the size in
 * bytes of the statically-defined storage area. Otherwise, heapsize
 * defines the standard length of each extent that will be requested
 * to Linux when a memory starvation is encountered for the heap.
 * heapsize must be a multiple of pagesize and lower than 16
 * Mbytes. Depending on the Linux allocation service used, requests
 * for extent memory might be limited in size. For instance, heapsize
 * must be lower than 128Kb for kmalloc()-based allocations. In the
 * current implementation, heapsize must be large enough to contain an
 * internal header. The following formula gives the size of this
 * header: hdrsize = (sizeof(rtextent_t) + ((heapsize -
 * sizeof(rtextent_t))) / (pagesize + 1) + 15) & ~15;
 *
 * @param pagesize The size in bytes of the fundamental memory page
 * which will be used to subdivide the heap internally. Choosing the
 * right page size is important regarding performance and memory
 * fragmentation issues, so it might be a good idea to take a look at
 * http://docs.FreeBSD.org/44doc/papers/kernmalloc.pdf to pick the
 * best one for your needs. In the current implementation, pagesize
 * must be a power of two in the range [ 8 .. 32768] inclusive.
 *
 * @return 0 is returned upon success, or one of the following
 * error codes:
 * - RTHEAP_PARAM is returned whenever a parameter is invalid.
 * - RTHEAP_NOMEM is returned if no initial extent can be allocated
 * for a dynamically extendable heap (i.e. heapaddr == NULL).
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

int rtheap_init (rtheap_t *heap, void *heapaddr, u_long heapsize, u_long pagesize)
{
	u_long hdrsize, pmapsize, shiftsize, pageshift;
	rtextent_t *extent;
	int n;

	/*
	 * Perform some parametrical checks first.
	 * Constraints are:
	 * PAGESIZE must be >= 2 ** MINLOG2.
	 * PAGESIZE must be <= 2 ** MAXLOG2.
	 * PAGESIZE must be a power of 2.
	 * HEAPSIZE must be large enough to contain the static part of an
	 * extent header.
	 * HEAPSIZE must be a multiple of PAGESIZE.
	 * HEAPSIZE must be lower than RTHEAP_MAXEXTSZ.
	 */
	if ((pagesize < (1 << RTHEAP_MINLOG2)) ||
	    (pagesize > (1 << RTHEAP_MAXLOG2)) ||
	    (pagesize & (pagesize - 1)) != 0 ||
	    heapsize <= sizeof(rtextent_t) ||
	    heapsize > RTHEAP_MAXEXTSZ ||
	    (heapsize & (pagesize - 1)) != 0) {
		return RTHEAP_PARAM;
	}

	/* Determine the page map overhead inside the given extent
	   size. We need to reserve a byte in a page map for each page
	   which is addressable into this extent. The page map is itself
	   stored in the extent space, right after the static part of its
	   header, and before the first allocatable page. */
	pmapsize = ((heapsize - sizeof(rtextent_t)) * sizeof(u_char)) / (pagesize + sizeof(u_char));

	/* The overall header size is: static_part + page_map rounded to
	   the minimum alignment size. */
	hdrsize = (sizeof(rtextent_t) + pmapsize + RTHEAP_MINALIGNSZ - 1) & ~(RTHEAP_MINALIGNSZ - 1);

	/* An extent must contain at least two addressable pages to cope
	   with allocation sizes between pagesize and 2 * pagesize. */
	if (hdrsize + 2 * pagesize > heapsize) {
		return RTHEAP_PARAM;
	}

	/* Compute the page shiftmask from the page size (i.e. log2 value). */
	for (pageshift = 0, shiftsize = pagesize; shiftsize > 1; shiftsize >>= 1, pageshift++);

	heap->pagesize   = pagesize;
	heap->pageshift  = pageshift;
	heap->hdrsize    = hdrsize;
#ifdef CONFIG_RTAI_MALLOC_VMALLOC
	heap->extentsize = heapsize;
#else  /* !CONFIG_RTAI_MALLOC_VMALLOC */
	heap->extentsize = heapsize > KMALLOC_LIMIT ? KMALLOC_LIMIT : heapsize;
#endif /* CONFIG_RTAI_MALLOC_VMALLOC */
	heap->npages     = (heap->extentsize - hdrsize) >> pageshift;
	heap->maxcont    = heap->npages*pagesize;
	heap->flags      =
	heap->ubytes     = 0;
	INIT_LIST_HEAD(&heap->extents);
	spin_lock_init(&heap->lock);

	for (n = 0; n < RTHEAP_NBUCKETS; n++) {
		heap->buckets[n] = NULL;
	}

	if (heapaddr) {
		extent = (rtextent_t *)heapaddr;
		init_extent(heap, extent);
		list_add_tail(&extent->link, &heap->extents);
	} else {
		u_long init_size = 0;
		while (init_size < heapsize) {
			if (!(extent = (rtextent_t *)alloc_extent(heap->extentsize))) {
				struct list_head *holder, *nholder;
				list_for_each_safe(holder, nholder, &heap->extents) {
					extent = list_entry(holder, rtextent_t, link);
					free_extent(extent, heap->extentsize);
				}
				return RTHEAP_NOMEM;
			}
			init_extent(heap, extent);
			list_add_tail(&extent->link, &heap->extents);
			init_size += heap->extentsize;
		}
	}
	return 0;
}

/*! 
 * \fn void rtheap_destroy(rtheap_t *heap);
 * \brief Destroys a memory heap.
 *
 * Destroys a memory heap. Dynamically allocated extents are returned
 * to Linux.
 *
 * @param heap The descriptor address of the destroyed heap.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

void rtheap_destroy (rtheap_t *heap)
{
	struct list_head *holder, *nholder;

	list_for_each_safe(holder, nholder, &heap->extents) {
		free_extent(list_entry(holder, rtextent_t, link), heap->extentsize);
	}
}

/*
 * get_free_range() -- Obtain a range of contiguous free pages to
 * fulfill an allocation of 2 ** log2size. Each extent is searched,
 * and a new one is allocated if needed, provided the heap is
 * extendable. Must be called with the heap lock set.
 */

static caddr_t get_free_range (rtheap_t *heap,
			       u_long bsize,
			       int log2size,
			       int mode)
{
    caddr_t block, eblock, freepage, lastpage, headpage, freehead = NULL;
    u_long pagenum, pagecont, freecont;
    struct list_head *holder;
    rtextent_t *extent;

    list_for_each(holder,&heap->extents) {

	extent = list_entry(holder,rtextent_t,link);
	freepage = extent->freelist;

	while (freepage != NULL)
	    {
	    headpage = freepage;
    	    freecont = 0;

	    /* Search for a range of contiguous pages in the free page
	       list of the current extent. The range must be 'bsize'
	       long. */
	    do
		{
		lastpage = freepage;
		freepage = *((caddr_t *)freepage);
		freecont += heap->pagesize;
		}
	    while (freepage == lastpage + heap->pagesize && freecont < bsize);

	    if (freecont >= bsize)
		{
		/* Ok, got it. Just update the extent's free page
		   list, then proceed to the next step. */

		if (headpage == extent->freelist)
		    extent->freelist = *((caddr_t *)lastpage);
		else   
		    *((caddr_t *)freehead) = *((caddr_t *)lastpage);

		goto splitpage;
		}

	    freehead = lastpage;
	    }
    }

    /* No available free range in the existing extents so far. If we
       cannot extend the heap, we have failed and we are done with
       this request. */

    return NULL;

splitpage:

    /* At this point, headpage is valid and points to the first page
       of a range of contiguous free pages larger or equal than
       'bsize'. */

    if (bsize < heap->pagesize)
	{
	/* If the allocation size is smaller than the standard page
	   size, split the page in smaller blocks of this size,
	   building a free list of free blocks. */

	for (block = headpage, eblock = headpage + heap->pagesize - bsize;
	     block < eblock; block += bsize)
	    *((caddr_t *)block) = block + bsize;

	*((caddr_t *)eblock) = NULL;
	}
    else   
        *((caddr_t *)headpage) = NULL;

    pagenum = (headpage - extent->membase) >> heap->pageshift;

    /* Update the extent's page map.  If log2size is non-zero
       (i.e. bsize <= 2 * pagesize), store it in the first page's slot
       to record the exact block size (which is a power of
       two). Otherwise, store the special marker RTHEAP_PLIST,
       indicating the start of a block whose size is a multiple of the
       standard page size, but not necessarily a power of two.  In any
       case, the following pages slots are marked as 'continued'
       (PCONT). */

    extent->pagemap[pagenum] = log2size ? log2size : RTHEAP_PLIST;

    for (pagecont = bsize >> heap->pageshift; pagecont > 1; pagecont--)
	extent->pagemap[pagenum + pagecont - 1] = RTHEAP_PCONT;

    return headpage;
}

/*! 
 * \fn void *rtheap_alloc(rtheap_t *heap, u_long size, int flags);
 * \brief Allocate a memory block from a memory heap.
 *
 * Allocates a contiguous region of memory from an active memory heap.
 * Such allocation is guaranteed to be time-bounded if the heap is
 * non-extendable (see rtheap_init()). Otherwise, it might trigger a
 * dynamic extension of the storage area through an internal request
 * to the Linux allocation service (kmalloc/vmalloc).
 *
 * @param heap The descriptor address of the heap to get memory from.
 *
 * @param size The size in bytes of the requested block. Sizes lower
 * or equal to the page size are rounded either to the minimum
 * allocation size if lower than this value, or to the minimum
 * alignment size if greater or equal to this value. In the current
 * implementation, with MINALLOC = 16 and MINALIGN = 16, a 15 bytes
 * request will be rounded to 16 bytes, and a 17 bytes request will be
 * rounded to 32.
 *
 * @param flags A set of flags affecting the operation. Unless
 * RTHEAP_EXTEND is passed and the heap is extendable, this service
 * will return NULL without attempting to extend the heap dynamically
 * upon memory starvation.
 *
 * @return The address of the allocated region upon success, or NULL
 * if no memory is available from the specified non-extendable heap,
 * or no memory can be obtained from Linux to extend the heap.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can always be called on behalf of a thread
 * context. It can also be called on behalf of an IST context if the
 * heap storage area has been statically-defined at initialization
 * time (see rtheap_init()).
 */

void *rtheap_alloc (rtheap_t *heap, u_long size, int mode)

{
    u_long bsize, flags;
    caddr_t block;
    int log2size;

    if (size == 0)
	return NULL;

    if (size <= heap->pagesize)
	/* Sizes lower or equal to the page size are rounded either to
	   the minimum allocation size if lower than this value, or to
	   the minimum alignment size if greater or equal to this
	   value. In other words, with MINALLOC = 15 and MINALIGN =
	   16, a 15 bytes request will be rounded to 16 bytes, and a
	   17 bytes request will be rounded to 32. */
	{
	if (size <= RTHEAP_MINALIGNSZ)
	    size = (size + RTHEAP_MINALLOCSZ - 1) & ~(RTHEAP_MINALLOCSZ - 1);
	else
	    size = (size + RTHEAP_MINALIGNSZ - 1) & ~(RTHEAP_MINALIGNSZ - 1);
	}
    else
	/* Sizes greater than the page size are rounded to a multiple
	   of the page size. */
	size = (size + heap->pagesize - 1) & ~(heap->pagesize - 1);

    /* It becomes more space efficient to directly allocate pages from
       the free page list whenever the requested size is greater than
       2 times the page size. Otherwise, use the bucketed memory
       blocks. */

    if (size <= heap->pagesize * 2)
	{
	/* Find the first power of two greater or equal to the rounded
	   size. The log2 value of this size is also computed. */

	for (bsize = (1 << RTHEAP_MINLOG2), log2size = RTHEAP_MINLOG2;
	     bsize < size; bsize <<= 1, log2size++)
	    ; /* Loop */

	flags = rt_spin_lock_irqsave(&heap->lock);

	block = heap->buckets[log2size - RTHEAP_MINLOG2];

	if (block == NULL)
	    {
	    block = get_free_range(heap,bsize,log2size,mode);

	    if (block == NULL)
		goto release_and_exit;
	    }

	heap->buckets[log2size - RTHEAP_MINLOG2] = *((caddr_t *)block);
	heap->ubytes += bsize;
	}
    else
        {
        if (size > heap->maxcont)
            return NULL;

	flags = rt_spin_lock_irqsave(&heap->lock);

	/* Directly request a free page range. */
	block = get_free_range(heap,size,0,mode);

	if (block)   
	    heap->ubytes += size;
	}

release_and_exit:

    rt_spin_unlock_irqrestore(flags,&heap->lock);

    return block;
}

/*! 
 * \fn int rtheap_free(rtheap_t *heap, void *block);
 * \brief Release a memory block to a memory heap.
 *
 * Releases a memory region to the memory heap it was previously
 * allocated from.
 *
 * @param heap The descriptor address of the heap to release memory
 * to.
 *
 * @param block The address of the region to release returned by a
 * previous call to rtheap_alloc().
 *
 * @return 0 is returned upon success, or RTHEAP_PARAM is returned
 * whenever the block is not a valid region of the specified heap.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context
 */

int rtheap_free (rtheap_t *heap, void *block)

{
    u_long pagenum, pagecont, boffset, bsize, flags;
    caddr_t freepage, lastpage, nextpage, tailpage;
    rtextent_t *extent = NULL;
    struct list_head *holder;
    int log2size, npages;

    flags = rt_spin_lock_irqsave(&heap->lock);

    /* Find the extent from which the returned block is
       originating. If the heap is non-extendable, then a single
       extent is scanned at most. */

    list_for_each(holder,&heap->extents) {

        extent = list_entry(holder,rtextent_t,link);

	if ((caddr_t)block >= extent->membase &&
	    (caddr_t)block < extent->memlim)
	    break;
    }

    if (!holder)
	goto unlock_and_fail;

    /* Compute the heading page number in the page map. */
    pagenum = ((caddr_t)block - extent->membase) >> heap->pageshift;
    boffset = ((caddr_t)block - (extent->membase + (pagenum << heap->pageshift)));

    switch (extent->pagemap[pagenum])
	{
	case RTHEAP_PFREE: /* Unallocated page? */
	case RTHEAP_PCONT:  /* Not a range heading page? */

unlock_and_fail:

	    rt_spin_unlock_irqrestore(flags,&heap->lock);
	    return RTHEAP_PARAM;

	case RTHEAP_PLIST:

	    npages = 1;

	    while (npages < heap->npages &&
		   extent->pagemap[pagenum + npages] == RTHEAP_PCONT)
		npages++;

	    bsize = npages * heap->pagesize;

	    /* Link all freed pages in a single sub-list. */

	    for (freepage = (caddr_t)block,
		     tailpage = (caddr_t)block + bsize - heap->pagesize;
		 freepage < tailpage; freepage += heap->pagesize)
		*((caddr_t *)freepage) = freepage + heap->pagesize;

	    /* Mark the released pages as free in the extent's page map. */

	    for (pagecont = 0; pagecont < npages; pagecont++)
		extent->pagemap[pagenum + pagecont] = RTHEAP_PFREE;

	    /* Return the sub-list to the free page list, keeping
	       an increasing address order to favor coalescence. */
    
	    for (nextpage = extent->freelist, lastpage = NULL;
		 nextpage != NULL && nextpage < (caddr_t)block;
		 lastpage = nextpage, nextpage = *((caddr_t *)nextpage))
		; /* Loop */

	    *((caddr_t *)tailpage) = nextpage;

	    if (lastpage)
		*((caddr_t *)lastpage) = (caddr_t)block;
	    else
		extent->freelist = (caddr_t)block;

	    break;

	default:

	    log2size = extent->pagemap[pagenum];
	    bsize = (1 << log2size);

	    if ((boffset & (bsize - 1)) != 0) /* Not a block start? */
		goto unlock_and_fail;

	    /* Return the block to the bucketed memory space. */

	    *((caddr_t *)block) = heap->buckets[log2size - RTHEAP_MINLOG2];
	    heap->buckets[log2size - RTHEAP_MINLOG2] = block;

	    break;
	}

    heap->ubytes -= bsize;

    rt_spin_unlock_irqrestore(flags,&heap->lock);

    return 0;
}

int __rtai_heap_init (void)
{
	rtai_global_heap_size = (rtai_global_heap_size + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
	if (rtheap_init(&rtai_global_heap, NULL, rtai_global_heap_size, PAGE_SIZE)) {
		printk(KERN_INFO "RTAI[malloc]: failed to initialize the global heap (size=%d bytes).\n", rtai_global_heap_size);
		return 1;
	}
	rtai_global_heap_adr = rtai_global_heap.extents.next;
	printk(KERN_INFO "RTAI[malloc]: loaded (global heap size=%d bytes).\n", rtai_global_heap_size);
	return 0;
}

void __rtai_heap_exit (void)
{
	rtheap_destroy(&rtai_global_heap);
	printk("RTAI[malloc]: unloaded.\n");
}

/*
 * IMPLEMENTATION NOTES:
 *
 * The implementation follows the algorithm described in a USENIX
 * 1988 paper called "Design of a General Purpose Memory Allocator for
 * the 4.3BSD Unix Kernel" by Marshall K. McKusick and Michael
 * J. Karels. You can find it at various locations on the net,
 * including http://docs.FreeBSD.org/44doc/papers/kernmalloc.pdf.
 * A minor variation allows this implementation to have 'extendable'
 * heaps when needed, with multiple memory extents providing autonomous
 * page address spaces. When the non-extendable form is used, the heap
 * management routines show bounded worst-case execution time.
 *
 * The data structures hierarchy is as follows:
 *
 * HEAP {
 *      block_buckets[]
 *      extent_queue -------+
 * }                        |
 *                          V
 *                       EXTENT #1 {
 *                              <static header>
 *                              page_map[npages]
 *                              page_array[npages][pagesize]
 *                       } -+
 *                          |
 *                          |
 *                          V
 *                       EXTENT #n {
 *                              <static header>
 *                              page_map[npages]
 *                              page_array[npages][pagesize]
 *                       }
 */

/*@}*/

#ifndef CONFIG_RTAI_MALLOC_BUILTIN
module_init(__rtai_heap_init);
module_exit(__rtai_heap_exit);
#endif /* !CONFIG_RTAI_MALLOC_BUILTIN */

#ifdef CONFIG_KBUILD
EXPORT_SYMBOL(rtheap_init);
EXPORT_SYMBOL(rtheap_destroy);
EXPORT_SYMBOL(rtheap_alloc);
EXPORT_SYMBOL(rtheap_free);
EXPORT_SYMBOL(rtai_global_heap);
EXPORT_SYMBOL(rtai_global_heap_adr);
EXPORT_SYMBOL(rtai_global_heap_size);
#endif /* CONFIG_KBUILD */
