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


#include "pse51/mutex.h"
#include "pse51/cond.h"

#define link2cond(laddr) \
((pthread_cond_t *)(((char *)laddr)-(int)(&((pthread_cond_t *)0)->link)))

#define synch2cond(saddr) \
((pthread_cond_t *)(((char *)saddr)-(int)(&((pthread_cond_t *)0)->synchbase)))

static pthread_condattr_t default_cond_attr;
static xnqueue_t pse51_condq;

static void cond_destroy_internal(pthread_cond_t *cond);


/* FIXME: instead of trying to guess why the thread was unblocked, signals,
   cancellations and maybe condition variables should use xnthread spare
   bits. Using a "pthread_cond_signal" count is wrong. Signals should cause a
   wakeup, the user should decide whether this wakeup is spurious. */

int pthread_cond_init(pthread_cond_t *cond, const pthread_condattr_t *attr)
{
    xnflags_t synch_flags = XNSYNCH_PRIO & XNSYNCH_NOPIP;

    if(!attr)
        attr=&default_cond_attr;

    xnmutex_lock(&__imutex);
    if(pse51_obj_busy(cond)) {
        xnmutex_unlock(&__imutex);
        return EBUSY;
    }

    if(attr->magic!=PSE51_COND_ATTR_MAGIC){
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    cond->magic=PSE51_COND_MAGIC;
    xnsynch_init(&cond->synchbase, synch_flags);
    inith(&cond->link);
    cond->attr=*attr;
    cond->mutex=NULL;
    cond->signals = 0;

    appendq(&pse51_condq, &cond->link);

    xnmutex_unlock(&__imutex);

    return 0;    
}



int pthread_cond_destroy(pthread_cond_t *cond)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    if(xnsynch_nsleepers(&cond->synchbase)) {
        xnmutex_unlock(&__imutex);
        return EBUSY;
    }

    pse51_mark_deleted(cond);
    xnsynch_destroy(&cond->synchbase);
    removeq(&pse51_condq, &cond->link);
    
    xnmutex_unlock(&__imutex);

    return 0;
}



static int cond_timedwait_internal(pthread_cond_t *cond, pthread_mutex_t *mutex,
                                   xnticks_t to)
{
    pthread_t cur;
    int err = 0;
    int count = 1;

    if(!cond || !mutex)
        return EINVAL;
    
    xnmutex_lock(&__imutex);
    /* If another thread waiting for cond does not use the same mutex */
    if(!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t)
       || (cond->mutex && cond->mutex!=mutex)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    /* Save the mutex lock count. */
    if(mutex->count) {
        count=mutex->count;
        mutex->count = 1;
    }
    
    if(mutex_unlock_internal(mutex, &__imutex)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    /* mutex is unlocked with its previous recursive lock count stored in
       "count" */
    
    /* Wait for another thread to signal the condition. */
    cur = pse51_current_thread();
    do {
        /* Bind mutex to cond. */
        if(!cond->mutex) {
            cond->mutex=mutex;
            ++mutex->condvars;
        }
        xnthread_clear_flags(&cur->threadbase, XNBREAK | XNTIMEO);
        xnsynch_sleep_on(&cond->synchbase,
                         to==XN_INFINITE?to:to-xnpod_get_time(), &__imutex);

        if(xnthread_test_flags(&cur->threadbase, XNBREAK | XNTIMEO)) {
            if(xnthread_test_flags(&cur->threadbase, XNTIMEO)) {
                err=ETIMEDOUT;
                break;
            }

            if(thread_testcancel(cur))
                break;
        }
        /* Sleep again only if wake up was spurious and not caused by a
           cancellation request (handled after the current thread will have
           locked the mutex). */
    } while(!cond->signals);

    /* Relock the mutex */
    mutex_timedlock_internal(mutex, XN_INFINITE, &__imutex);

    /* Restore the mutex lock count. */
    mutex->count+=count-1;

    /* Unbind mutex and cond, if this thread was the last waiting thread. */
    if(!xnsynch_nsleepers(&cond->synchbase) && cond->mutex) {
        --mutex->condvars;
        cond->mutex=NULL;
    }

    thread_cancellation_point(cur, &__imutex);

    /* The wakeup signal is only consumed after the cancellation point. */
    if(!err && cond->signals)
        --cond->signals;

    xnmutex_unlock(&__imutex);

    return err;
}



int pthread_cond_wait(pthread_cond_t *cond, pthread_mutex_t *mutex)
{
    return cond_timedwait_internal(cond, mutex, XN_INFINITE);
}



int pthread_cond_timedwait(pthread_cond_t *cond, pthread_mutex_t *mutex,
                           const struct timespec *abstime)
{
    return cond_timedwait_internal(cond, mutex, timespec2ticks(abstime));
}



int pthread_cond_signal(pthread_cond_t *cond)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    if(xnsynch_wakeup_one_sleeper(&cond->synchbase))
        ++cond->signals;
    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);

    return 0;
}



int pthread_cond_broadcast(pthread_cond_t *cond)
{
    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(cond, PSE51_COND_MAGIC, pthread_cond_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    cond->signals += xnsynch_nsleepers(&cond->synchbase);
    xnsynch_flush(&cond->synchbase, 0);
    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);

    return 0;
}



void pse51_cond_obj_init(void)
{
    initq(&pse51_condq);
    pthread_condattr_init(&default_cond_attr);
}



void pse51_cond_obj_cleanup(void)
{
    xnholder_t *holder;

    while ((holder = getq(&pse51_condq)) != NULL)
	cond_destroy_internal(link2cond(holder));
}



static void cond_destroy_internal(pthread_cond_t *cond)
{
    pse51_mark_deleted(cond);
    xnsynch_destroy(&cond->synchbase);
    removeq(&pse51_condq, &cond->link);
}
