/**
 * @file
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
 *
 * \ingroup event
 */

/*!
 * \ingroup native
 * \defgroup event Event flag group services.
 *
 * An event flag group is a synchronization object represented by a
 * long-word structure; every available bit in such word can be used
 * to map a user-defined event flag.  When a flag is set, the
 * associated event is said to have occurred. RTAI tasks and interrupt
 * handlers can use event flags to signal the occurrence of events to
 * other tasks; those tasks can either wait for the events to occur in
 * a conjuntive manner (all awaited events must have occurred to wake
 * up), or in a disjunctive way (at least one of the awaited events
 * must have occurred to wake up).
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/event.h>
#include <rtai/registry.h>

#if CONFIG_RTAI_NATIVE_EXPORT_REGISTRY

static ssize_t __event_read_proc (char *page,
				char **start,
				off_t off,
				int count,
				int *eof,
				void *data)
{
    RT_EVENT *event = (RT_EVENT *)data;
    char *p = page;
    int len;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    p += sprintf(p,"=0x%lx\n",event->value);

    if (xnsynch_nsleepers(&event->synch_base) > 0)
	{
	xnpholder_t *holder;

	/* Pended event -- dump waiters. */

	holder = getheadpq(xnsynch_wait_queue(&event->synch_base));

	while (holder)
	    {
	    xnthread_t *sleeper = link2thread(holder,plink);
	    RT_TASK *task = thread2rtask(sleeper);
	    const char *mode = (task->wait_args.event.mode & EV_ANY) ? "any" : "all";
	    unsigned long mask = task->wait_args.event.mask;

	    if (*xnthread_name(sleeper))
		p += sprintf(p,"+%s (mask=0x%lx, %s)\n",
			     xnthread_name(sleeper),
			     mask,
			     mode);
	    else
		p += sprintf(p,"+%p\n (mask=0x%lx, %s)\n",
			     sleeper,
			     mask,
			     mode);

	    holder = nextpq(xnsynch_wait_queue(&event->synch_base),holder);
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    len = (p - page) - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static RT_OBJECT_PROCNODE __event_pnode = {

    .dir = NULL,
    .type = "events",
    .entries = 0,
    .read_proc = &__event_read_proc,
    .write_proc = NULL
};

#elif CONFIG_RTAI_OPT_NATIVE_REGISTRY

static RT_OBJECT_PROCNODE __event_pnode = {

    .type = "events"
};

#endif /* CONFIG_RTAI_NATIVE_EXPORT_REGISTRY */

/**
 * @fn int rt_event_create(RT_EVENT *event,
                           const char *name,
                           unsigned long ivalue,
                           int mode)
 * @brief Create an event group.
 *
 * Event groups provide for task synchronization by allowing a set of
 * flags (or "events") to be waited for and posted atomically. An
 * event group contains a mask of received events; any set of bits
 * from the event mask can be pended or posted in a single operation.
 *
 * Tasks can wait for a conjunctive (AND) or disjunctive (OR) set of
 * events to occur.  A task pending on an event group in conjunctive
 * mode is woken up as soon as all awaited events are set in the event
 * mask. A task pending on an event group in disjunctive mode is woken
 * up as soon as any awaited event is set in the event mask.

 * @param event The address of an event group descriptor RTAI will
 * use to store the event-related data.  This descriptor must always
 * be valid while the group is active therefore it must be allocated
 * in permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * group. When non-NULL and non-empty, this string is copied to a
 * safe place into the descriptor, and passed to the registry package
 * if enabled for indexing the created event group.
 *
 * @param ivalue The initial value of the group's event mask.
 *
 * @param mode The event group creation mode. The following flags can
 * be OR'ed into this bitmask, each of them affecting the new group:
 *
 * - EV_FIFO makes tasks pend in FIFO order on the event group.
 *
 * - EV_PRIO makes tasks pend in priority order on the event group.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_event_create (RT_EVENT *event,
                     const char *name,
                     unsigned long ivalue,
                     int mode)
{
    int err = 0;

    if (xnpod_asynch_p())
	return -EPERM;

    xnsynch_init(&event->synch_base,mode & EV_PRIO);
    event->value = ivalue;
    event->handle = 0;  /* i.e. (still) unregistered event. */
    event->magic = RTAI_EVENT_MAGIC;
    xnobject_copy_name(event->name,name);

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    event->cpid = 0;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(event->name,event,&event->handle,&__event_pnode);

        if (err)
            rt_event_delete(event);
        }
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_event_delete(RT_EVENT *event)
 * @brief Delete an event group.
 *
 * Destroy an event group and release all the tasks currently pending
 * on it.  An event group exists in the system since rt_event_create()
 * has been called to create it, so this service must be called in
 * order to destroy it afterwards.
 *
 * @param event The descriptor address of the affected event group.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group descriptor.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_event_delete (RT_EVENT *event)

{
    int err = 0, rc;
    spl_t s;

    if (xnpod_asynch_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

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

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_event_signal(RT_EVENT *event,
                           unsigned long mask)
 * @brief Post an event group.
 *
 * Post a set of bits to the event mask. All tasks having their wait
 * request fulfilled by the posted events are resumed.
 *
 * @param event The descriptor address of the affected event.
 *
 * @param mask The set of events to be posted.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not an event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_event_signal (RT_EVENT *event,
		     unsigned long mask)
{
    xnpholder_t *holder, *nholder;
    int err = 0, resched = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

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
            nholder = xnsynch_wakeup_this_sleeper(&event->synch_base,holder);
            resched = 1;
            }
        else
            nholder = nextpq(xnsynch_wait_queue(&event->synch_base),holder);
        }

    if (resched)
        xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_event_wait(RT_EVENT *event,
                         unsigned long mask,
                         unsigned long *mask_r,
                         int mode,
                         RTIME timeout)
 * @brief Pend on an event group.
 *
 * Waits for one or more events on the specified event group, either
 * in conjunctive or disjunctive mode.

 * If the specified set of bits is not set, the calling task is
 * blocked. The task is not resumed until the request is fulfilled.
 *
 * @param event The descriptor address of the affected event group.
 *
 * @param mask The set of bits to wait for. Passing zero causes this
 * service to return immediately with a success value; the current
 * value of the event mask is also copied to @a mask_r.
 *
 * @param mask_r The value of the event mask at the time the task was
 * readied.
 *
 * @param mode The pend mode. The following flags can be OR'ed into
 * this bitmask, each of them affecting the operation:
 *
 * - EV_ANY makes the task pend in disjunctive mode (i.e. OR); this
 * means that the request is fulfilled when at least one bit set into
 * @a mask is set in the current event mask.
 *
 * - EV_ALL makes the task pend in conjunctive mode (i.e. AND); this
 * means that the request is fulfilled when at all bits set into @a
 * mask are set in the current event mask.
 *
 * @param timeout The number of clock ticks to wait for fulfilling the
 * request (see note). Passing TM_INFINITE causes the caller to block
 * indefinitely until the request is fulfilled. Passing TM_NONBLOCK
 * causes the service to return immediately without waiting if the
 * request cannot be satisfied immediately.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group
 * descriptor, including if the deletion occurred while the caller was
 * sleeping on it before the request has been satisfied.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and the current event mask value does not satisfy the request.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the request has been satisfied.
 *
 * - -ETIMEDOUT is returned if the request has not been satisfied
 * within the specified amount of time.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime or
 * scheduler locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

int rt_event_wait (RT_EVENT *event,
                   unsigned long mask,
                   unsigned long *mask_r,
                   int mode,
                   RTIME timeout)
{
    RT_TASK *task;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    event = rtai_h2obj_validate(event,RTAI_EVENT_MAGIC,RT_EVENT);

    if (!event)
        {
        err = rtai_handle_error(event,RTAI_EVENT_MAGIC,RT_EVENT);
        goto unlock_and_exit;
        }
    
    if (!mask)
	{
	*mask_r = event->value;
	goto unlock_and_exit;
	}
    
    if (timeout == TM_NONBLOCK)
        {
        unsigned long bits = (event->value & mask);
        event->value &= ~mask;
        *mask_r = bits;

        if (mode & EV_ANY)
            {
            if (!bits)
                err = -EWOULDBLOCK;
            }
        else if (bits != mask)
            err =  -EWOULDBLOCK;

        goto unlock_and_exit;
        }

    if (((mode & EV_ANY) && (mask & event->value) != 0) ||
        (!(mode & EV_ANY) && ((mask & event->value) == mask)))
        {
        *mask_r = (event->value & mask);
        event->value &= ~mask;
        goto unlock_and_exit;
        }

    if (xnpod_unblockable_p())
	{
	err = -EPERM;
	goto unlock_and_exit;
	}

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

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_event_clear(RT_EVENT *event,
                          unsigned long mask,
			  unsigned long *mask_r)
 * @brief Clear an event group.
 *
 * Clears a set of flags from an event mask.
 *
 * @param event The descriptor address of the affected event.
 *
 * @param mask The set of events to be cleared.
 *
 * @param mask_r If non-NULL, @a mask_r is the address of a memory
 * location which will be written upon success with the previous value
 * of the event group before the flags are cleared.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not an event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_event_clear (RT_EVENT *event,
		    unsigned long mask,
		    unsigned long *mask_r)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    event = rtai_h2obj_validate(event,RTAI_EVENT_MAGIC,RT_EVENT);

    if (!event)
        {
        err = rtai_handle_error(event,RTAI_EVENT_MAGIC,RT_EVENT);
        goto unlock_and_exit;
        }

    if (mask_r)
	*mask_r = event->value;
    
    /* Clear the flags. */

    event->value &= ~mask;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_event_inquire(RT_EVENT *event, RT_EVENT_INFO *info)
 * @brief Inquire about an event group.
 *
 * Return various information about the status of a specified
 * event group.
 *
 * @param event The descriptor address of the inquired event group.
 *
 * @param info The address of a structure the event group information
 * will be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group
 * descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_event_inquire (RT_EVENT *event,
                      RT_EVENT_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    event = rtai_h2obj_validate(event,RTAI_EVENT_MAGIC,RT_EVENT);

    if (!event)
        {
        err = rtai_handle_error(event,RTAI_EVENT_MAGIC,RT_EVENT);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,event->name);
    info->value = event->value;
    info->nwaiters = xnsynch_nsleepers(&event->synch_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_event_bind(RT_EVENT *event,
			const char *name)
 * @brief Bind to an event flag group.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given RTAI event flag group identified by its symbolic name. If the
 * event flag group does not exist on entry, this service blocks the
 * caller until a event flag group of the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * event flag group to bind to.
 *
 * @param event The address of an event flag group descriptor
 * retrieved by the operation. Contents of this memory is undefined
 * upon failure.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EFAULT is returned if @a event or @a name is referencing invalid
 * memory.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the retrieval has completed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied.
 */

/**
 * @fn int rt_event_unbind(RT_EVENT *event)
 *
 * @brief Unbind from an event flag group.
 *
 * This user-space only service unbinds the calling task from the
 * event flag group object previously retrieved by a call to
 * rt_event_bind().
 *
 * @param event The address of an event flag group descriptor to
 * unbind from.
 *
 * @return 0 is always returned.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

int __event_pkg_init (void)

{
    return 0;
}

void __event_pkg_cleanup (void)

{
}

/*@}*/

EXPORT_SYMBOL(rt_event_create);
EXPORT_SYMBOL(rt_event_delete);
EXPORT_SYMBOL(rt_event_signal);
EXPORT_SYMBOL(rt_event_wait);
EXPORT_SYMBOL(rt_event_clear);
EXPORT_SYMBOL(rt_event_inquire);
