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
 * Context: This routine can be called on behalf of a task or from the
 * initialization code.
 */

int rt_event_create (RT_EVENT *event,
                     const char *name,
                     unsigned long ivalue,
                     int mode)
{
    int err = 0;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnsynch_init(&event->synch_base,mode & EV_PRIO);
    event->value = ivalue;
    event->handle = 0;  /* i.e. (still) unregistered event. */
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
 * Side-effect: This routine calls the rescheduling procedure if tasks
 * have been woken up as a result of the deletion.
 *
 * Context: This routine can always be called on behalf of a task, or
 * from the initialization code.
 */

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

/**
 * @fn int rt_event_post(RT_EVENT *event,
                         unsigned long mask)
 * @brief Post an event group.
 *
 * Post a set of bits to the event mask. All tasks having their wait
 * request fulfilled by the posted events are resumed. In the same
 * move, the matched bits are automatically cleared from the event
 * mask by this service.
 *
 * @param event The descriptor address of the affected event.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not an event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group descriptor.
 *
 * Side-effect: This routine calls the rescheduling procedure if
 * a task is woken up as a result of the operation.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

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

/**
 * @fn int rt_event_pend(RT_EVENT *sem,
                         unsigned long mask,
                         unsigned long *mask_r,
                         int mode,
                         RTIME timeout)
 * @brief Pend on an event group.
 *
 * Pends for one or more events on the specified event group, either
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
 * request (see note). Passing RT_TIME_INFINITE causes the caller to
 * block indefinitely until the request is fulfilled. Passing
 * RT_TIME_NONBLOCK causes the service to return immediately without
 * waiting if the request cannot be satisfied immediately.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group
 * descriptor, including if the deletion occurred while the caller was
 * sleeping on it before the request has been satisfied.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * RT_TIME_NONBLOCK and the current event mask value does not satisfy
 * the request.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the request has been satisfied.
 *
 * - -ETIMEDOUT is returned if the request has not been satisfied
 * within the specified amount of time.
 *
 * Side-effect: This routine calls the rescheduling procedure unless
 * the request is immediately satisfied or @a timeout specifies a
 * non-blocking operation.
 *
 * Context: This routine can be called on behalf of a task.  It can
 * also be called on behalf of an interrupt context or from the
 * initialization code provided @a timeout is equal to
 * RT_TIME_NONBLOCK.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

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
    
    if (!mask)
	{
	*mask_r = event->value;
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

/**
 * @fn int rt_event_inquire(RT_EVENT *event,
                            RT_EVENT_INFO *info)
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
 * structure pointed at by @info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a event is not a event group descriptor.
 *
 * - -EIDRM is returned if @a event is a deleted event group
 * descriptor.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

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
