/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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

#include "rtai_config.h"
#include "vrtx/task.h"
#include "vrtx/heap.h"

static xnqueue_t vrtxheapq;

static void heap_destroy_internal(vrtxheap_t *heap);

int vrtxheap_init (u_long heap0size)

{
    char *heap0addr;
    int err;
    int heapid;

    initq(&vrtxheapq);

    if (heap0size < 2048)
	heap0size = 2048;

    heap0addr = (char *)xnmalloc(heap0size);

    if (!heap0addr)
	return XNERR_POD_NOMEM;

    heapid = sc_hcreate(heap0addr, heap0size, 7, &err);
    if ( err != RET_OK )
	{
	if ( err == ER_IIP)
	    {
	    return XNERR_HEAP_PARAM;
	    }
	else
	    {
	    return XNERR_HEAP_NOMEM;
	    }
	}
  
    return XN_OK;
}

void vrtxheap_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&vrtxheapq)) != NULL)
	heap_destroy_internal(link2vrtxheap(holder));
}

static void heap_destroy_internal (vrtxheap_t *heap)

{
    xnmutex_lock(&__imutex);
    removeq(&vrtxheapq,&heap->link);
    vrtx_mark_deleted(heap);
    xnmutex_unlock(&__imutex);
    xnheap_destroy(&heap->sysheap);
}


int sc_hcreate(char *heapaddr,
	       u_long heapsize,
	       unsigned log2psize,
	       int *perr)
{
    u_long pagesize;
    vrtxheap_t *heap;

    int err;
    int heapid;


    *perr = RET_OK;

    /* checks will be done in xnheap_init */

    if (log2psize == 0)
	{
	pagesize = 512; /* VRTXsa system call reference */
	}
    else
	{
	pagesize = 1 << log2psize;
	}


    heap = (vrtxheap_t *)xnmalloc(sizeof(*heap));

    if (!heap)
	{
	*perr = ER_NOCB;
	return 0;
	}

    err = xnheap_init(&heap->sysheap, heapaddr, heapsize, pagesize);
    if (err != XN_OK)
	{
	    if (err == XNERR_HEAP_PARAM)
	        {
		*perr = ER_IIP;
		}
	    else
	        {
		*perr = ER_NOCB;
		}
	    xnfree(heap);

	    return 0;
	}


    heap->magic = VRTX_HEAP_MAGIC;
    inith(&heap->link);
    heap->log2psize = log2psize;
    heap->allocated = 0;
    heap->released = 0;

    xnmutex_lock(&__imutex);
    heapid = vrtx_alloc_id(heap);
    appendq(&vrtxheapq, &heap->link);
    xnmutex_unlock(&__imutex);

    return heapid;
}

void sc_hdelete (int hid, int opt, int *errp)
{
    vrtxheap_t *heap;

    xnmutex_lock(&__imutex);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);
    
    if (opt == 0)
	{ /* delete heap only if no blocks are allocated */
	if (heap->sysheap.ubytes > 0)
	    {
	    xnmutex_unlock(&__imutex);
	    *errp = ER_PND;
	    return;
	    }
	}
    else if (opt != 1)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_IIP;
	return;
	}

    *errp = RET_OK;

    heap_destroy_internal(heap);

    xnmutex_unlock(&__imutex);
}

char *sc_halloc(int hid, unsigned long bsize, int *errp)
{
    vrtxheap_t *heap;
    char *blockp;

    xnmutex_lock(&__imutex);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);
    if (heap == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return NULL;
	}

    blockp = xnheap_alloc(&heap->sysheap, bsize, XNHEAP_NOWAIT);
    if (blockp == NULL)
	{
	*errp = ER_MEM;
	}
    else
	{
	*errp = RET_OK;
	}
    heap->allocated++;

    xnmutex_unlock(&__imutex);

    return blockp;
}    

void sc_hfree(int hid, char *blockp, int *errp)
{
    vrtxheap_t *heap;

    xnmutex_lock(&__imutex);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);

    if (heap == NULL)
	{
	*errp = ER_ID;
	xnmutex_unlock(&__imutex);
	return;
	}

    if (XN_OK != xnheap_free(&heap->sysheap, blockp))
	{
	*errp = ER_NMB;
	xnmutex_unlock(&__imutex);
	return;
	}

    *errp = RET_OK;
    heap->allocated--;
    heap->released++;

    xnmutex_unlock(&__imutex);
}

void sc_hinquiry (int info[3], int hid, int *errp)
{
    vrtxheap_t *heap;

    xnmutex_lock(&__imutex);

    heap = (vrtxheap_t *)vrtx_find_object_by_id(hid);
    if (heap == NULL)
	{
	*errp = ER_ID;
	xnmutex_unlock(&__imutex);
	return;
	}
    else
	{
	*errp = RET_OK;
	}

    info[0] = heap->allocated;

    info[1] = heap->released;

    info[2] = heap->log2psize;

    xnmutex_unlock(&__imutex);
}
