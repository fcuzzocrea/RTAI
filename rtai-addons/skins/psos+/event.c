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

void ev_init (psosevent_t *evgroup)

{
    xnsynch_init(&evgroup->synchbase,XNSYNCH_FIFO);
    evgroup->events = 0;
}

void ev_destroy (psosevent_t *evgroup)

{
    xnmutex_lock(&__imutex);

    if (xnsynch_destroy(&evgroup->synchbase) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);
}

u_long ev_receive (u_long events,
		   u_long flags,
		   u_long timeout,
		   u_long *events_r)
{
    psosevent_t *evgroup;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    evgroup = &psos_current_task()->evgroup;

    if (!events)
	{
	*events_r = evgroup->events;
	xnmutex_unlock(&__imutex);
	return SUCCESS;
	}
	    
    if (flags & EV_NOWAIT)
	{
	u_long bits = (evgroup->events & events);
	evgroup->events &= ~events;
	*events_r = bits;

	xnmutex_unlock(&__imutex);

	if (flags & EV_ANY)
	    return bits ? SUCCESS : ERR_NOEVS;

	return bits == events ? SUCCESS : ERR_NOEVS;
	}
    
    if (((flags & EV_ANY) && (events & evgroup->events) != 0) ||
	(!(flags & EV_ANY) && ((events & evgroup->events) == events)))
	{
	*events_r = (evgroup->events & events);
	evgroup->events &= ~events;
	xnmutex_unlock(&__imutex);
	return SUCCESS;
	}

    psos_current_task()->waitargs.evgroup.flags = flags;
    psos_current_task()->waitargs.evgroup.events = events;
    xnsynch_sleep_on(&evgroup->synchbase,timeout,&__imutex);
    xnmutex_unlock(&__imutex);

    if (xnthread_test_flags(&psos_current_task()->threadbase,XNTIMEO))
	{
	*events_r = psos_current_task()->waitargs.evgroup.events;
	return ERR_TIMEOUT;
	}

    *events_r = psos_current_task()->waitargs.evgroup.events;

    return SUCCESS;
}

u_long ev_send (u_long tid,
		u_long events)
{
    psosevent_t *evgroup;
    psostask_t *task;

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    evgroup = &task->evgroup;
    evgroup->events |= events;

    /* Only the task to which the event group pertains can
       pend on it. */

    if (countpq(xnsynch_wait_queue(&evgroup->synchbase)) > 0)
	{
	u_long flags = task->waitargs.evgroup.flags;
	u_long bits = task->waitargs.evgroup.events;

	if (((flags & EV_ANY) && (bits & evgroup->events) != 0) ||
	    (!(flags & EV_ANY) && ((bits & evgroup->events) == bits)))
	    {
	    xnsynch_wakeup_one_sleeper(&evgroup->synchbase);
	    task->waitargs.evgroup.events = (bits & evgroup->events);
	    evgroup->events &= ~bits;
	    xnpod_schedule(&__imutex);
	    }
	}

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}
