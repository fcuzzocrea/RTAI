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
#include "vrtx/event.h"

static xnqueue_t vrtxeventq;

static int event_destroy_internal(vrtxevent_t *event);

void vrtxevent_init (void) {
    initq(&vrtxeventq);
}

void vrtxevent_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&vrtxeventq)) != NULL)
	event_destroy_internal(link2vrtxevent(holder));
}

static int event_destroy_internal (vrtxevent_t *event)

{
    int s;

    removeq(&vrtxeventq,&event->link);
    vrtx_release_id(event->eventid);
    s = xnsynch_destroy(&event->synchbase);
    vrtx_mark_deleted(event);
    xnfree(event);

    return s;
}

int sc_fcreate (int *perr)
{
    vrtxevent_t *event;
    int eventid;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    event = (vrtxevent_t *)xnmalloc(sizeof(*event));

    if (!event)
	{
	*perr = ER_NOCB;
	return 0;
	}

    eventid = vrtx_alloc_id(event);

    if (eventid < 0)
	{
	*perr = ER_NOCB;
	xnfree(event);
	return 0;
	}

    xnsynch_init(&event->synchbase ,XNSYNCH_PRIO);
    inith(&event->link);
    event->eventid = eventid;
    event->magic = VRTX_EVENT_MAGIC;
    event->events = 0;

    xnmutex_lock(&__imutex);
    appendq(&vrtxeventq,&event->link);
    xnmutex_unlock(&__imutex);

    *perr = RET_OK;

    return eventid;
}

void sc_fdelete(int eventid, int opt, int *errp)
{
    vrtxevent_t *event;

    if ((opt != 0) && (opt != 1))
	{
	*errp = ER_IIP;
	return;
	}

    xnmutex_lock(&__imutex);

    event = (vrtxevent_t *)vrtx_find_object_by_id(eventid);

    if (event == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;

    if (opt == 0 && /* we look for pending task */
	xnsynch_nsleepers(&event->synchbase) > 0)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_PND;
	return;
	}

    /* forcing delete or no task pending */
    if (event_destroy_internal(event) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);
}

int sc_fpend (int group_id, long timeout, int mask, int opt, int *errp)
{
    vrtxevent_t *evgroup;
    vrtxtask_t *task;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ((opt != 0) && (opt != 1))
	{
	*errp = ER_IIP;
	return 0;
	}

    xnmutex_lock(&__imutex);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);

    if (evgroup == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return 0;
	}

    *errp = RET_OK;

    if ( ( (opt == 0) && ( (mask & evgroup->events) != 0) ) ||
	 ( (opt == 1) && ( (mask & evgroup->events) == mask) ) )
	{
	xnmutex_unlock(&__imutex);
	return (mask & evgroup->events);
	}

    task = vrtx_current_task();
    task->waitargs.evgroup.opt = opt;
    task->waitargs.evgroup.mask = mask;

    task->vrtxtcb.TCBSTAT = TBSFLAG;

    if (timeout)
	task->vrtxtcb.TCBSTAT |= TBSDELAY;

    /* xnsynch_sleep_on() called for the current thread automatically
       reschedules. */

    xnsynch_sleep_on(&evgroup->synchbase,timeout,&__imutex);

    if (xnthread_test_flags(&task->threadbase,XNRMID))
	{ /* Timeout */
	*errp = ER_DEL;
	}
    else if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	{ /* Timeout */
	*errp = ER_TMO;
	}

    xnmutex_unlock(&__imutex);

    return mask;
}

void sc_fpost (int group_id, int mask, int *errp)
{
    xnpholder_t *holder, *nholder;
    vrtxevent_t *evgroup;
    int topt;
    int tmask;

    xnmutex_lock(&__imutex);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);
    if (evgroup == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return;
	}

    if (evgroup->events & mask)
	{ /* one of the bits was set already */
	*errp = ER_OVF;
	}
    else
	{
	*errp = RET_OK;
	}

    evgroup->events |= mask;

    nholder = getheadpq(xnsynch_wait_queue(&evgroup->synchbase));

    while ((holder = nholder) != NULL)
	{
	vrtxtask_t *task = thread2vrtxtask(link2thread(holder,plink));
	topt = task->waitargs.evgroup.opt;
	tmask = task->waitargs.evgroup.mask;

	if ( ( (topt == 0) && ( (tmask & evgroup->events) != 0) ) ||
	     ( (topt == 1) && ( (tmask & evgroup->events) == mask) ) )
	    {
	    nholder = xnsynch_wakeup_this_sleeper(&evgroup->synchbase,holder);
	    }
	else
	    nholder = nextpq(xnsynch_wait_queue(&evgroup->synchbase),holder);
	}

    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);
}

int sc_fclear (int group_id, int mask, int *errp)
{
    vrtxevent_t *evgroup;
    int oldevents = 0;

    xnmutex_lock(&__imutex);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);

    if (evgroup == NULL)
	*errp = ER_ID;
    else
	{
	*errp = RET_OK;
	oldevents = evgroup->events;
	evgroup->events &= ~mask;
	}

    xnmutex_unlock(&__imutex);

    return oldevents;
}

int sc_finquiry (int group_id, int *errp)
{
    vrtxevent_t *evgroup;
    int mask;

    xnmutex_lock(&__imutex);

    evgroup = (vrtxevent_t *)vrtx_find_object_by_id(group_id);

    if (evgroup == NULL)
	{
	*errp = ER_ID;
	mask = 0;
	}
    else
	{
	*errp = RET_OK;
	mask = evgroup->events;
	}

    xnmutex_unlock(&__imutex);

    return mask;
}
