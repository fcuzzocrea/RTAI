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
 * \ingroup mutex
 */

/*!
 * \ingroup native
 * \defgroup mutex Mutex services.
 *
 * Mutex services.
 *
 * A mutex is a MUTual EXclusion object, and is useful for protecting
 * shared data structures from concurrent modifications, and
 * implementing critical sections and monitors.
 *
 * A mutex has two possible states: unlocked (not owned by any task),
 * and locked (owned by one task). A mutex can never be owned by two
 * different tasks simultaneously. A task attempting to lock a mutex
 * that is already locked by another task is blocked until the latter
 * unlocks the mutex first.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/mutex.h>
#include <rtai/registry.h>

int __mutex_pkg_init (void)

{
    return 0;
}

void __mutex_pkg_cleanup (void)

{
}

/**
 * @fn int rt_mutex_create(RT_MUTEX *mutex,
                           const char *name)
 * @brief Create a mutex.
 *
 * Create a mutual exclusion object that allows multiple tasks to
 * synchronize access to a shared resource. A mutex is left in an
 * unlocked state after creation.
 *
 * @param mutex The address of a mutex descriptor RTAI will use to
 * store the mutex-related data.  This descriptor must always be valid
 * while the mutex is active therefore it must be allocated in
 * permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * mutex. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created mutex.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
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

int rt_mutex_create (RT_MUTEX *mutex,
		     const char *name)
{
    int err = 0;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnsynch_init(&mutex->synch_base,XNSYNCH_PRIO|XNSYNCH_PIP);
    mutex->handle = 0;  /* i.e. (still) unregistered mutex. */
    mutex->magic = RTAI_MUTEX_MAGIC;
    mutex->owner = NULL;
    mutex->lockcnt = 0;
    xnobject_copy_name(mutex->name,name);

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    mutex->cpid = 0;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(mutex->name,mutex,&mutex->handle);

        if (err)
            rt_mutex_delete(mutex);
        }
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_mutex_delete(RT_MUTEX *mutex)
 * @brief Delete a mutex.
 *
 * Destroy a mutex and release all the tasks currently pending on it.
 * A mutex exists in the system since rt_mutex_create() has been
 * called to create it, so this service must be called in order to
 * destroy it afterwards.
 *
 * @param mutex The descriptor address of the affected mutex.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor.
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

int rt_mutex_delete (RT_MUTEX *mutex)

{
    int err = 0, rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    mutex = rtai_h2obj_validate(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);

    if (!mutex)
        {
        err = rtai_handle_error(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);
        goto unlock_and_exit;
        }
    
    rc = xnsynch_destroy(&mutex->synch_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (mutex->handle)
        rt_registry_remove(mutex->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    rtai_mark_deleted(mutex);

    if (rc == XNSYNCH_RESCHED)
        /* Some task has been woken up as a result of the deletion:
           reschedule now. */
        xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_mutex_lock(RT_MUTEX *mutex)
 * @brief Acquire a mutex.
 *
 * Attempt to lock a mutex. The calling task is blocked until the
 * mutex is available, in which case it is locked again before this
 * service returns. Mutexes have an ownership property, which means
 * that their current owner is tracked. RTAI mutexes are implicitely
 * recursive and implement the priority inheritance protocol.
 *
 * Since a nested locking count is maintained for the current owner,
 * rt_mutex_lock() and rt_mutex_unlock() must be used in pairs.
 *
 * Tasks pend on mutexes by priority order.
 *
 * @param mutex The descriptor address of the mutex to acquire.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor,
 * including if the deletion occurred while the caller was sleeping on
 * it.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the mutex has become available.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied.
 * If the caller is blocked, the current owner's priority might be
 * temporarily raised as a consequence of the priority inheritance
 * protocol.
 */

int rt_mutex_lock (RT_MUTEX *mutex)

{
    RT_TASK *task;
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    mutex = rtai_h2obj_validate(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);

    if (!mutex)
        {
        err = rtai_handle_error(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);
        goto unlock_and_exit;
        }

    task = rtai_current_task();
    
    if (mutex->owner == NULL)
	{
	xnsynch_set_owner(&mutex->synch_base,&task->thread_base);
	mutex->owner = task;
	mutex->lockcnt = 1;
	goto unlock_and_exit;
	}

    if (mutex->owner == task)
	{
	mutex->lockcnt++;
	goto unlock_and_exit;
	}

    xnsynch_sleep_on(&mutex->synch_base,XN_INFINITE);
        
    if (xnthread_test_flags(&task->thread_base,XNRMID))
	err = -EIDRM; /* Mutex deleted while pending. */
    else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_mutex_unlock(RT_MUTEX *mutex)
 * @brief Unlock mutex.
 *
 * Release a mutex. If the mutex is pended, the first waiting task (by
 * priority order) is immediately unblocked and transfered the
 * ownership of the mutex; otherwise, the mutex is left in an unlocked
 * state.
 *
 * @param mutex The descriptor address of the released mutex.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor.
 *
 * - -EACCES is returned if @a mutex is not owned by the current task.
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

int rt_mutex_unlock (RT_MUTEX *mutex)

{
    int err = 0;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    mutex = rtai_h2obj_validate(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);

    if (!mutex)
        {
        err = rtai_handle_error(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);
        goto unlock_and_exit;
        }

    if (rtai_current_task() != mutex->owner)
	{
	err = -EACCES;
	goto unlock_and_exit;
	}

    if (--mutex->lockcnt > 0)
	goto unlock_and_exit;

    mutex->owner = thread2rtask(xnsynch_wakeup_one_sleeper(&mutex->synch_base));

    if (mutex->owner != NULL)
	xnpod_schedule();
    
 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_mutex_inquire(RT_MUTEX *mutex, RT_MUTEX_INFO *info)
 * @brief Inquire about a mutex.
 *
 * Return various information about the status of a given mutex.
 *
 * @param mutex The descriptor address of the inquired mutex.
 *
 * @param info The address of a structure the mutex information will
 * be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a mutex is not a mutex descriptor.
 *
 * - -EIDRM is returned if @a mutex is a deleted mutex descriptor.
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

int rt_mutex_inquire (RT_MUTEX *mutex,
                      RT_MUTEX_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    mutex = rtai_h2obj_validate(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);

    if (!mutex)
        {
        err = rtai_handle_error(mutex,RTAI_MUTEX_MAGIC,RT_MUTEX);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,mutex->name);
    info->lockcnt = mutex->lockcnt;
    info->nwaiters = xnsynch_nsleepers(&mutex->synch_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_mutex_bind(RT_MUTEX *mutex,
		         const char *name)
 * @brief Bind to a mutex.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given RTAI mutex identified by its symbolic name. If the mutex does
 * not exist on entry, this service blocks the caller until a mutex of
 * the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * mutex to bind to.
 *
 * @param mutex The address of a mutex descriptor retrieved by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EFAULT is returned if @a mutex or @a name is referencing invalid
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
 * @fn int rt_mutex_unbind(RT_MUTEX *mutex)
 *
 * @brief Unbind from a mutex.
 *
 * This user-space only service unbinds the calling task from the
 * mutex object previously retrieved by a call to rt_mutex_bind().
 *
 * @param mutex The address of a mutex descriptor to unbind from.
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

EXPORT_SYMBOL(rt_mutex_create);
EXPORT_SYMBOL(rt_mutex_delete);
EXPORT_SYMBOL(rt_mutex_lock);
EXPORT_SYMBOL(rt_mutex_unlock);
EXPORT_SYMBOL(rt_mutex_inquire);
