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

#include "pse51/thread.h"
#include "pse51/sem.h"

static xnqueue_t pse51_semq;
static void sem_destroy_internal(sem_t *sem);

static inline int sem_trywait_internal(sem_t *sem)
{
    if(sem->value == 0)
        return EAGAIN;

    --sem->value;
    return 0;
}

int sem_trywait(sem_t *sem)
{
    int err;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t))
        err=EINVAL;
    else
        err=sem_trywait_internal(sem);
    xnmutex_unlock(&__imutex);
    
    if(err) {
        thread_errno()=err;
        return -1;
    }

    return 0;
}

static inline int sem_timedwait_internal(sem_t *sem, xnticks_t to)
{
    pthread_t cur;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t)) {
        thread_errno()=EINVAL;
        goto error;
    }
    cur = pse51_current_thread();
    if(sem_trywait_internal(sem) == EAGAIN) {
        xnthread_clear_flags(&cur->threadbase, XNBREAK | XNTIMEO);
        xnsynch_sleep_on(&sem->synchbase,
                         to==XN_INFINITE?to:to-xnpod_get_time(), &__imutex);
            
        /* Handle cancellation requests. */
        thread_cancellation_point(cur, &__imutex);
            
        if(xnthread_test_flags(&cur->threadbase, XNBREAK)) {
            thread_errno()=EINTR;
            goto error;
        }
        
        if(xnthread_test_flags(&cur->threadbase, XNTIMEO)) {
            thread_errno()=ETIMEDOUT;
            goto error;
        }
    }
    xnmutex_unlock(&__imutex);    
    return 0;

 error:
    xnmutex_unlock(&__imutex);    
    return -1;
}

int sem_wait(sem_t *sem)
{
    return sem_timedwait_internal(sem, XN_INFINITE);
}

int sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
    return sem_timedwait_internal(sem, timespec2ticks(abs_timeout));
}

int sem_post(sem_t *sem)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t)) {
        thread_errno() = EINVAL;
        goto error;
    }

    if(xnsynch_wakeup_one_sleeper(&sem->synchbase))
        xnpod_schedule(&__imutex);
    else if(sem->value == SEM_VALUE_MAX) {
        thread_errno() = EAGAIN;
        goto error;
    } else
        ++sem->value;           /* Same behaviour as linuxthreads. */

    xnmutex_unlock(&__imutex);
    return 0;

 error:
    xnmutex_unlock(&__imutex);
    return -1;
}

int sem_getvalue(sem_t *sem, int *value)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t)) {
        xnmutex_unlock(&__imutex);
        thread_errno() = EINVAL;
        return -1;
    }

    *value=sem->value ? (int) sem->value : -(xnsynch_nsleepers(&sem->synchbase));
    xnmutex_lock(&__imutex);

    return 0;
}

int *pse51_errno_location(void)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    return &thread_errno();
}

int sem_init(sem_t *sem, int pshared, unsigned int value)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(pse51_obj_busy(sem)) {
        xnmutex_unlock(&__imutex);
        thread_errno() = EBUSY;
        return -1;
    }

    if(pshared) {
        xnmutex_unlock(&__imutex);
        thread_errno() = ENOSYS;
        return -1;
    }

    if(value > SEM_VALUE_MAX) {
        xnmutex_unlock(&__imutex);
        thread_errno() = EINVAL;
        return -1;
    }

    sem->magic=PSE51_SEM_MAGIC;
    inith(&sem->link);
    xnsynch_init(&sem->synchbase, XNSYNCH_PRIO);
    sem->value=value;

    xnmutex_unlock(&__imutex);

    return 0;
}

int sem_destroy(sem_t *sem)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(sem, PSE51_SEM_MAGIC, sem_t)) {
        xnmutex_unlock(&__imutex);
        thread_errno() = EINVAL;
        return -1;
    }

    if(xnsynch_nsleepers(&sem->synchbase) > 0) {
        xnmutex_unlock(&__imutex);
        thread_errno() = EBUSY;
        return -1;
    }

    sem_destroy_internal(sem);

    xnmutex_unlock(&__imutex);

    return 0;
}

void pse51_sem_obj_init(void)
{
    initq(&pse51_semq);
}

void pse51_sem_obj_cleanup(void)
{
    xnholder_t *holder;

    while( (holder=getheadq(&pse51_semq)) )
        sem_destroy_internal(link2sem(holder));
}

static void sem_destroy_internal(sem_t *sem)
{
    pse51_mark_deleted(sem);
    xnsynch_destroy(&sem->synchbase);
    removeq(&pse51_semq, &sem->link);    
}
