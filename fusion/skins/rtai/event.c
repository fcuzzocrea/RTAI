/**
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/event.h>
#include <rtai/registry.h>

int __event_pkg_init (void)

{
    return 0;
}

void __event_pkg_cleanup (void)

{
}

int rt_event_create (RT_EVENT *event,
		     const char *name,
		     unsigned long ivalue,
		     int mode)
{
    int err = 0;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnsynch_init(&event->synch_base,mode & EV_PRIO);
    event->value = ivalue;
    event->handle = 0;	/* i.e. (still) unregistered semaphore. */
    event->magic = RTAI_EVENT_MAGIC;
    xnobject_copy_name(event->name,name);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
	{
	err = rt_registry_enter(event->name,event,&event->handle);

	if (err)
	    rt_event_delete(event);
	}
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

int rt_event_delete (RT_EVENT *event)

{
    int err = 0, rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    event = rtai_h2obj_validate(event,RTAI_EVENT_MAGIC,RT_EVENT);

    if (!event)
	{
	err = rtai_handle_error(event,RTAI_EVENT_MAGIC,RT_EVENT);
	goto unlock_and_exit;
	}
    
    rc = xnsynch_destroy(&event->synch_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (event->handle)
	rt_registry_remove(event->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    rtai_mark_deleted(event);

    if (rc == XNSYNCH_RESCHED)
	/* Some task has been woken up as a result of the deletion:
	   reschedule now. */
	xnpod_schedule();

 unlock_and_exit:

    splexit(s);

    return err;
}

int rt_event_post (RT_EVENT *event,
		   unsigned long mask)
{
    xnpholder_t *holder, *nholder;
    int err = 0, resched = 0;
    spl_t s;

    splhigh(s);

    event = rtai_h2obj_validate(event,RTAI_EVENT_MAGIC,RT_EVENT);

    if (!event)
	{
	err = rtai_handle_error(event,RTAI_EVENT_MAGIC,RT_EVENT);
	goto unlock_and_exit;
	}
    
    /* Post the flags. */

    event->value |= mask;

    /* And wakeup any sleeper having its request fulfilled. */

    nholder = getheadpq(xnsynch_wait_queue(&event->synch_base));

    while ((holder = nholder) != NULL)
	{
	RT_TASK *sleeper = thread2rtask(link2thread(holder,plink));
	int mode = sleeper->wait_args.event.mode;
	unsigned long bits = sleeper->wait_args.event.mask;

	if (((mode & EV_ANY) && (bits & event->value) != 0) ||
	    (!(mode & EV_ANY) && ((bits & event->value) == bits)))
	    {
	    sleeper->wait_args.event.mask = (bits & event->value);
	    event->value &= ~bits;
	    nholder = xnsynch_wakeup_this_sleeper(&event->synch_base,holder);
	    resched = 1;
	    }
	else
	    nholder = nextpq(xnsynch_wait_queue(&event->synch_base),holder);
	}

    if (resched)
	xnpod_schedule();

 unlock_and_exit:

    splexit(s);

    return err;
}

int rt_event_pend (RT_EVENT *event,
		   unsigned long mask,
		   unsigned long *mask_r,
		   int mode,
		   RTIME timeout)
{
    RT_TASK *task;
    int err = 0;
    spl_t s;

    splhigh(s);

    event = rtai_h2obj_validate(event,RTAI_EVENT_MAGIC,RT_EVENT);

    if (!event)
	{
	err = rtai_handle_error(event,RTAI_EVENT_MAGIC,RT_EVENT);
	goto unlock_and_exit;
	}
    
    if (timeout == RT_TIME_NONBLOCK)
	{
	unsigned long bits = (event->value & mask);
	event->value &= ~mask;
	*mask_r = bits;

	if (mode & EV_ANY)
	    {
	    if (!bits)
		err = -EAGAIN;
	    }
	else if (bits != mask)
	    err =  -EAGAIN;

	goto unlock_and_exit;
	}

    if (((mode & EV_ANY) && (mask & event->value) != 0) ||
	(!(mode & EV_ANY) && ((mask & event->value) == mask)))
	{
	*mask_r = (event->value & mask);
	event->value &= ~mask;
	goto unlock_and_exit;
	}

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    task = rtai_current_task();
    task->wait_args.event.mode = mode;
    task->wait_args.event.mask = mask;
    xnsynch_sleep_on(&event->synch_base,timeout);
    /* The returned mask is only significant if the operation has
       succeeded, but do always write it back anyway. */
    *mask_r = task->wait_args.event.mask;

    if (xnthread_test_flags(&task->thread_base,XNRMID))
	err = -EIDRM; /* Event group deleted while pending. */
    else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	err = -ETIMEDOUT; /* Timeout.*/
    else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/

 unlock_and_exit:

    splexit(s);

    return err;
}

int rt_event_inquire (RT_EVENT *event,
		      RT_EVENT_INFO *info)
{
    int err = 0;
    spl_t s;

    splhigh(s);

    event = rtai_h2obj_validate(event,RTAI_EVENT_MAGIC,RT_EVENT);

    if (!event)
	{
	err = rtai_handle_error(event,RTAI_EVENT_MAGIC,RT_EVENT);
	goto unlock_and_exit;
	}
    
    strcpy(info->name,event->name);
    info->value = event->value;
    info->nsleepers = xnsynch_nsleepers(&event->synch_base);

 unlock_and_exit:

    splexit(s);

    return err;
}
