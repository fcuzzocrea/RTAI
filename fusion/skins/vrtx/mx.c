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
#include "vrtx/mx.h"

static vrtxmx_t vrtxmxmap[VRTX_MAX_MXID];
static int mx_destroy_internal (int indx);
static xnqueue_t mx_free_q;

#define vrtxmx2indx(addr) ((vrtxmx_t *)addr - vrtxmxmap)

void vrtxmx_init(void)
{
    int indx;
    initq(&mx_free_q);
    for (indx = 0 ; indx < VRTX_MAX_MXID ; indx++)
	{
	vrtxmxmap[indx].state = VRTXMX_FREE;
	inith(&vrtxmxmap[indx].link);
	appendq(&mx_free_q, &vrtxmxmap[indx].link);
	}
}

void vrtxmx_cleanup(void)
{
    int indx;
    for (indx = 0 ; indx < VRTX_MAX_MXID ; indx++)
	{
	mx_destroy_internal(indx);
	}
}

int mx_destroy_internal (int indx)
{
    spl_t s;
    int rc;

    splhigh(s);
    rc = xnsynch_destroy(&vrtxmxmap[indx].synchbase);
    appendq(&mx_free_q, &vrtxmxmap[indx].link);
    vrtxmxmap[indx].state = VRTXMX_FREE;
    splexit(s);

    return rc;
}


int sc_mcreate (unsigned int opt, int *errp)
{
    int indx;
    int flags;
    xnholder_t *holder;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    switch (opt) {
    case 0:
	flags = XNSYNCH_PRIO;
	break;
    case 1:
	flags = XNSYNCH_FIFO;
	break;
    case 2:
	flags = XNSYNCH_PRIO | XNSYNCH_PIP;
	break;
    default:
	*errp = ER_IIP;
	return 0;
    }

    splhigh(s);

    holder = getq(&mx_free_q);
    if (holder == NULL)
	{
	splexit(s);
	*errp = ER_NOCB;
	return -1;
	}

    indx = vrtxmx2indx(link2vrtxmx(holder));
    vrtxmxmap[indx].state = VRTXMX_UNLOCKED;

    xnsynch_init(&vrtxmxmap[indx].synchbase, flags);

    splexit(s);

    *errp = RET_OK;
    return indx;
}

void sc_mpost(int mid, int *errp)

{
    xnthread_t *waiter;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    if ( (mid < 0) || (mid >= VRTX_MAX_MXID)
	 || (vrtxmxmap[mid].state == VRTXMX_FREE) )
	{
	splexit(s);
	*errp = ER_ID;
	return;
	}

    waiter = xnsynch_wakeup_one_sleeper(&vrtxmxmap[mid].synchbase);
    
    if (waiter)
	xnpod_schedule();
    else
	{
	vrtxmxmap[mid].state = VRTXMX_UNLOCKED;
	}

    splexit(s);

    *errp = RET_OK;
    return;
}

void sc_mdelete (int mid, int opt, int *errp)
{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ( (opt != 0) && (opt != 1) )
	{
	*errp = ER_IIP;
	return;
	}

    splhigh(s);

    if ( (mid < 0) || (mid >= VRTX_MAX_MXID)
	 || (vrtxmxmap[mid].state == VRTXMX_FREE) )
	{
	splexit(s);
	*errp = ER_ID;
	return;
	}

    if (vrtxmxmap[mid].state == VRTXMX_LOCKED)
	{
	if ( (opt == 0) || (xnpod_current_thread() != vrtxmxmap[mid].owner) )
	    {
	    splexit(s);
	    *errp = ER_PND;
	    return;
	    }
	}
    *errp = RET_OK;

    /* forcing delete or no task pending */
    if (mx_destroy_internal(mid) == XNSYNCH_RESCHED)
	xnpod_schedule();

    splexit(s);
}

void sc_mpend (int mid, unsigned long timeout, int *errp)
{
    vrtxtask_t *task;
    spl_t s;

    splhigh(s);

    if ( (mid < 0) || (mid >= VRTX_MAX_MXID)
	 || (vrtxmxmap[mid].state == VRTXMX_FREE) )
	{
	splexit(s);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;

    if (vrtxmxmap[mid].state == VRTXMX_UNLOCKED)
	{
	vrtxmxmap[mid].state = VRTXMX_LOCKED;
	vrtxmxmap[mid].owner = xnpod_current_thread();
	}
    else
	{
	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSMUTEX;
	if (timeout)
	    task->vrtxtcb.TCBSTAT |= TBSDELAY;
	xnsynch_sleep_on(&vrtxmxmap[mid].synchbase,timeout);
	if (xnthread_test_flags(xnpod_current_thread(), XNRMID))
	    *errp = ER_DEL; /* Mutex deleted while pending. */
	else if (xnthread_test_flags(xnpod_current_thread(), XNTIMEO))
	    *errp = ER_TMO; /* Timeout.*/
	}

    splexit(s);
}

void sc_maccept (int mid, int *errp)
{
    spl_t s;

    splhigh(s);

   if ( (mid < 0) || (mid >= VRTX_MAX_MXID)
	 || (vrtxmxmap[mid].state == VRTXMX_FREE) )
	{
	splexit(s);
	*errp = ER_ID;
	return;
	}

    if (vrtxmxmap[mid].state == VRTXMX_UNLOCKED)
	{
	vrtxmxmap[mid].state = VRTXMX_LOCKED;
	vrtxmxmap[mid].owner = xnpod_current_thread();
	*errp = RET_OK;
	}
    else
	{
	*errp = ER_PND;
	}

    splexit(s);
}

int sc_minquiry (int mid, int *errp)
{
    spl_t s;
    int rc;

    splhigh(s);

    if ( (mid < 0) || (mid >= VRTX_MAX_MXID)
	 || (vrtxmxmap[mid].state == VRTXMX_FREE) )
	{
	splexit(s);
	*errp = ER_ID;
	return 0;
	}

    rc = (vrtxmxmap[mid].state == VRTXMX_UNLOCKED);

    splexit(s);

    return rc;
}
