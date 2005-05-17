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
 * \ingroup task
 */

/*!
 * \ingroup native
 * \defgroup task Task management services.
 *
 * RTAI provides a set of multitasking mechanisms. The basic process
 * object performing actions in RTAI is a task, a logically complete
 * path of application code. Each RTAI task is an independent portion
 * of the overall application code embodied in a C procedure, which
 * executes on its own stack context.
 *
 * The RTAI scheduler ensures that concurrent tasks are run according
 * to one of the supported scheduling policies. Currently, the RTAI
 * scheduler supports fixed priority-based FIFO and round-robin
 * policies.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <rtai/task.h>
#include <rtai/registry.h>

static DECLARE_XNQUEUE(__rtai_task_q);

static void __task_delete_hook (xnthread_t *thread)

{
    /* The scheduler is locked while hooks are running. */
    RT_TASK *task;

    if (xnthread_get_magic(thread) != RTAI_SKIN_MAGIC)
	return;

    task = thread2rtask(thread);

#ifdef CONFIG_RTAI_OPT_NATIVE_MPS
    /* The nucleus will reschedule as needed when all the deletion
       hooks are done. */
    xnsynch_destroy(&task->mrecv);
    xnsynch_destroy(&task->msendq);
#endif /* CONFIG_RTAI_OPT_NATIVE_MPS */

#ifdef CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (task->handle)
	rt_registry_remove(task->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    removeq(&__rtai_task_q,&task->link);

    rtai_mark_deleted(task);

    if (xnthread_test_flags(&task->thread_base,XNSHADOW))
	xnfree(task);
}

int __task_pkg_init (void)

{
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
 * Creates a real-time task, either running in a kernel module or in
 * user-space depending on the caller's context.
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
 * substituted. This parameter is ignored for user-space tasks.
 *
 * @param prio The base priority of the new thread. This value must
 * range from [1 .. 99] (inclusive) where 1 is the lowest effective
 * priority.
 *
 * @param mode The task creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new task:
 *
 * - T_FPU allows the task to use the FPU whenever available on the
 * platform. This flag is forced for user-space tasks.
 *
 * - T_SUSP causes the task to start in suspended mode. In such a
 * case, the thread will have to be explicitely resumed using the
 * rt_task_resume() service for its execution to actually begin.
 *
 * - T_CPU(cpuid) makes the new task affine to CPU # @b cpuid. CPU
 * identifiers range from 0 to RTHAL_NR_CPUS - 1 (inclusive).
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

int rt_task_create (RT_TASK *task,
		    const char *name,
		    int stksize,
		    int prio,
		    int mode)
{
    int err = 0, cpumask, cpu;
    xnflags_t bflags;
    spl_t s;

    if (prio < T_LOPRIO || prio > T_HIPRIO)
	return -EINVAL;

    if (xnpod_asynch_p())
	return -EPERM;

    bflags = mode & (XNFPU|XNSHADOW|XNSHIELD|XNSUSP);

    if (xnpod_init_thread(&task->thread_base,
			  name,
			  prio,
			  bflags,
			  stksize) != 0)
	/* Assume this is the only possible failure. */
	return -ENOMEM;

    xnthread_set_magic(&task->thread_base,RTAI_SKIN_MAGIC);

    inith(&task->link);
    task->suspend_depth = (bflags & XNSUSP) ? 1 : 0;
    task->overrun = -1;
    task->handle = 0;	/* i.e. (still) unregistered task. */

    xnarch_cpus_clear(task->affinity);

    for (cpu = 0, cpumask = (mode >> 24) & 0xff;
	 cpumask != 0 && cpu < 8; cpu++, cpumask >>= 1)
	if (cpumask & 1)
	    xnarch_cpu_set(cpu,task->affinity);

#ifdef CONFIG_RTAI_OPT_NATIVE_MPS
    xnsynch_init(&task->mrecv,XNSYNCH_FIFO);
    xnsynch_init(&task->msendq,XNSYNCH_PRIO|XNSYNCH_PIP);
    xnsynch_set_owner(&task->msendq,&task->thread_base);
    task->flowgen = 0;
#endif /* CONFIG_RTAI_OPT_NATIVE_MPS */

    xnlock_get_irqsave(&nklock,s);
    task->magic = RTAI_TASK_MAGIC;
    appendq(&__rtai_task_q,&task->link);
    xnlock_put_irqrestore(&nklock,s);

#ifdef CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
	{
	err = rt_registry_enter(xnthread_name(&task->thread_base),
				task,
				&task->handle,
				NULL);
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
 * The TSTART hooks are called on behalf of the calling context (if
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

int rt_task_start (RT_TASK *task,
		   void (*entry)(void *cookie),
		   void *cookie)
{
    int err = 0;
    spl_t s;

    if (xnpod_asynch_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

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
		       task->affinity,
		       entry,
		       cookie);
 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_task_suspend(RT_TASK *task)
 * @brief Suspend a real-time task.
 *
 * Forcibly suspend the execution of a task. This task will not be
 * eligible for scheduling until it is explicitly resumed by a call to
 * rt_task_resume(). In other words, the suspended state caused by a
 * call to rt_task_suspend() is cumulative with respect to the delayed
 * and blocked states caused by other services, and is managed
 * separately from them.
 *
 * A nesting count is maintained so that rt_task_suspend() and
 * rt_task_resume() must be used in pairs.
 *
 * @param task The descriptor address of the affected task. If @a task
 * is NULL, the current task is suspended.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EPERM is returned if @a task is NULL but not called from a task
 * context, or this service was called from a context which cannot
 * sleep (e.g. interrupt, non-realtime or scheduler locked).
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a task is non-NULL.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always if @a task is NULL.
 */

int rt_task_suspend (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    if (!task)
	{
	if (!xnpod_primary_p())
	    return -EPERM;

	task = rtai_current_task();
	}

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}

    if (xnpod_unblockable_p())
	{
	err = -EPERM;
	goto unlock_and_exit;
	}

    if (task->suspend_depth++ == 0)
	xnpod_suspend_thread(&task->thread_base,
			     XNSUSP,
			     XN_INFINITE,
			     NULL);
 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

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
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if the suspension nesting level falls down
 * to zero as a result of the current invocation.
 */

int rt_task_resume (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

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

    xnlock_put_irqrestore(&nklock,s);

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
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EPERM is returned if @a task is NULL but not called from a task
 * context, or this service was called from an asynchronous context.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 *   only if @a task is non-NULL.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always if @a task is NULL.
 */

int rt_task_delete (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    if (!task)
	{
	if (!xnpod_primary_p())
	    return -EPERM;

	task = rtai_current_task();
	}
    else if (xnpod_asynch_p())
	    return -EPERM;

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    /* Does not return if task is current. */
    xnpod_delete_thread(&task->thread_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_task_yield(void)
 * @brief Manual round-robin.
 *
 * Move the current task to the end of its priority group, so that the
 * next equal-priority task in ready state is switched in.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EPERM is returned if this service was called from a context
 * which cannot sleep (e.g. interrupt, non-realtime or scheduler
 * locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: always if a next equal-priority task is ready to run,
 * otherwise, this service leads to a no-op.
 */

int rt_task_yield (void)

{
    if (xnpod_unblockable_p())
	return -EPERM;

    xnpod_yield();

    return 0;
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
 * be delayed until this point is reached. If @a idate is equal to
 * TM_NOW, the current system date is used, and no initial delay takes
 * place.

 * @param period The period of the task, expressed in clock ticks (see
 * note). Passing TM_INFINITE attempts to stop the task's periodic
 * timer; in the latter case, the routine always exits succesfully,
 * regardless of the previous state of this timer.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * - -ETIMEDOUT is returned if @a idate is different from TM_INFINITE
 * and represents a date in the past.
 *
 * - -EWOULDBLOCK is returned if the system timer has not been started
 * using rt_timer_start().
 *
 * - -EPERM is returned if @a task is NULL but not called from a task
 * context.

 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code or interrupt only if @a
 * task is non-NULL.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always if the operation affects the current task and
 * @a idate has not elapsed yet.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

int rt_task_set_periodic (RT_TASK *task,
			  RTIME idate,
			  RTIME period)
{
    int err;
    spl_t s;

    if (!task)
	{
	if (!xnpod_primary_p())
	    return -EPERM;

	task = rtai_current_task();
	}

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}

    err = xnpod_set_thread_periodic(&task->thread_base,idate,period);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

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
 * - -EPERM is returned if this service was called from a context
 * which cannot sleep (e.g. interrupt, non-realtime or scheduler
 * locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless an overrun has been detected.  In the
 * latter case, the current task immediately returns from this service
 * without being delayed.
 */

int rt_task_wait_period (void)

{
    if (xnpod_unblockable_p())
	return -EPERM;

    return xnpod_wait_thread_period();
}

/**
 * @fn int rt_task_set_priority(RT_TASK *task,
                                int prio)
 * @brief Change the base priority of a real-time task.
 *
 * Changing the base priority of a task does not affect the priority
 * boost the target task might have obtained as a consequence of a
 * previous priority inheritance.
 *
 * @param task The descriptor address of the affected task.
 *
 * @param prio The new task priority. This value must range from [1
 * .. 99] (inclusive) where 1 is the lowest effective priority.

 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor, or if @a
 * prio is invalid.
 *
 * - -EPERM is returned if @a task is NULL but not called from a task
 * context.
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
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a task is non-NULL.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if @a task is the current one.
 */

int rt_task_set_priority (RT_TASK *task,
			  int prio)
{
    int oldprio;
    spl_t s;

    if (prio < T_LOPRIO || prio > T_HIPRIO)
	return -EINVAL;

    if (!task)
	{
	if (!xnpod_primary_p())
	    return -EPERM;

	task = rtai_current_task();
	}

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	oldprio = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    oldprio = xnthread_base_priority(&task->thread_base);

    xnpod_renice_thread(&task->thread_base,prio);

    xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

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
 * - -EWOULDBLOCK is returned if the system timer is inactive.
 *
 * - -EPERM is returned if this service was called from a context
 * which cannot sleep (e.g. interrupt, non-realtime or scheduler
 * locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless a null delay is given.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

int rt_task_sleep (RTIME delay)

{
    if (xnpod_unblockable_p())
	return -EPERM;

    if (delay == 0)
	return 0;

    if (!testbits(nkpod->status,XNTIMED))
	return -EWOULDBLOCK;

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
 * @param date The absolute date in clock ticks to wait before
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
 * - -EWOULDBLOCK is returned if the system timer is inactive.
 *
 * - -EPERM is returned if this service was called from a context
 * which cannot sleep (e.g. interrupt, non-realtime or scheduler
 * locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless a date in the past is given.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

int rt_task_sleep_until (RTIME date)

{
    int err = 0;
    RTIME now;
    spl_t s;

    if (xnpod_unblockable_p())
	return -EPERM;

    if (!testbits(nkpod->status,XNTIMED))
	return -EWOULDBLOCK;

    xnlock_get_irqsave(&nklock,s);

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

    xnlock_put_irqrestore(&nklock,s);

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

int rt_task_unblock (RT_TASK *task)

{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    xnpod_unblock_thread(&task->thread_base);

    xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_task_inquire(RT_TASK *task, RT_TASK_INFO *info)
 * @brief Inquire about a real-time task.
 *
 * Return various information about the status of a given task.
 *
 * @param task The descriptor address of the inquired task. If @a task
 * is NULL, the current task is inquired.
 *
 * @param info The address of a structure the task information will be
 * written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EPERM is returned if @a task is NULL but not called from a task
 * context.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a task is non-NULL.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_task_inquire (RT_TASK *task, RT_TASK_INFO *info)

{
    int err = 0;
    spl_t s;

    if (!task)
	{
	if (!xnpod_primary_p())
	    return -EPERM;

	task = rtai_current_task();
	}

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    strcpy(info->name,xnthread_name(&task->thread_base));
    info->bprio = xnthread_base_priority(&task->thread_base);
    info->cprio = xnthread_current_priority(&task->thread_base);
    info->status = xnthread_status_flags(&task->thread_base) & RT_TASK_STATUS_MASK;
    info->relpoint = xntimer_get_date(&task->timer);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_task_add_hook(int type, void (*routine)(void *cookie));
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
 * - T_HOOK_START: The user-defined routine will be called on behalf
 * of the starter task whenever a new task starts. An opaque cookie is
 * passed to the routine which can use it to retrieve the descriptor
 * address of the started task through the T_DESC() macro.
 *
 * - T_HOOK_DELETE: The user-defined routine will be called on behalf
 * of the deletor task whenever a task is deleted. An opaque cookie is
 * passed to the routine which can use it to retrieve the descriptor
 * address of the deleted task through the T_DESC() macro.
 *
 * - T_HOOK_SWITCH: The user-defined routine will be called on behalf
 * of the resuming task whenever a context switch takes place. An
 * opaque cookie is passed to the routine which can use it to retrieve
 * the descriptor address of the task which has been switched in
 * through the T_DESC() macro.
 *
 * @param routine The address of the user-supplied routine to call.
 *
 * @return 0 is returned upon success. Otherwise, one of the following
 * error codes indicates the cause of the failure:
 *
 * - -EINVAL is returned if @a type is incorrect.
 *
 * - -ENOMEM is returned if not enough memory is available from the
 * system heap to add the new hook.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int rt_task_add_hook (int type, void (*routine)(void *cookie)) {

    return xnpod_add_hook(type,(void (*)(xnthread_t *))routine);
}

/**
 * @fn int rt_task_remove_hook(int type, void (*routine)(void *cookie));
 * @brief Remove a task hook.
 *
 * This service allows to remove a task hook previously registered
 * using rt_task_add_hook().
 *
 * @param type Defines the kind of hook to uninstall. Possible values
 * are:
 *
 * - T_HOOK_START
 * - T_HOOK_DELETE
 * - T_HOOK_SWITCH
 *
 * @param routine The address of the user-supplied routine to remove
 * from the hook list.
 *
 * @return 0 is returned upon success. Otherwise, one of the following
 * error codes indicates the cause of the failure:
 *
 * - -EINVAL is returned if @a type is incorrect.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int rt_task_remove_hook (int type, void (*routine)(void *cookie)) {

    return xnpod_remove_hook(type,(void (*)(xnthread_t *))routine);
}

/**
 * @fn int rt_task_catch(void (*handler)(rt_sigset_t))
 * @brief Install a signal handler.
 *
 * This service installs a signal handler for the current
 * task. Signals are discrete events tasks can receive each time they
 * resume execution. When signals are pending upon resumption, @a
 * handler is fired to process them. Signals can be sent using
 * rt_task_notify(). A task can block the signal delivery by passing
 * the T_NOSIG bit to rt_task_set_mode().
 *
 * Calling this service implicitely unblocks the signal delivery for
 * the caller.
 *
 * @param handler The address of the user-supplied routine to fire
 * when signals are pending for the task. This handler is passed the
 * set of pending signals as its first and only argument.
 *
 * @return 0 upon success, or:
 *
 * - -EPERM is returned if this service was not called from a
 * real-time task context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_task_catch (void (*handler)(rt_sigset_t))

{
    spl_t s;

    if (!xnpod_primary_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);
    rtai_current_task()->thread_base.asr = (xnasr_t)handler;
    rtai_current_task()->thread_base.asrmode &= ~XNASDI;
    rtai_current_task()->thread_base.asrimask = 0;
    xnlock_put_irqrestore(&nklock,s);

    /* The rescheduling procedure checks for pending signals. */
    xnpod_schedule();

    return 0;
}

/**
 * @fn int rt_task_notify(RT_TASK *task,
                          rt_sigset_t signals)
 * @brief Send signals to a task.
 *
 * This service sends a set of signals to a given task.  A task can
 * install a signal handler using the rt_task_catch() service to
 * process them.
 *
 * @param task The descriptor address of the affected task which must
 * have been previously created by the rt_task_create() service.
 *
 * @param signals The set of signals to make pending for the
 * task. This set is OR'ed with the current set of pending signals for
 * the task; there is no count of occurence maintained for each
 * available signal, which is either pending or cleared.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor.
 *
 * - -EPERM is returned if @a task is NULL but not called from a
 * real-time task context.
 *
 * - -EIDRM is returned if @a task is a deleted task descriptor.
 *
 * - -ESRCH is returned if @a task has not set any signal handler.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a task is non-NULL.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_task_notify (RT_TASK *task,
		    rt_sigset_t signals)
{
    int err = 0;
    spl_t s;

    if (!task)
	{
	if (!xnpod_primary_p())
	    return -EPERM;

	task = rtai_current_task();
	}

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    if (task->thread_base.asr == RT_HANDLER_NONE)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    if (signals > 0)
	{
	task->thread_base.signals |= signals;

	if (xnpod_current_thread() == &task->thread_base)
	    xnpod_schedule();
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_task_set_mode(int setmask,
                            int clrmask,
			    int *mode_r)
 * @brief Change task mode bits.
 *
 * Each RTAI task has a set of internal bits determining various
 * operating conditions; the rt_task_set_mode() service allows to
 * alter three of them, respectively controlling:
 *
 * - whether the task locks the rescheduling procedure,
 * - whether the task undergoes a round-robin scheduling,
 * - whether the task blocks the delivery of signals.
 *
 * To this end, rt_task_set_mode() takes a bitmask of mode bits to
 * clear for disabling the corresponding modes, and another one to set
 * for enabling them. The mode bits which were previously in effect
 * can be returned upon request.
 *
 * The following bits can be part of the bitmask:
 *
 * - T_LOCK causes the current task to lock the scheduler. Clearing
 * this bit unlocks the scheduler.
 *
 * - T_RRB causes the current task to be marked as undergoing the
 * round-robin scheduling policy. If the task is already undergoing
 * the round-robin scheduling policy at the time this service is
 * called, the time quantum remains unchanged.
 *
 * - T_NOSIG disables the asynchronous signal delivery for the current
 * task.
 *
 * - T_SHIELD enables the interrupt shield for the current user-space
 * task. When engaged, the interrupt shield protects the RTAI task
 * running in secondary mode from any preemption by the regular Linux
 * interrupt handlers, without delaying in any way the RTAI interrupt
 * handling. The shield is operated on a per-task basis at each
 * context switch, depending on the setting of this bit. This bit is
 * set by default upon user-space task creation.
 *
 * - When set, T_WARNSW causes the SIGXCPU signal to be sent to the
 * current user-space task whenever it switches to the secondary
 * mode. This feature is useful to detect unwanted migrations to the
 * Linux domain.
 *
 * - T_PRIMARY can be passed to switch the current user-space task to
 * primary mode (setmask |= T_PRIMARY), or secondary mode (clrmask |=
 * T_PRIMARY). Upon return from rt_task_set_mode(), the user-space
 * task will run into the specified domain.
 *
 * Normally, this service can only be called on behalf of a regular
 * real-time task, either running in kernel or user-space. However, as
 * a special exception, requests for setting/clearing the T_LOCK bit
 * from asynchronous contexts are silently dropped, and the call
 * returns successfully if no other mode bits have been
 * specified. This is consistent with the fact that RTAI enforces a
 * scheduler lock until the outer interrupt handler has returned.
 *
 * @param clrmask A bitmask of mode bits to clear for the current
 * task, before @a setmask is applied. 0 is an acceptable value which
 * leads to a no-op.
 *
 * @param setmask A bitmask of mode bits to set for the current
 * task. 0 is an acceptable value which leads to a no-op.
 *
 * @param mode_r If non-NULL, @a mode_r must be a pointer to a memory
 * location which will be written upon success with the previous set
 * of active mode bits. If NULL, the previous set of active mode bits
 * will not be returned.
 *
 * @return 0 is returned upon success, or:
 *
 * - -EINVAL if either @a setmask or @a clrmask specifies invalid
 * bits. T_PRIMARY is invalid for kernel-based tasks.
 *
 * - -EPERM is returned if this service was not called from a
 * real-time task context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible, if T_LOCK has been passed into @a clrmask
 * and the calling context is a task.
 */

int rt_task_set_mode (int clrmask,
		      int setmask,
		      int *mode_r)
{
    int mode;

    if (xnpod_asynch_p())
	{
	clrmask &= ~T_LOCK;
	setmask &= ~T_LOCK;

	if (!clrmask && !setmask)
	    return 0;
	}

    if (((clrmask|setmask) & ~(T_LOCK|T_RRB|T_NOSIG|T_SHIELD|T_WARNSW)) != 0)
	return -EINVAL;

    if (!xnpod_primary_p())
	return -EPERM;

    mode = xnpod_set_thread_mode(&rtai_current_task()->thread_base,
				 clrmask,
				 setmask);
    if (mode_r)
	*mode_r = mode;

    if ((clrmask & ~setmask) & T_LOCK)
	/* Reschedule if the scheduler has been unlocked. */
	xnpod_schedule();

    return 0;
}

/**
 * @fn RT_TASK *rt_task_self(void)
 * @brief Retrieve the current task.
 *
 * Return the current task descriptor address.
 *
 * @return The address of the caller's task descriptor is returned
 * upon success, or NULL if the calling context is asynchronous
 * (i.e. not a RTAI task).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * Those will cause a NULL return.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

RT_TASK *rt_task_self (void)

{
    return xnpod_asynch_p() ? NULL : rtai_current_task();
}

/**
 * @fn int rt_task_slice(RT_TASK *task, RTIME quantum)
 * @brief Set a task's round-robin quantum.
 *
 * Set the time credit allotted to a task undergoing the round-robin
 * scheduling. As a side-effect, rt_task_slice() refills the current
 * quantum of the target task.
 *
 * @param task The descriptor address of the affected task. If @a task
 * is NULL, the current task is considered.
 *
 * @param quantum The round-robin quantum for the task expressed in
 * clock ticks (see note).
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a task is not a task descriptor, or if @a
 * quantum is zero.
 *
 * - -EPERM is returned if @a task is NULL but not called from a task
 * context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * only if @a task is non-NULL.
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

int rt_task_slice (RT_TASK *task, RTIME quantum)

{
    int err = 0;
    spl_t s;

    if (!quantum)
	return -EINVAL;

    if (!task)
	{
	if (!xnpod_primary_p())
	    return -EPERM;

	task = rtai_current_task();
	}

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}
    
    xnthread_time_slice(&task->thread_base) = quantum;
    xnthread_time_credit(&task->thread_base) = quantum;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

#ifdef CONFIG_RTAI_OPT_NATIVE_MPS

/**
 * @fn int rt_task_send(RT_TASK *task,
                        RT_TASK_MCB *mcb_s,
                        RT_TASK_MCB *mcb_r,
                        RTIME timeout)
 * @brief Send a message to a task.
 *
 * This service is part of the synchronous message passing support
 * available to RTAI tasks. It allows the caller to send a
 * variable-sized message to another task, waiting for the remote to
 * receive the initial message by a call to rt_task_receive(), then
 * reply to it using rt_task_reply().
 *
 * A basic message control block is used to store the location and
 * size of the data area to send or retrieve upon reply, in addition
 * to a user-defined operation code.
 *
 * @param task The descriptor address of the recipient task.
 *
 * @param mcb_s The address of the message control block referring to
 * the message to be sent. The fields from this control block should
 * be set as follows:
 *
 * - mcb_s->data should contain the address of the payload data to
 * send to the remote task.
 *
 * - mcb_s->size should contain the size in bytes of the payload data
 * pointed at by mcb_s->data. 0 is a legitimate value, and indicates
 * that no payload data will be transferred. In the latter case,
 * mcb_s->data will be ignored. See note.
 *
 * - mcb_s->opcode is an opaque operation code carried during the
 * message transfer the caller can fill with any appropriate value. It
 * will be made available "as is" to the remote task into the
 * operation code field by the rt_task_receive() service.
 *
 * @param mcb_r The address of an optional message control block
 * referring to the reply message area. If @a mcb_r is NULL and a
 * reply is sent back by the remote task, the reply message will be
 * discarded, and -ENOSPC will be returned to the caller. When @a
 * mcb_r is valid, the fields from this control block should be set as
 * follows:
 *
 * - mcb_r->data should contain the address of a buffer large enough
 * to collect the reply data from the remote task.
 *
 * - mcb_r->size should contain the size in bytes of the buffer space
 * pointed at by mcb_r->data. If mcb_r->size is lower than the actual
 * size of the reply message, no data copy takes place and -ENOSPC is
 * returned to the caller. See note.
 *
 * Upon return, mcb_r->opcode will contain the status code sent back
 * from the remote task using rt_task_reply(), or 0 if unspecified.
 *
 * @param timeout The number of clock ticks to wait for the remote
 * task to reply to the initial message (see note). Passing
 * TM_INFINITE causes the caller to block indefinitely until the
 * remote task eventually replies. Passing TM_NONBLOCK causes the
 * service to return immediately without waiting if the remote task is
 * not waiting for messages (i.e. if @a task is not currently blocked
 * on the rt_task_receive() service).
 *
 * @return A positive value is returned upon success, representing the
 * length (in bytes) of the reply message returned by the remote
 * task. 0 is a success status, meaning either that @a mcb_r was NULL
 * on entry, or that no actual message was passed to the remote call
 * to rt_task_reply(). Otherwise:
 *
 * - -ENOSPC is returned if @a mcb_r does not point at a message area
 * large enough to collect the remote task's reply. This includes the
 * case where @a mcb_r is NULL on entry albeit the remote task
 * attempts to send a reply message.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and @a task is not currently blocked on the rt_task_receive()
 * service.
 *
 * - -EIDRM is returned if @a task has been deleted while waiting for
 * a reply.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * caller before any reply was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime or
 * scheduler locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: Always.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 *
 * @note When called from a user-space task, this service may need to
 * allocate some temporary buffer space from the system heap to hold
 * both the sent and the reply data if this cumulated size exceeds a
 * certain amount; the threshold before allocation is currently set to
 * 64 bytes.
 */

ssize_t rt_task_send (RT_TASK *task,
		      RT_TASK_MCB *mcb_s,
		      RT_TASK_MCB *mcb_r,
		      RTIME timeout)
{
    RT_TASK *sender;
    size_t rsize;
    ssize_t err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    task = rtai_h2obj_validate(task,RTAI_TASK_MAGIC,RT_TASK);

    if (!task)
	{
	err = rtai_handle_error(task,RTAI_TASK_MAGIC,RT_TASK);
	goto unlock_and_exit;
	}

    if (timeout == TM_NONBLOCK && xnsynch_nsleepers(&task->mrecv) == 0)
	{
	/* Can't block and no receiver pending; just bail out. */
	err = -EWOULDBLOCK;
	goto unlock_and_exit;
	}

    if (xnpod_unblockable_p())
	{
	err = -EPERM;
	goto unlock_and_exit;
	}

    sender = rtai_current_task();

    /* First, setup the send control block. Compute the flow
       identifier, making sure that we won't draw a null or negative
       value. */

    if (++task->flowgen < 0)
	task->flowgen = 1;

    mcb_s->flowid = task->flowgen;

    sender->wait_args.mps.mcb_s = *mcb_s;

    /* Then, setup the reply control block. */

    if (mcb_r)
	sender->wait_args.mps.mcb_r = *mcb_r;
    else
	sender->wait_args.mps.mcb_r.size = 0;

    /* Wake up the receiver if it is currently waiting for a message,
       then sleep on the send queue, waiting for the remote
       reply. xnsynch_sleep_on() will reschedule as needed. */

    xnsynch_flush(&task->mrecv,0);

    /* Since the receiver is perpetually marked as the current owner
       of its own send queue which has been declared as a PIP-enabled
       object, it will inherit the priority of the sender in the case
       required by the priority inheritance protocol
       (i.e. prio(sender) > prio(receiver)). */

    xnsynch_sleep_on(&task->msendq,timeout);

    if (xnthread_test_flags(&sender->thread_base,XNRMID))
	err = -EIDRM; /* Receiver deleted while pending. */
    else if (xnthread_test_flags(&sender->thread_base,XNTIMEO))
	err = -ETIMEDOUT; /* Timeout.*/
    else if (xnthread_test_flags(&sender->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/
    else
	{
	rsize = sender->wait_args.mps.mcb_r.size;

	if (rsize > 0)
	    {
	    /* Ok, the message has been processed and answered by the
	       remote, and a memory address is available to pass the
	       reply back to our caller: let's do it. Make sure we
	       have enough buffer space to perform the entire copy. */

	    if (mcb_r != NULL && rsize <= mcb_r->size)
		{
		memcpy(mcb_r->data,sender->wait_args.mps.mcb_r.data,rsize);
		err = (ssize_t)rsize;
		}
	    else
		err = -ENOSPC;
	    }
	else
	    err = 0; /* i.e. message processed, no reply expected or sent. */

	/* The status code is considered meaningful whether there is
	   some actual reply data or not; recycle the opcode field to
	   return it. */

	if (mcb_r)
	    {
	    mcb_r->opcode = sender->wait_args.mps.mcb_r.opcode;
	    mcb_r->size = rsize;
	    }
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_task_receive(RT_TASK_MCB *mcb_r,
                           RTIME timeout)
 * @brief Receive a message from a task.
 *
 * This service is part of the synchronous message passing support
 * available to RTAI tasks. It allows the caller to receive a
 * variable-sized message sent from another task using the
 * rt_task_send() service. The sending task is blocked until the
 * caller invokes rt_task_reply() to finish the transaction.
 *
 * A basic message control block is used to store the location and
 * size of the data area to receive from the sender, in addition to a
 * user-defined operation code.
 *
 * @param mcb_r The address of a message control block referring to
 * the receive message area. The fields from this control block should
 * be set as follows:
 *
 * - mcb_r->data should contain the address of a buffer large enough
 * to collect the data sent by the remote task;
 *
 * - mcb_r->size should contain the size in bytes of the buffer space
 * pointed at by mcb_r->data. If mcb_r->size is lower than the actual
 * size of the received message, no data copy takes place and -ENOSPC
 * is returned to the caller. See note.
 *
 * Upon return, mcb_r->opcode will contain the operation code sent
 * from the remote task using rt_task_send().
 *
 * @param timeout The number of clock ticks to wait for receiving a
 * message (see note). Passing TM_INFINITE causes the caller to block
 * indefinitely until a remote task eventually sends a message.
 * Passing TM_NONBLOCK causes the service to return immediately
 * without waiting if no remote task is currently waiting for sending
 * a message.
 *
 * @return A strictly positive value is returned upon success,
 * representing a flow identifier for the opening transaction; this
 * token should be passed to rt_task_reply(), in order to send back a
 * reply to and unblock the remote task appropriately. Otherwise:
 *
 * - -ENOSPC is returned if @a mcb_r does not point at a message area
 * large enough to collect the remote task's message.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no remote task is currently waiting for sending a message to
 * the caller.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * caller before any message was available.
 *
 * - -EPERM is returned if this service was called from a context
 * which cannot sleep (e.g. interrupt, non-realtime or scheduler
 * locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: Always.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 *
 * @note When called from a user-space task, this service may need to
 * allocate some temporary buffer space from the system heap to hold
 * the received data if the size of the latter exceeds a certain
 * amount; the threshold before allocation is currently set to 64
 * bytes.
 */

int rt_task_receive (RT_TASK_MCB *mcb_r,
		     RTIME timeout)
{
    RT_TASK *receiver, *sender;
    xnpholder_t *holder;
    size_t rsize;
    int err;
    spl_t s;

    if (!xnpod_primary_p())
	return -EPERM;

    receiver = rtai_current_task();

    xnlock_get_irqsave(&nklock,s);

    /* Fetch the first available message, but don't wake up the sender
       until our caller invokes rt_task_reply(). IOW,
       rt_task_receive() will fetch back the exact same message until
       rt_task_reply() is called to release the heading sender. */
    holder = getheadpq(xnsynch_wait_queue(&receiver->msendq));

    if (holder)
	goto pull_message;

    if (timeout == TM_NONBLOCK)
	{
	err = -EWOULDBLOCK;
	goto unlock_and_exit;
	}

    if (xnpod_unblockable_p())
	{
	err = -EPERM;
	goto unlock_and_exit;
	}

    /* Wait on our receive slot for some sender to enqueue itself in
       our send queue. */

    xnsynch_sleep_on(&receiver->mrecv,timeout);

    /* XNRMID cannot happen, since well, the current task would be the
       deleted object, so... */

    if (xnthread_test_flags(&receiver->thread_base,XNTIMEO))
	{
	err = -ETIMEDOUT; /* Timeout.*/
	goto unlock_and_exit;
	}
    else if (xnthread_test_flags(&receiver->thread_base,XNBREAK))
	{
	err = -EINTR; /* Unblocked.*/
	goto unlock_and_exit;
	}

    holder = getheadpq(xnsynch_wait_queue(&receiver->msendq));
    /* There must be a valid holder since we waited for it. */

 pull_message:

    sender = thread2rtask(link2thread(holder,plink));

    rsize = sender->wait_args.mps.mcb_s.size;

    if (rsize <= mcb_r->size)
	{
	if (rsize > 0)
	    memcpy(mcb_r->data,sender->wait_args.mps.mcb_s.data,rsize);

	/* The flow identifier can't be either null or negative. */
	err = sender->wait_args.mps.mcb_s.flowid;
	}
    else
	err = -ENOSPC;

    mcb_r->opcode = sender->wait_args.mps.mcb_s.opcode;
    mcb_r->size = rsize;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_task_reply(int flowid,
                         RT_TASK_MCB *mcb_s)
 * @brief Reply to a task.
 *
 * This service is part of the synchronous message passing support
 * available to RTAI tasks. It allows the caller to send back a
 * variable-sized message to the sender task, once the initial message
 * from this task has been pulled using rt_task_receive() and
 * processed. As a consequence of this call, the remote task will be
 * unblocked from the rt_task_send() service.
 *
 * A basic message control block is used to store the location and
 * size of the data area to send back, in addition to a user-defined
 * status code.
 *
 * @param flowid The flow identifier returned by a previous call to
 * rt_task_receive() which uniquely identifies the current
 * transaction.
 *
 * @param mcb_s The address of an optional message control block
 * referring to the message to be sent back. If @a mcb_s is NULL, the
 * sender will be unblocked without getting any reply data. When @a
 * mcb_s is valid, the fields from this control block should be set as
 * follows:
 *
 * - mcb_s->data should contain the address of the payload data to
 * send to the remote task.
 *
 * - mcb_s->size should contain the size in bytes of the payload data
 * pointed at by mcb_s->data. 0 is a legitimate value, and indicates
 * that no payload data will be transferred. In the latter case,
 * mcb_s->data will be ignored. See note.
 *
 * - mcb_s->opcode is an opaque status code carried during the message
 * transfer the caller can fill with any appropriate value. It will be
 * made available "as is" to the remote task into the status code
 * field by the rt_task_send() service. If @a mcb_s is NULL, 0 will be
 * returned to the initial sender into the status code field.
 *
 * @return O is returned upon success. Otherwise:
 *
 * - -ENXIO is returned if @a flowid does not match the expected
 * identifier returned from the latest call of the current task to
 * rt_task_receive(), or if the remote task stopped waiting for the
 * reply in the meantime (e.g. the initial sender could have been
 * deleted or forcibly unblocked).
 *
 * - -EPERM is returned if this service was called from an invalid
 * context (e.g. interrupt, or non-primary).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: Always.
 *
 * @note When called from a user-space task, this service may need to
 * allocate some temporary buffer space from the system heap to hold
 * the reply data if the size of the latter exceeds a certain amount;
 * the threshold before allocation is currently set to 64 bytes.
 */

int rt_task_reply (int flowid, RT_TASK_MCB *mcb_s)

{
    RT_TASK *sender, *receiver;
    xnpholder_t *holder;
    size_t rsize;
    int err;
    spl_t s;

    if (!xnpod_primary_p())
	return -EPERM;

    sender = rtai_current_task();

    xnlock_get_irqsave(&nklock,s);

    for (holder = getheadpq(xnsynch_wait_queue(&sender->msendq));
	 holder != NULL;
	 holder = nextpq(xnsynch_wait_queue(&sender->msendq),holder))
	{
	receiver = thread2rtask(link2thread(holder,plink));

	/* Check the flow identifier, just in case the sender has
	   vanished away while we were processing its last
	   message. Each sent message carries a distinct flow
	   identifier from other senders wrt to a given receiver. */

	if (receiver->wait_args.mps.mcb_s.flowid == flowid)
	    {
	    /* Note that the following will cause the receiver to be
	       unblocked without transferring the ownership of the
	       msendq object, since we want the sender to keep it. */
	    xnpod_resume_thread(&receiver->thread_base,XNPEND);
	    break;
	    }
	}

    if (!holder)
	{
	err = -ENXIO;
	goto unlock_and_exit;
	}

    /* Copy the reply data to a location where the receiver can find
       it. */

    rsize = mcb_s ? mcb_s->size : 0;
    err = 0;

    if (receiver->wait_args.mps.mcb_r.size >= rsize)
	{
	/* Sending back a NULL or zero-length reply is perfectly
	   valid; it just means to unblock the initial sender without
	   passing it back any reply data. */

	if (rsize > 0)
	    memcpy(receiver->wait_args.mps.mcb_r.data,mcb_s->data,rsize);
	}
    else
	/* The receiver will get the same error code. Falldown
	   through the rescheduling is wanted. */
	err = -ENOSPC;

    /* Copy back the actual size of the reply data, */
    receiver->wait_args.mps.mcb_r.size = rsize;
    /* And the status code. */
    receiver->wait_args.mps.mcb_r.opcode = mcb_s ? mcb_s->opcode : 0;

    /* That's it, we just need to start the rescheduling procedure
       now. */

    xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

#endif /* CONFIG_RTAI_OPT_NATIVE_MPS */

/**
 * @fn int rt_task_spawn(RT_TASK *task,
		         const char *name,
			 int stksize,
			 int prio,
			 int mode,
			 void (*entry)(void *cookie),
			 void *cookie)
 * @brief Spawn a new real-time task.
 *
 * Creates and immediately starts a real-time task, either running in
 * a kernel module or in user-space depending on the caller's
 * context. This service is a simple shorthand for rt_task_create()
 * followed by a call to rt_task_start().
 *
 * @param task The address of a task descriptor RTAI will use to store
 * the task-related data.  This descriptor must always be valid while
 * the task is active therefore it must be allocated in permanent
 * memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * task. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created task.
 *
 * @param stksize The size of the stack (in bytes) for the new
 * task. If zero is passed, a reasonable pre-defined size will be
 * substituted. This parameter is ignored for user-space tasks.
 *
 * @param prio The base priority of the new thread. This value must
 * range from [1 .. 99] (inclusive) where 1 is the lowest effective
 * priority.
 *
 * @param mode The task creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new task:
 *
 * - T_FPU allows the task to use the FPU whenever available on the
 * platform. This flag is forced for user-space tasks.
 *
 * - T_SUSP causes the task to start in suspended mode. In such a
 * case, the thread will have to be explicitely resumed using the
 * rt_task_resume() service for its execution to actually begin.
 *
 * - T_CPU(cpuid) makes the new task affine to CPU # @b cpuid. CPU
 * identifiers range from 0 to RTHAL_NR_CPUS - 1 (inclusive).
 *
 * Passing T_FPU|T_CPU(1) in the @a mode parameter thus creates a task
 * with FPU support enabled and which will be affine to CPU #1.
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
 * - -ENOMEM is returned if the system fails to get enough dynamic
 * memory from the global real-time heap in order to create the new
 * task's stack space or register the task.
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

/**
 * @fn int rt_task_bind(RT_TASK *task,
			const char *name)
 * @brief Bind to a real-time task.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given RTAI task identified by its symbolic name. If the task does
 * not exist on entry, this service blocks the caller until a task of
 * the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * task to bind to.
 *
 * @param task The address of a task descriptor retrieved by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EFAULT is returned if @a task or @a name is referencing invalid
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
 * @fn int rt_task_unbind(RT_TASK *task)
 *
 * @brief Unbind from a real-time task.
 *
 * This user-space only service unbinds the calling task from the task
 * object previously retrieved by a call to rt_task_bind().
 *
 * @param task The address of a task descriptor to unbind from.
 *
 * @return 0 is always returned.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

/*@}*/

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
EXPORT_SYMBOL(rt_task_remove_hook);
EXPORT_SYMBOL(rt_task_catch);
EXPORT_SYMBOL(rt_task_notify);
EXPORT_SYMBOL(rt_task_set_mode);
EXPORT_SYMBOL(rt_task_self);
EXPORT_SYMBOL(rt_task_slice);
#ifdef CONFIG_RTAI_OPT_NATIVE_MPS
EXPORT_SYMBOL(rt_task_send);
EXPORT_SYMBOL(rt_task_receive);
EXPORT_SYMBOL(rt_task_reply);
#endif /* CONFIG_RTAI_OPT_NATIVE_MPS */
