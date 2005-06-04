/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

#include "posix/thread.h"
#include "posix/sem.h"

static xnqueue_t pse51_semq;

static inline int sem_trywait_internal (sem_t *sem)

{
    if (sem->value == 0)
        return EAGAIN;

    --sem->value;

    return 0;
}

static void sem_destroy_internal (sem_t *sem)

{
    pse51_mark_deleted(sem);
    /* synchbase wait queue may not be empty only when this function is called
       from pse51_sem_obj_cleanup, hence the absence of xnpod_schedule(). */
    xnsynch_destroy(&sem->synchbase);
    removeq(&pse51_semq, &sem->link);    
}

int sem_trywait (sem_t *sem)

{
    int err;
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t))
        err = EINVAL;
    else
        err = sem_trywait_internal(sem);

    xnlock_put_irqrestore(&nklock, s);
    
    if (err)
	{
        thread_errno() = err;
        return -1;
	}

    return 0;
}

static inline int sem_timedwait_internal (sem_t *sem, xnticks_t to)

{
    pthread_t cur;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t))
	{
        thread_errno() = EINVAL;
        goto error;
	}

    cur = pse51_current_thread();

    if (sem_trywait_internal(sem) == EAGAIN)
	{
        xnsynch_sleep_on(&sem->synchbase,
                         to == XN_INFINITE ? to:to - xnpod_get_time());
            
        /* Handle cancellation requests. */
        thread_cancellation_point(cur);
            
        if (xnthread_test_flags(&cur->threadbase, XNBREAK))
	    {
            thread_errno() = EINTR;
            goto error;
	    }
        
        if (xnthread_test_flags(&cur->threadbase, XNTIMEO))
	    {
            thread_errno() = ETIMEDOUT;
            goto error;
	    }
	}

    xnlock_put_irqrestore(&nklock, s);

    return 0;

 error:

    xnlock_put_irqrestore(&nklock, s);    

    return -1;
}

int sem_wait (sem_t *sem) {

    return sem_timedwait_internal(sem, XN_INFINITE);
}

int sem_timedwait (sem_t *sem, const struct timespec *abs_timeout) {

    return sem_timedwait_internal(sem, ts2ticks_ceil(abs_timeout)+1);
}

int sem_post (sem_t *sem)

{
    spl_t s;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t))
	{
        thread_errno() = EINVAL;
        goto error;
	}

    if (sem->value == SEM_VALUE_MAX)
        {
        thread_errno() = EAGAIN;
        goto error;
	}

    if(xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
        xnpod_schedule();
    else
        ++sem->value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;

 error:

    xnlock_put_irqrestore(&nklock, s);

    return -1;
}

int sem_getvalue (sem_t *sem, int *value)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        thread_errno() = EINVAL;
        return -1;
	}

    *value = sem->value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sem_init (sem_t *sem, int pshared, unsigned int value)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (pshared)
	{
        xnlock_put_irqrestore(&nklock, s);
        thread_errno() = ENOSYS;
        return -1;
	}

    if (value > SEM_VALUE_MAX)
	{
        xnlock_put_irqrestore(&nklock, s);
        thread_errno() = EINVAL;
        return -1;
	}

    sem->magic = PSE51_SEM_MAGIC;
    inith(&sem->link);
    appendq(&pse51_semq, &sem->link);    
    xnsynch_init(&sem->synchbase, XNSYNCH_PRIO);
    sem->value = value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int sem_destroy (sem_t *sem)

{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t))
	{
        xnlock_put_irqrestore(&nklock, s);
        thread_errno() = EINVAL;
        return -1;
	}

    if (xnsynch_nsleepers(&sem->synchbase) > 0)
	{
        xnlock_put_irqrestore(&nklock, s);
        thread_errno() = EBUSY;
        return -1;
	}

    sem_destroy_internal(sem);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

void pse51_sem_obj_init (void) {

    initq(&pse51_semq);
}

void pse51_sem_obj_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&pse51_semq)) != NULL)
        sem_destroy_internal(link2sem(holder));
}

EXPORT_SYMBOL(sem_init);
EXPORT_SYMBOL(sem_destroy);
EXPORT_SYMBOL(sem_post);
EXPORT_SYMBOL(sem_trywait);
EXPORT_SYMBOL(sem_wait);
EXPORT_SYMBOL(sem_timedwait);
EXPORT_SYMBOL(sem_getvalue);
