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
#include <nucleus/heap.h>
#include <rtai/task.h>
#include <rtai/registry.h>

static xnqueue_t __rtai_task_q;

static void __task_delete_hook (xnthread_t *thread)

{
    /* The scheduler is locked while hooks are running. */
    RT_TASK *task;

    if (xnthread_magic(thread) != RTAI_SKIN_MAGIC)
	return;

    task = thread2rtask(thread);

    xntimer_destroy(&task->timer);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (task->handle)
	rt_registry_remove(task->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    removeq(&__rtai_task_q,&task->link);

    rtai_mark_deleted(task);

    if (xnthread_test_flags(&task->thread_base,XNSHADOW))
	xnfree(task);
}

static void __task_periodic_handler (void *cookie)

{
    RT_TASK *task = (RT_TASK *)cookie;

    task->overrun++;

    if (xnthread_test_flags(&task->thread_base,XNDELAY))
	xnpod_resume_thread(&task->thread_base,XNDELAY);
}

int __task_pkg_init (void)

{
    initq(&__rtai_task_q);
    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__task_delete_hook);

    return 0;
}

void __task_pkg_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&__rtai_task_q)) != NULL)
	rt_task_delete(link2rtask(holder));

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__task_delete_hook);
}

/**
 * @fn int rt_task_create(RT_TASK *task,
		            const char *name,
			     int stksize,
			     int prio,
			     int mode)
 * @brief Create a new real-time task.
 *
 * @param task The address of a task descriptor RTAI will use to store
 * the task-related data.  This descriptor must always be valid while
 * the task is active therefore it must be allocated in permanent
 * memory.
 *
 * The task is left in an innocuous state until it is actually started
 * by rt_task_start().
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created task.
 *
 * @param stksize The size of the stack (in bytes) for the new
 * task. If zero is passed, a reasonable pre-defined size will be
 * substituted.
 *
 * @param prio The base priority of the new thread. This value must
 * range from [1 .. 99] (inclusive) where 1 is the highest priority.
 *
 * @param mode The task creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new task:
 *
 * - T_FPU allows the task to use the FPU whenever available on the
 * platform.
 *
 * - T_CPU(cpuid) makes the new task affine to CPU # @b cpuid. CPU
 * identifiers range from 0 to CONFIG_RTAI_HW_NRCPUS - 1 (inclusive).
 *
 * Passing T_FPU|T_CPU(1) in the @a mode parameter thus creates a task
 * with FPU support enabled and which will be affine to CPU #1.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to create or
 * register the task.
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * Context: This routine can be called on behalf of a task or from the
 * initialization code.
 */

int rt_task_create (RT_TASK *task,
		    const char *name,
		    int stksize,
		    int prio,
		    int mode)
{
    xnflags_t bflags;
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    bflags = mode & (XNFPU|XNSHADOW);

    if (xnpod_init_thread(&task->thread_base,
			  name,
			  rtprio2xn(prio),
			  bflags,
			  stksize) != 0)
	/* Assume this is the only possible failure. */
	return -ENOMEM;

    xnthread_set_magic(&task->thread_base,RTAI_SKIN_MAGIC);

    inith(&task->link);
    task->suspend_depth = 0;
    task->overrun = -1;
    task->handle = 0;	/* i.e. (still) unregistered task. */
    xntimer_init(&task->timer,&__task_periodic_handler,task);

    splhigh(s);
    task->magic = RTAI_TASK_MAGIC;
    appendq(&__rtai_task_q,&task->link);
    splexit(s);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
	{
	err = rt_registry_enter(xnthread_name(&task->thread_base),task,&task->handle);

	if (err)
	    rt_task_delete(task);
	}
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_task_start(RT_TASK *task,
			    void (*entry)(void *cookie),
			    void *cookie)
 * @brief Start a real-time task.
 *
 * Start a (newly) created task, scheduling it for the first
 * time. This call releases the target task from the dormant state.
 *
 * The START hooks are called on behalf of the calling context (if
 * any, see rt_task_add_hook()).
 *
 * @param task The descriptor address of the affected task which must
 * have been previously created by the rt_task_create() service.
 *
 * @param entry The address of the task's body routine. In other
 * words, it is the task entry point.
 *
 * @param cookie A user-defined opaque cookie the real-time kernel
 * will pass to the emerging task as the sole argument of its entry
 * point.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * - -EBUSY is returned if @a task is already started.
 *
 * Side-effect: This routine calls the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a task context or
 * from the initialization code.
 */

int rt_task_start (RT_TASK *task,
		   void (*entry)(void *cookie),
		   void *cookie)
{
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}

    if (!xnthread_test_flags(&task->thread_base,XNDORMANT))
	{
	err = -EBUSY; /* Task already started. */
	goto unlock_and_exit;
	}

    xnpod_start_thread(&task->thread_base,
		       0,
		       0,
		       XNPOD_ALL_CPUS,
		       entry,
		       cookie);

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_suspend(RT_TASK *task)
 * @brief Suspend a real-time task.
 *
 * Forcibly suspend the execution of a task. This task will not be
 * eligible for scheduling until it is explicitly resumed by a call to
 * rt_task_resume().
 *
 * A nesting count is maintained so that rt_task_suspend() and
 * rt_task_resume() must be used in pairs.
 *
 * @param task The descriptor address of the affected task. If @a task
 * is NULL, the current task is suspended.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor, or if @a
 * task is NULL but not called from a task context.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Side-effect: This routine calls the rescheduling procedure if the
 * current task suspends itself.
 *
 * Context: This routine can always be called on behalf of a task. It
 * can also be called on behalf of an interrupt context or from the
 * initialization code provided @a task is non-NULL.
 */

int rt_task_suspend (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    if (!task)
	{
	if (xnpod_asynch_p() || xnpod_root_p())
	    return -EINVAL;

	task = rtai_current_task();
	}

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}

    if (task->suspend_depth++ == 0)
	xnpod_suspend_thread(&task->thread_base,
			     XNSUSP,
			     XN_INFINITE,
			     NULL);

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_resume(RT_TASK *task)
 * @brief Resume a real-time task.
 *
 * Forcibly resume the execution of a task which has been previously
 * suspended by a call to rt_task_suspend().
 *
 * The suspension nesting count is decremented so that
 * rt_task_resume() will only resume the task if this count falls down
 * to zero as a result of the current invocation.
 *
 * @param task The descriptor address of the affected task.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Side-effect: This routine calls the rescheduling procedure if the
 * suspension nesting level falls down to zero as a result of the
 * current invocation.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context, or from the initialization code.
 */

int rt_task_resume (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}

    if (task->suspend_depth > 0 && --task->suspend_depth == 0)
	{
	xnpod_resume_thread(&task->thread_base,XNSUSP);
	xnpod_schedule();
	}

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_delete(RT_TASK *task)
 * @brief Delete a real-time task.
 *
 * Terminate a task and release all the real-time kernel resources it
 * currently holds. A task exists in the system since rt_task_create()
 * has been called to create it, so this service must be called in
 * order to destroy it afterwards.
 *
 * The DELETE hooks are called on behalf of the calling context (if
 * any). The information stored in the task control block remains
 * valid until all hooks have been called.
 *
 * @param task The descriptor address of the affected task. If @a task
 * is NULL, the current task is deleted.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor, or if @a
 * task is NULL but not called from a task context.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Side-effect: This routine calls the rescheduling procedure if the
 * current task self-deletes.
 *
 * Context: This routine can always be called on behalf of a task. It
 * can also be called on behalf of the initialization code provided @a
 * task is non-NULL.
 */

int rt_task_delete (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (!task)
	{
	if (xnpod_root_p())
	    return -EINVAL;

	task = rtai_current_task();
	}

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    /* Does not return if task is current. */
    xnpod_delete_thread(&task->thread_base);

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_yield(void)
 * @brief Manual round-robin.
 *
 * Move the current task to the end of its priority group, so that the
 * next equal-priority task in ready state is switched in.
 *
 * Side-effect: This routine calls the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a task.
 */

void rt_task_yield (void)

{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    xnpod_yield();
}

/**
 * @fn int rt_task_set_periodic(RT_TASK *task,
                                RTIME idate,
				    RTIME period)
 * @brief Make a real-time task periodic.
 *
 * Make a task periodic by programing its first release point and its
 * period in the processor time line.  Subsequent calls to
 * rt_task_wait_period() will delay the task until the next periodic
 * release point in the processor timeline is reached.
 *
 * @param task The descriptor address of the affected task. This task
 * is immediately delayed until the first periodic release point is
 * reached. If @a task is NULL, the current task is set periodic.
 *
 * @param idate The initial (absolute) date of the first release
 * point, expressed in clock ticks (see note). The affected task will
 * be delayed until this point is reached.

 * @param period The period of the task, expressed in clock ticks (see
 * note).
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * - -ETIMEDOUT is returned if @a idate has already elapsed.
 *
 * Side-effect: This routine calls the rescheduling procedure if the
 * operation affects the current task and @a idate has not elapsed
 * yet.
 *
 * Context: This routine can always be called on behalf of a task. It
 * can also be called on behalf from the initialization code provided
 * @a task is non-NULL.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

int rt_task_set_periodic (RT_TASK *task,
			  RTIME idate,
			  RTIME period)
{
    int err = 0;
    RTIME now;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (!task)
	{
	if (xnpod_root_p())
	    return -EINVAL;

	task = rtai_current_task();
	}

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}

    task->suspend_depth = 0;
    now = xnpod_get_time();

    if (idate > now && xntimer_start(&task->timer,idate - now,period) == 0)
	xnpod_suspend_thread(&task->thread_base,
			     XNDELAY,
			     XN_INFINITE,
			     NULL);
    else
	err = -ETIMEDOUT;

    task->overrun = -1;

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_wait_period(void)
 * @brief Wait for the next periodic release point.
 *
 * Make the current task wait for the next periodic release point in
 * the processor time line.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if rt_task_set_periodic() has not previously
 * been called for the calling task.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the next periodic release point has been
 * reached.
 *
 * - -ETIMEDOUT is returned if a timer overrun occurred, which indicates
 * that a previous release point has been missed by the calling task.
 *
 * Side-effect: This routine calls the rescheduling procedure unless
 * an overrun has been detected.  In the latter case, the current task
 * immediately returns from this service without being delayed.
 *
 * Context: This routine can be called on behalf of a task.
 */

int rt_task_wait_period (void)

{
    RT_TASK *task;
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    task = rtai_current_task();

    if (!xntimer_active_p(&task->timer))
	{
	err = -EINVAL;
	goto unlock_and_exit;
	}

    if (task->overrun < 0)
	{
	xnpod_suspend_thread(&task->thread_base,
			     XNDELAY,
			     XN_INFINITE,
			     NULL);

	if (xnthread_test_flags(&task->thread_base,XNBREAK))
	    {
	    err = -EINTR;
	    goto unlock_and_exit;
	    }
	}
    else
	err = -ETIMEDOUT;

    task->overrun--;

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_set_priority(RT_TASK *task,
                                int prio)
 * @brief Change the base priority of a real-time task.
 *
 * Changing the base priority does not affect the priority boost the
 * target task might have obtained as a consequence of a previous
 * priority inheritance.
 *
 * @param task The descriptor address of the affected task.
 *
 * @param prio The new task priority. This value must range from [1
 * .. 99] (inclusive) where 1 is the highest priority.

 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Side-effects:
 *
 * - This service calls the rescheduling procedure.
 *
 * - Assigning the same priority to a running or ready task moves it
 * to the end of its priority group, thus causing a manual
 * round-robin.
 *
 * Context: This routine can always be called on behalf of a task. It
 * can also be called on behalf of an interrupt context or from the
 * initialization code provided @a task is non-NULL.
 */

int rt_task_set_priority (RT_TASK *task,
			  int prio)
{
    int oldprio;
    spl_t s;

    if (prio < T_HIPRIO || prio > T_LOPRIO)
	return -EINVAL;

    if (!task)
	{
	if (xnpod_asynch_p() || xnpod_root_p())
	    return -EINVAL;

	task = rtai_current_task();
	}

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	oldprio = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    oldprio = xnprio2rt(xnthread_base_priority(&task->thread_base));

    xnpod_renice_thread(&task->thread_base,rtprio2xn(prio));

    xnpod_schedule();

 unlock_and_exit:

    splexit(s);

    return oldprio;
}

/**
 * @fn int rt_task_sleep(RTIME delay)
 * @brief Delay the calling task (relative).
 *
 * Delay the execution of the calling task for a number of internal
 * clock ticks.
 *
 * @param delay The number of clock ticks to wait before resuming the
 * task (see note). Passing zero causes the task to return immediately
 * with no delay.
 *
 * @return 0 is returned upon success, otherwise:
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * sleeping task before the sleep time has elapsed.
 *
 * Side-effect: This routine calls the rescheduling procedure unless a
 * null delay is given.
 *
 * Context: This routine can be called on behalf of a task.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

int rt_task_sleep (RTIME delay)

{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (delay == 0)
	return 0;

    /* Calling the suspension service on behalf of the current task
       implicitely calls the rescheduling procedure. */

    xnpod_suspend_thread(&rtai_current_task()->thread_base,
			 XNDELAY,
			 delay,
			 NULL);

    return xnthread_test_flags(&rtai_current_task()->thread_base,XNBREAK) ? -EINTR : 0;
}

/**
 * @fn int rt_task_sleep_until(RTIME date)
 * @brief Delay the calling task (absolute).
 *
 * Delay the execution of the calling task until a given date is
 * reached.
 *
 * @param delay The absolute date in clock ticks to wait before
 * resuming the task (see note). Passing an already elapsed date
 * causes the task to return immediately with no delay.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * sleeping task before the sleep time has elapsed.
 *
 * - -ETIMEDOUT is returned if @a date has already elapsed.
 *
 * Side-effect: This routine calls the rescheduling procedure unless
 * an already elapsed date is given.
 *
 * Context: This routine can be called on behalf of a task.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

int rt_task_sleep_until (RTIME date)

{
    int err = 0;
    RTIME now;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    /* Calling the suspension service on behalf of the current task
       implicitely calls the rescheduling procedure. */

    now = xnpod_get_time();

    if (date > now)
	{
	xnpod_suspend_thread(&rtai_current_task()->thread_base,
			     XNDELAY,
			     date - now,
			     NULL);

	if (xnthread_test_flags(&rtai_current_task()->thread_base,XNBREAK))
	    err = -EINTR;
	}
    else
	err = -ETIMEDOUT;

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_unblock(RT_TASK *task)
 * @brief Unblock a real-time task.
 *
 * Break the task out of any wait it is currently in.  This call
 * clears all delay and/or resource wait condition for the target
 * task. However, rt_task_unblock() does not resume a task which has
 * been forcibly suspended by a previous call to rt_task_suspend().
 * If all suspensive conditions are gone, the task becomes eligible
 * anew for scheduling.
 *
 * @param task The descriptor address of the affected task.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Side-effects: This service calls the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

int rt_task_unblock (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    xnpod_unblock_thread(&task->thread_base);

    xnpod_schedule();

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_inquire(RT_TASK *task,
                           RT_TASK_INFO *info)
 * @brief Inquire about a real-time task status.
 *
 * Return various information about the status of a specified task.
 *
 * @param task The descriptor address of the inquired task. If @a task
 * is NULL, the current task is inquired.
 *
 * @param info The address of a structure the task information will be
 * written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor, or if @a
 * task is NULL but not called from a task context.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Context: This routine can always be called on behalf of a task. It
 * can also be called on behalf of an interrupt context or from the
 * initialization code provided @a task is non-NULL.
 */

int rt_task_inquire (RT_TASK *task, RT_TASK_INFO *info)

{
    int err = 0;
    spl_t s;

    if (!task)
	{
	if (xnpod_asynch_p() || xnpod_root_p())
	    return -EINVAL;

	task = rtai_current_task();
	}

    splhigh(s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    strcpy(info->name,xnthread_name(&task->thread_base));
    info->bprio = xnprio2rt(xnthread_base_priority(&task->thread_base));
    info->cprio = xnprio2rt(xnthread_current_priority(&task->thread_base));
    info->status = xnthread_status_flags(&task->thread_base) & RT_TASK_STATUS_MASK;
    info->relpoint = xntimer_get_date(&task->timer);

 unlock_and_exit:

    splexit(s);

    return err;
}

/**
 * @fn int rt_task_add_hook(int type,
                            void (*routine)(RT_TASK *));
 * @brief Install a task hook.
 *
 * The real-time kernel allows to register user-defined routines which
 * get called whenever a specific scheduling event occurs. Multiple
 * hooks can be chained for a single event type, and get called on a
 * FIFO basis.
 *
 * The scheduling is locked while a hook is executing.
 *
 * @param type Defines the kind of hook to install:
 *
 *        - RT_HOOK_TSTART: The user-defined routine will be called on
 *        behalf of the starter task whenever a new task starts. An
 *        opaque cookie is passed to the routine which can use it to
 *        retrieve the descriptor address of the started task through
 *        the RT_HOOK_TASKPTR() macro.
 *
 *        - RT_HOOK_TDELETE: The user-defined routine will be called
 *        on behalf of the deletor task whenever a task is deleted. An
 *        opaque cookie is passed to the routine which can use it to
 *        retrieve the descriptor address of the deleted task through
 *        the RT_HOOK_TASKPTR() macro.
 *
 *        - RT_HOOK_TSWITCH: The user-defined routine will be called
 *        on behalf of the resuming task whenever a context switch
 *        takes place. An opaque cookie is passed to the routine which
 *        can use it to retrieve the descriptor address of the task
 *        which has been switched in through the RT_HOOK_TASKPTR()
 *        macro.
 *
 * @param routine The address of the user-supplied routine to call.
 *
 * @return 0 is returned upon success. Otherwise, one of the following
 * error codes indicates the cause of the failure:
 *
 *         - -EINVAL is returned if @a type is incorrect.
 *
 *         - -ENOMEM is returned if not enough memory is available
 *         from the system heap to add the new hook.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

int rt_task_add_hook (int type, void (*routine)(void *cookie)) {

    return xnpod_add_hook(type,(void (*)(xnthread_t *))routine);
}

EXPORT_SYMBOL(rt_task_create);
EXPORT_SYMBOL(rt_task_start);
EXPORT_SYMBOL(rt_task_suspend);
EXPORT_SYMBOL(rt_task_resume);
EXPORT_SYMBOL(rt_task_delete);
EXPORT_SYMBOL(rt_task_yield);
EXPORT_SYMBOL(rt_task_set_periodic);
EXPORT_SYMBOL(rt_task_wait_period);
EXPORT_SYMBOL(rt_task_set_priority);
EXPORT_SYMBOL(rt_task_sleep);
EXPORT_SYMBOL(rt_task_sleep_until);
EXPORT_SYMBOL(rt_task_unblock);
EXPORT_SYMBOL(rt_task_inquire);
EXPORT_SYMBOL(rt_task_add_hook);
