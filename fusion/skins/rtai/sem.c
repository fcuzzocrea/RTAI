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
 * \ingroup semaphore
 */

/*!
 * \ingroup native
 * \defgroup semaphore Semaphore services.
 *
 * Semaphore services.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/sem.h>
#include <rtai/registry.h>

int __sem_pkg_init (void) {

    return 0;
}

void __sem_pkg_cleanup (void) {

}

/**
 * @fn int rt_sem_create(RT_SEM *sem,
                         const char *name,
                         unsigned long icount,
                         int mode)
 * @brief Create a counting semaphore.
 *
 * @param sem The address of a semaphore descriptor RTAI will use to
 * store the semaphore-related data.  This descriptor must always be
 * valid while the semaphore is active therefore it must be allocated
 * in permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * semaphore. When non-NULL and non-empty, this string is copied to a
 * safe place into the descriptor, and passed to the registry package
 * if enabled for indexing the created semaphore.
 *
 * @param icount The initial value of the semaphore count.
 *
 * @param mode The semaphore creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new semaphore:
 *
 * - S_FIFO makes tasks pend in FIFO order on the semaphore.
 *
 * - S_PRIO makes tasks pend in priority order on the semaphore.
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

int rt_sem_create (RT_SEM *sem,
                   const char *name,
                   unsigned long icount,
                   int mode)
{
    int err = 0;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnsynch_init(&sem->synch_base,mode & S_PRIO);
    sem->count = icount;
    sem->handle = 0;    /* i.e. (still) unregistered semaphore. */
    sem->magic = RTAI_SEM_MAGIC;
    xnobject_copy_name(sem->name,name);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(sem->name,sem,&sem->handle);

        if (err)
            rt_sem_delete(sem);
        }
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_sem_delete(RT_SEM *sem)
 * @brief Delete a semaphore.
 *
 * Destroy a semaphore and release all the tasks currently pending on
 * it.  A semaphore exists in the system since rt_sem_create() has
 * been called to create it, so this service must be called in order
 * to destroy it afterwards.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor.
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

int rt_sem_delete (RT_SEM *sem)

{
    int err = 0, rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    sem = rtai_h2obj_validate(sem,RTAI_SEM_MAGIC,RT_SEM);

    if (!sem)
        {
        err = rtai_handle_error(sem,RTAI_SEM_MAGIC,RT_SEM);
        goto unlock_and_exit;
        }
    
    rc = xnsynch_destroy(&sem->synch_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (sem->handle)
        rt_registry_remove(sem->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    rtai_mark_deleted(sem);

    if (rc == XNSYNCH_RESCHED)
        /* Some task has been woken up as a result of the deletion:
           reschedule now. */
        xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_sem_p(RT_SEM *sem,
                    RTIME timeout)
 * @brief Pend on a semaphore.
 *
 * Acquire a semaphore unit. If the semaphore value is greater than
 * zero, it is decremented by one and the service immediately returns
 * to the caller. Otherwise, the caller is blocked until the semaphore
 * is either signaled or destroyed, unless a non-blocking operation
 * has been required.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @param timeout The number of clock ticks to wait for a semaphore
 * unit to be available (see note). Passing RT_TIME_INFINITE causes
 * the caller to block indefinitely until a unit is available. Passing
 * RT_TIME_NONBLOCK causes the service to return immediately without
 * waiting if no unit is available.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor,
 * including if the deletion occurred while the caller was sleeping on
 * it for a unit to become available.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * RT_TIME_NONBLOCK and the semaphore value is zero.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before a semaphore unit has become available.
 *
 * - -ETIMEDOUT is returned if no unit is available within the
 * specified amount of time.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @timeout is equal to RT_TIME_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

int rt_sem_p (RT_SEM *sem,
              RTIME timeout)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sem = rtai_h2obj_validate(sem,RTAI_SEM_MAGIC,RT_SEM);

    if (!sem)
        {
        err = rtai_handle_error(sem,RTAI_SEM_MAGIC,RT_SEM);
        goto unlock_and_exit;
        }
    
    if (timeout == RT_TIME_NONBLOCK)
        {
        if (sem->count > 0)
            sem->count--;
        else
            err = -EWOULDBLOCK;

        goto unlock_and_exit;
        }

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (sem->count > 0)
        --sem->count;
    else
        {
        RT_TASK *task = rtai_current_task();

        xnsynch_sleep_on(&sem->synch_base,timeout);
        
        if (xnthread_test_flags(&task->thread_base,XNRMID))
            err = -EIDRM; /* Semaphore deleted while pending. */
        else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
            err = -ETIMEDOUT; /* Timeout.*/
        else if (xnthread_test_flags(&task->thread_base,XNBREAK))
            err = -EINTR; /* Unblocked.*/
        }

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_sem_v(RT_SEM *sem)
 * @brief Signal a semaphore.
 *
 * Release a semaphore unit. If the semaphore is pended, the first
 * waiting task (by queuing order) is immediately unblocked;
 * otherwise, the semaphore value is incremented by one.
 *
 * @param sem The descriptor address of the affected semaphore.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor.
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

int rt_sem_v (RT_SEM *sem)

{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sem = rtai_h2obj_validate(sem,RTAI_SEM_MAGIC,RT_SEM);

    if (!sem)
        {
        err = rtai_handle_error(sem,RTAI_SEM_MAGIC,RT_SEM);
        goto unlock_and_exit;
        }
    
    if (xnsynch_wakeup_one_sleeper(&sem->synch_base) != NULL)
        xnpod_schedule();
    else
        sem->count++;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_sem_inquire(RT_SEM *sem, RT_SEM_INFO *info)
 * @brief Inquire about a semaphore.
 *
 * Return various information about the status of a given semaphore.
 *
 * @param sem The descriptor address of the inquired semaphore.
 *
 * @param info The address of a structure the semaphore information
 * will be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a sem is not a semaphore descriptor.
 *
 * - -EIDRM is returned if @a sem is a deleted semaphore descriptor.
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

int rt_sem_inquire (RT_SEM *sem,
                    RT_SEM_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sem = rtai_h2obj_validate(sem,RTAI_SEM_MAGIC,RT_SEM);

    if (!sem)
        {
        err = rtai_handle_error(sem,RTAI_SEM_MAGIC,RT_SEM);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,sem->name);
    info->count = sem->count;
    info->nwaiters = xnsynch_nsleepers(&sem->synch_base);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*@}*/

EXPORT_SYMBOL(rt_sem_create);
EXPORT_SYMBOL(rt_sem_delete);
EXPORT_SYMBOL(rt_sem_p);
EXPORT_SYMBOL(rt_sem_v);
EXPORT_SYMBOL(rt_sem_inquire);
