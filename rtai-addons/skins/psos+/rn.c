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

#include "rtai_config.h"
#include "psos+/task.h"
#include "psos+/rn.h"

static xnqueue_t psosrnq;

static psosrn_t *psosrn0;

static void *rn0addr;

static int rn_destroy_internal(psosrn_t *rn);

int psosrn_init (u_long rn0size)

{
    u_long allocsize, rn0id;
    initq(&psosrnq);

    if (rn0size < 2048)
	rn0size = 2048;

    rn0addr = xnmalloc(rn0size);

    if (!rn0addr)
	return XNERR_NOMEM;

    rn_create("RN#0",
	      rn0addr,
	      rn0size,
	      128,
	      RN_FORCEDEL,
	      &rn0id,
	      &allocsize);

    psosrn0 = (psosrn_t *)rn0id; /* Eeek... */

    return XN_OK;
}

void psosrn_cleanup (void) {

    xnholder_t *holder;

    while ((holder = getheadq(&psosrnq)) != NULL)
	rn_destroy_internal(link2psosrn(holder));

    if (rn0addr)
	xnfree(rn0addr);
}

static int rn_destroy_internal (psosrn_t *rn)

{
    int s;

    xnmutex_lock(&__imutex);

    removeq(&psosrnq,&rn->link);
    s = xnsynch_destroy(&rn->synchbase);
    xnheap_destroy(&rn->heapbase);
    psos_mark_deleted(rn);

    xnmutex_unlock(&__imutex);

    return s;
}

u_long rn_create (char name[4],
		  void *rnaddr,
		  u_long rnsize,
		  u_long usize,
		  u_long flags,
		  u_long *rnid,
		  u_long *allocsize)
{
    int bflags = 0;
    psosrn_t *rn;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ((u_long)rnaddr & (sizeof(u_long) - 1))
	return ERR_RNADDR;

    if (usize < 16)
	return ERR_TINYUNIT;

    if ((usize & (usize - 1)) != 0)
	return ERR_UNITSIZE; /* Not a power of two. */

    if (rnsize <= sizeof(psosrn_t))
	return ERR_TINYRN;

    if (flags & RN_PRIOR)
	bflags |= XNSYNCH_PRIO;

    if (flags & RN_DEL)
	bflags |= RN_FORCEDEL;

    rn = (psosrn_t *)rnaddr;
    rnsize -= sizeof(psosrn_t);

    inith(&rn->link);
    rn->rnsize = rnsize; /* Adjusted region size. */
    rn->usize = usize;
    rn->data = (char *)&rn[1];
    rn->name[0] = name[0];
    rn->name[1] = name[1];
    rn->name[2] = name[2];
    rn->name[3] = name[3];
    rn->name[4] = '\0';

    if (xnheap_init(&rn->heapbase,rn->data,rnsize,4096) != XN_OK)
	return ERR_TINYRN;

    xnsynch_init(&rn->synchbase,bflags);

    rn->magic = PSOS_RN_MAGIC;
    xnmutex_lock(&__imutex);
    appendq(&psosrnq,&rn->link);
    xnmutex_unlock(&__imutex);
    *rnid = (u_long)rn;
    *allocsize = rn->rnsize;

    return SUCCESS;
}

u_long rn_delete (u_long rnid)

{
    psosrn_t *rn;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (rnid == 0)	/* May not delete region #0 */
	return ERR_OBJID;

    xnmutex_lock(&__imutex);

    rn = psos_h2obj_active(rnid,PSOS_RN_MAGIC,psosrn_t);

    if (!rn)
	{
	u_long err = psos_handle_error(rnid,PSOS_RN_MAGIC,psosrn_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (rn_destroy_internal(rn) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long rn_getseg (u_long rnid,
		  u_long size,
		  u_long flags,
		  u_long timeout,
		  void **segaddr)
{
    u_long err = SUCCESS;
    psostask_t *caller;
    psosrn_t *rn;
    void *chunk;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    if (rnid == 0)
	rn = psosrn0;
    else
	{
	rn = psos_h2obj_active(rnid,PSOS_RN_MAGIC,psosrn_t);

	if (!rn)
	    {
	    u_long err = psos_handle_error(rnid,PSOS_RN_MAGIC,psosrn_t);
	    xnmutex_unlock(&__imutex);
	    return err;
	    }
	}

    if (size > rn->rnsize)
	{
	xnmutex_unlock(&__imutex);
	return ERR_TOOBIG;
	}

    chunk = xnheap_alloc(&rn->heapbase,size,XNHEAP_NOWAIT);

    xnmutex_unlock(&__imutex);

    if (chunk == NULL)
	{
	if (flags & RN_NOWAIT)
	    {
	    /* Be gracious to those who are lazy with respect to
	       return code checking -- set the pointer to NULL :o> */
	    *segaddr = NULL;
	    return ERR_NOSEG;
	    }

	caller = psos_current_task();
	caller->waitargs.region.size = size;
	caller->waitargs.region.chunk = NULL;
	xnsynch_sleep_on(&rn->synchbase,timeout,NULL);

	if (xnthread_test_flags(&caller->threadbase,XNRMID))
	    err = ERR_RNKILLD; /* Region deleted while pending. */
	else if (xnthread_test_flags(&caller->threadbase,XNTIMEO))
	    err = ERR_TIMEOUT; /* Timeout.*/

	chunk = caller->waitargs.region.chunk;
	}

    *segaddr = chunk;

    return err;
}

u_long rn_ident (char name[4],
		 u_long *rnid)
{
    xnholder_t *holder;
    psosrn_t *rn;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    for (holder = getheadq(&psosrnq);
	 holder; holder = nextq(&psosrnq,holder))
	{
	rn = link2psosrn(holder);

	if (rn->name[0] == name[0] &&
	    rn->name[1] == name[1] &&
	    rn->name[2] == name[2] &&
	    rn->name[3] == name[3])
	    {
	    *rnid = (u_long)rn;
	    xnmutex_unlock(&__imutex);
	    return SUCCESS;
	    }
	}

    xnmutex_unlock(&__imutex);

    return ERR_OBJNF;
}

u_long rn_retseg (u_long rnid,
		  void *chunk)
{
    xnsynch_t *synch;
    psosrn_t *rn;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    if (rnid == 0)
	rn = psosrn0;
    else
	{
	rn = psos_h2obj_active(rnid,PSOS_RN_MAGIC,psosrn_t);

	if (!rn)
	    {
	    u_long err = psos_handle_error(rnid,PSOS_RN_MAGIC,psosrn_t);
	    xnmutex_unlock(&__imutex);
	    return err;
	    }
	}

    if ((char *)chunk < rn->data || (char *)chunk >= rn->data + rn->rnsize)
	{
	xnmutex_unlock(&__imutex);
	return ERR_NOTINRN;
	}

    if (xnheap_free(&rn->heapbase,chunk) == XNERR_PARAM)
	{
	xnmutex_unlock(&__imutex);
	return ERR_SEGADDR;
	}

    /* Attempt to wake up one or more threads pending on a memory
       request since some memory has just been released. */

    synch = &rn->synchbase;

    if (xnsynch_nsleepers(synch) > 0)
	{
	xnpholder_t *holder, *nholder;
	
	nholder = getheadpq(xnsynch_wait_queue(synch));

	while ((holder = nholder) != NULL)
	    {
	    psostask_t *sleeper = thread2psostask(link2thread(holder,plink));

	    chunk = xnheap_alloc(&rn->heapbase,
				 sleeper->waitargs.region.size,
				 XNHEAP_NOWAIT);
	    if (chunk)
		{
		nholder = xnsynch_wakeup_this_sleeper(synch,holder);
		sleeper->waitargs.region.chunk = chunk;
		}
	    else
		nholder = nextpq(xnsynch_wait_queue(synch),holder);
	    }

	xnpod_schedule(&__imutex);
	}

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - All region-related services are strictly synchronous (i.e.
 * cannot be called on behalf of an ISR), so a scheduler lock
 * is enough to protect from other threads of activity when
 * accessing the region's internal data.
 */
