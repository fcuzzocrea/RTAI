/*!\file heap.c
 * \brief Dynamic memory allocation services.
 * \author Philippe Gerum
 *
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
 *
 * \ingroup heap
 */

/*!
 * \ingroup nucleus
 * \defgroup heap Dynamic memory allocation services.
 *
 * Dynamic memory allocation services.
 *
 * The implementation of the memory allocator follows the algorithm
 * described in a USENIX 1988 paper called "Design of a General
 * Purpose Memory Allocator for the 4.3BSD Unix Kernel" by Marshall
 * K. McKusick and Michael J. Karels. You can find it at various
 * locations on the net, including
 * http://docs.FreeBSD.org/44doc/papers/kernmalloc.pdf.  A minor
 * variation allows this implementation to have 'extendable' heaps
 * when needed, with multiple memory extents providing autonomous page
 * address spaces.
 *
 * The data structures hierarchy is as follows:
 *
 * HEAP {
 *      block_buckets[]
 *      extent_queue -------+
 * }                        |
 *                          V
 *                       EXTENT #1 {
 *                              {static header}
 *                              page_map[npages]
 *                              page_array[npages][pagesize]
 *                       } -+
 *                          |
 *                          |
 *                          V
 *                       EXTENT #n {
 *                              {static header}
 *                              page_map[npages]
 *                              page_array[npages][pagesize]
 *                       }
 *@{*/

#define XENO_HEAP_MODULE

#include <nucleus/pod.h>
#include <nucleus/thread.h>
#include <nucleus/heap.h>

xnheap_t kheap;			/* System heap */

static void init_extent (xnheap_t *heap,
			 xnextent_t *extent)
{
    caddr_t freepage;
    int n, lastpgnum;

    inith(&extent->link);

    /* The page area starts right after the (aligned) header. */
    extent->membase = (caddr_t)extent + heap->hdrsize;
    lastpgnum = heap->npages - 1;

    /* Mark each page as free in the page map. */
    for (n = 0, freepage = extent->membase;
	 n < lastpgnum; n++, freepage += heap->pagesize)
	{
	*((caddr_t *)freepage) = freepage + heap->pagesize;
	extent->pagemap[n] = XNHEAP_PFREE;
	}

    *((caddr_t *)freepage) = NULL;
    extent->pagemap[lastpgnum] = XNHEAP_PFREE;
    extent->memlim = freepage + heap->pagesize;

    /* The first page starts the free list of a new extent. */
    extent->freelist = extent->membase;
}

/*! 
 * \fn int xnheap_init(xnheap_t *heap,
                       void *heapaddr,
                       u_long heapsize,
                       u_long pagesize);
 * \brief Initialize a memory heap.
 *
 * Initializes a memory heap suitable for time-bounded allocation
 * requests of dynamic memory.
 *
 * @param heap The address of a heap descriptor which will be used to
 * store the allocation data.  This descriptor must always be valid
 * while the heap is active therefore it must be allocated in
 * permanent memory.
 *
 * @param heapaddr The address of the heap storage area. All
 * allocations will be made from the given area in time-bounded
 * mode. Since additional extents can be added to a heap, this
 * parameter is also known as the "initial extent".
 *
 * @param heapsize The size in bytes of the initial extent pointed at
 * by @a heapaddr. @a heapsize must be a multiple of pagesize and
 * lower than 16 Mbytes. @a heapsize must be large enough to contain
 * an internal header. The following formula gives the size of this
 * header: hdrsize = (sizeof(xnextent_t) + ((heapsize -
 * sizeof(xnextent_t))) / (pagesize + 1) + 15) & ~15.
 *
 * @param pagesize The size in bytes of the fundamental memory page
 * which will be used to subdivide the heap internally. Choosing the
 * right page size is important regarding performance and memory
 * fragmentation issues, so it might be a good idea to take a look at
 * http://docs.FreeBSD.org/44doc/papers/kernmalloc.pdf to pick the
 * best one for your needs. In the current implementation, pagesize
 * must be a power of two in the range [ 8 .. 32768 ] inclusive.
 *
 * @return 0 is returned upon success, or one of the following error
 * codes:
 *
 * - -EINVAL is returned whenever a parameter is invalid.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of any context.
 */

int xnheap_init (xnheap_t *heap,
		 void *heapaddr,
		 u_long heapsize,
		 u_long pagesize)
{
    u_long hdrsize, pmapsize, shiftsize, pageshift;
    xnextent_t *extent;
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
     * HEAPSIZE must be lower than XNHEAP_MAXEXTSZ.
     */

    if ((pagesize < (1 << XNHEAP_MINLOG2)) ||
	(pagesize > (1 << XNHEAP_MAXLOG2)) ||
	(pagesize & (pagesize - 1)) != 0 ||
	heapsize <= sizeof(xnextent_t) ||
	heapsize > XNHEAP_MAXEXTSZ ||
	(heapsize & (pagesize - 1)) != 0)
	return -EINVAL;

    /* Determine the page map overhead inside the given extent
       size. We need to reserve a byte in a page map for each page
       which is addressable into this extent. The page map is itself
       stored in the extent space, right after the static part of its
       header, and before the first allocatable page. */
    pmapsize = ((heapsize - sizeof(xnextent_t)) * sizeof(u_char)) / (pagesize + sizeof(u_char));

    /* The overall header size is: static_part + page_map rounded to
       the minimum alignment size. */
    hdrsize = (sizeof(xnextent_t) + pmapsize + XNHEAP_MINALIGNSZ - 1) & ~(XNHEAP_MINALIGNSZ - 1);

    /* An extent must contain at least two addressable pages to cope
       with allocation sizes between pagesize and 2 * pagesize. */
    if (hdrsize + 2 * pagesize > heapsize)
	return -EINVAL;

    /* Compute the page shiftmask from the page size (i.e. log2 value). */
    for (pageshift = 0, shiftsize = pagesize;
	 shiftsize > 1; shiftsize >>= 1, pageshift++)
	; /* Loop */

    heap->pagesize = pagesize;
    heap->pageshift = pageshift;
    heap->extentsize = heapsize;
    heap->hdrsize = hdrsize;
    heap->npages = (heapsize - hdrsize) >> pageshift;
    heap->ubytes = 0;
    heap->maxcont = heap->npages * pagesize;
    initq(&heap->extents);
    xnlock_init(&heap->archdep.lock);

    for (n = 0; n < XNHEAP_NBUCKETS; n++)
	heap->buckets[n] = NULL;

    extent = (xnextent_t *)heapaddr;

    init_extent(heap,extent);

    appendq(&heap->extents,&extent->link);

    xnarch_init_display_context(heap);

    return 0;
}

/*! 
 * \fn void xnheap_destroy(xnheap_t *heap,
		           void (*flushfn)(xnheap_t *heap,
			                   void *extaddr,
				           u_long extsize,
					   void *cookie),
					   void *cookie);
 * \brief Destroys a memory heap.
 *
 * Destroys a memory heap.
 *
 * @param heap The descriptor address of the destroyed heap.
 *
 * @param flushfn If non-NULL, the address of a flush routine which
 * will be called for each extent attached to the heap. This routine
 * can be used by the calling code to further release the heap memory.
 *
 * @param cookie If @a flushfn is non-NULL, @a cookie is an opaque
 * pointer which will be passed unmodified to @a flushfn.

 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread.
 */

void xnheap_destroy (xnheap_t *heap,
		     void (*flushfn)(xnheap_t *heap,
				     void *extaddr,
				     u_long extsize,
				     void *cookie),
		     void *cookie)
{
    xnholder_t *holder;
    spl_t s;

    if (!flushfn)
	return;

    xnlock_get_irqsave(&heap->archdep.lock,s);

    while ((holder = getq(&heap->extents)) != NULL)
	{
	xnlock_put_irqrestore(&heap->archdep.lock,s);
	flushfn(heap,link2extent(holder),heap->extentsize,cookie);
	xnlock_get_irqsave(&heap->archdep.lock,s);
	}

    xnlock_put_irqrestore(&heap->archdep.lock,s);
}

/*
 * get_free_range() -- Obtain a range of contiguous free pages to
 * fulfill an allocation of 2 ** log2size.  The caller must have
 * acquired the heap lock.
 */

static caddr_t get_free_range (xnheap_t *heap,
			       u_long bsize,
			       int log2size)
{
    caddr_t block, eblock, freepage, lastpage, headpage, freehead = NULL;
    u_long pagenum, pagecont, freecont;
    xnholder_t *holder;
    xnextent_t *extent;

    holder = getheadq(&heap->extents);

    while (holder != NULL)
	{
	extent = link2extent(holder);

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

	holder = nextq(&heap->extents,holder);
	}

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
       two). Otherwise, store the special marker XNHEAP_PLIST,
       indicating the start of a block whose size is a multiple of the
       standard page size, but not necessarily a power of two.  In any
       case, the following pages slots are marked as 'continued'
       (PCONT). */

    extent->pagemap[pagenum] = log2size ?: XNHEAP_PLIST;

    for (pagecont = bsize >> heap->pageshift; pagecont > 1; pagecont--)
	extent->pagemap[pagenum + pagecont - 1] = XNHEAP_PCONT;

    return headpage;
}

/*! 
 * \fn void *xnheap_alloc(xnheap_t *heap, u_long size);
 * \brief Allocate a memory block from a memory heap.
 *
 * Allocates a contiguous region of memory from an active memory heap.
 * Such allocation is guaranteed to be time-bounded.
 *
 * @param heap The descriptor address of the heap to get memory from.
 *
 * @param size The size in bytes of the requested block. Sizes lower
 * or equal to the page size are rounded either to the minimum
 * allocation size if lower than this value, or to the minimum
 * alignment size if greater or equal to this value. In the current
 * implementation, with MINALLOC = 8 and MINALIGN = 16, a 7 bytes
 * request will be rounded to 8 bytes, and a 17 bytes request will be
 * rounded to 32.
 *
 * @return The address of the allocated region upon success, or NULL
 * if no memory is available from the specified heap.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of any context.
 */

void *xnheap_alloc (xnheap_t *heap, u_long size)

{
    caddr_t block;
    u_long bsize;
    int log2size;
    spl_t s;

    if (size == 0)
	return NULL;

    if (size <= heap->pagesize)
	/* Sizes lower or equal to the page size are rounded either to
	   the minimum allocation size if lower than this value, or to
	   the minimum alignment size if greater or equal to this
	   value. In other words, with MINALLOC = 8 and MINALIGN = 16,
	   a 7 bytes request will be rounded to 8 bytes, and a 17
	   bytes request will be rounded to 32. */
	{
	if (size <= XNHEAP_MINALIGNSZ)
	    size = (size + XNHEAP_MINALLOCSZ - 1) & ~(XNHEAP_MINALLOCSZ - 1);
	else
	    size = (size + XNHEAP_MINALIGNSZ - 1) & ~(XNHEAP_MINALIGNSZ - 1);
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

	for (bsize = (1 << XNHEAP_MINLOG2), log2size = XNHEAP_MINLOG2;
	     bsize < size; bsize <<= 1, log2size++)
	    ; /* Loop */

	xnlock_get_irqsave(&heap->archdep.lock,s);

	block = heap->buckets[log2size - XNHEAP_MINLOG2];

	if (block == NULL)
	    {
	    block = get_free_range(heap,bsize,log2size);

	    if (block == NULL)
		goto release_and_exit;
	    }

	heap->buckets[log2size - XNHEAP_MINLOG2] = *((caddr_t *)block);
	heap->ubytes += bsize;
	}
    else
        {
        if (size > heap->maxcont)
            return NULL;

	xnlock_get_irqsave(&heap->archdep.lock,s);

	/* Directly request a free page range. */
	block = get_free_range(heap,size,0);

	if (block)   
	    heap->ubytes += size;
	}

release_and_exit:

    xnlock_put_irqrestore(&heap->archdep.lock,s);

    return block;
}

/*! 
 * \fn int xnheap_free(xnheap_t *heap, void *block);
 * \brief Release a memory block to a memory heap.
 *
 * Releases a memory region to the memory heap it was previously
 * allocated from.
 *
 * @param heap The descriptor address of the heap to release memory
 * to.
 *
 * @param block The address of the region to be returned to the heap.
 *
 * @return 0 is returned upon success, or -EINVAL is returned whenever
 * the block is not a valid region of the specified heap.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of any context.
 */

int xnheap_free (xnheap_t *heap, void *block)

{
    caddr_t freepage, lastpage, nextpage, tailpage;
    u_long pagenum, pagecont, boffset, bsize;
    xnextent_t *extent = NULL;
    int log2size, npages;
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&heap->archdep.lock,s);

    /* Find the extent from which the returned block is
       originating. */

    for (holder = getheadq(&heap->extents);
	 holder != NULL; holder = nextq(&heap->extents,holder))
	{
	extent = link2extent(holder);

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
	case XNHEAP_PFREE: /* Unallocated page? */
	case XNHEAP_PCONT:  /* Not a range heading page? */

unlock_and_fail:

	    xnlock_put_irqrestore(&heap->archdep.lock,s);
	    return -EINVAL;

	case XNHEAP_PLIST:

	    npages = 1;

	    while (npages < heap->npages &&
		   extent->pagemap[pagenum + npages] == XNHEAP_PCONT)
		npages++;

	    bsize = npages * heap->pagesize;

	    /* Link all freed pages in a single sub-list. */

	    for (freepage = (caddr_t)block,
		     tailpage = (caddr_t)block + bsize - heap->pagesize;
		 freepage < tailpage; freepage += heap->pagesize)
		*((caddr_t *)freepage) = freepage + heap->pagesize;

	    /* Mark the released pages as free in the extent's page map. */

	    for (pagecont = 0; pagecont < npages; pagecont++)
		extent->pagemap[pagenum + pagecont] = XNHEAP_PFREE;

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

	    *((caddr_t *)block) = heap->buckets[log2size - XNHEAP_MINLOG2];
	    heap->buckets[log2size - XNHEAP_MINLOG2] = block;

	    break;
	}

    heap->ubytes -= bsize;

    xnlock_put_irqrestore(&heap->archdep.lock,s);

    return 0;
}

/*! 
 * \fn int xnheap_extend(xnheap_t *heap, void *extaddr, u_long extsize);
 * \brief Extend a memory heap.
 *
 * Add a new extent to an existing memory heap.
 *
 * @param heap The descriptor address of the heap to add an extent to.
 *
 * @param extaddr The address of the extent memory.
 *
 * @param extsize The size of the extent memory (in bytes). In the
 * current implementation, this size must match the one of the initial
 * extent passed to xnheap_init().
 *
 * @return 0 is returned upon success, or -EINVAL is returned if
 * @a extsize differs from the initial extent's size.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of any context.
 */

int xnheap_extend (xnheap_t *heap, void *extaddr, u_long extsize)

{
    xnextent_t *extent = (xnextent_t *)extaddr;
    spl_t s;

    if (extsize != heap->extentsize)
	return -EINVAL;

    init_extent(heap,extent);

    xnlock_get_irqsave(&heap->archdep.lock,s);

    appendq(&heap->extents,&extent->link);

    xnlock_put_irqrestore(&heap->archdep.lock,s);

    return 0;
}

/*@}*/

EXPORT_SYMBOL(xnheap_alloc);
EXPORT_SYMBOL(xnheap_destroy);
EXPORT_SYMBOL(xnheap_extend);
EXPORT_SYMBOL(xnheap_free);
EXPORT_SYMBOL(xnheap_init);

EXPORT_SYMBOL(kheap);
