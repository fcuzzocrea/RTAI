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

#include "pse51/internal.h"
#include "pse51/thread.h"
#include "pse51/mutex.h"

#define link2mutex(laddr) \
((pthread_mutex_t *)(((char *)laddr)-(int)(&((pthread_mutex_t *)0)->link)))

#define synch2mutex(saddr) \
((pthread_mutex_t *)(((char *)saddr)-(int)(&((pthread_mutex_t *)0)->synchbase)))

static void pse51_mutex_destroy_internal(pthread_mutex_t *mutex);

static pthread_mutexattr_t default_attr;
static xnqueue_t pse51_mutexq;


void pse51_mutex_obj_init(void)
{
    initq(&pse51_mutexq);
    pthread_mutexattr_init(&default_attr);
}

void pse51_mutex_obj_cleanup(void)
{
    xnholder_t *holder;

    xnmutex_lock(&__imutex);
    while ((holder = getheadq(&pse51_mutexq)) != NULL)
	pse51_mutex_destroy_internal(link2mutex(holder));
    xnmutex_unlock(&__imutex);
}



int pthread_mutex_init(pthread_mutex_t *mutex, const pthread_mutexattr_t *attr)
{
    xnflags_t synch_flags = XNSYNCH_PRIO & XNSYNCH_NOPIP;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if(!attr)
        attr=&default_attr;

    xnmutex_lock(&__imutex);
    if(pse51_obj_busy(mutex)) {
        xnmutex_unlock(&__imutex);
        return EBUSY;
    }

    if(attr->magic!=PSE51_MUTEX_ATTR_MAGIC) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }

    mutex->magic=PSE51_MUTEX_MAGIC;
    mutex->attr=*attr;
    mutex->owner=NULL;
    inith(&mutex->link);

    if(attr->protocol == PTHREAD_PRIO_INHERIT)
        synch_flags |= XNSYNCH_PIP;
    
    xnsynch_init(&mutex->synchbase, synch_flags);
    mutex->count=0;
    appendq(&pse51_mutexq, &mutex->link);

    xnmutex_unlock(&__imutex);

    return 0;
}

int pthread_mutex_destroy(pthread_mutex_t *mutex)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    if(!pse51_obj_active(mutex, PSE51_MUTEX_MAGIC, pthread_mutex_t)) {
        xnmutex_unlock(&__imutex);
        return EINVAL;
    }
    if(mutex->count || mutex->condvars) {
        xnmutex_unlock(&__imutex);
        return EBUSY;
    }

    pse51_mutex_destroy_internal(mutex);
    
    xnmutex_unlock(&__imutex);

    return 0;
}

static inline int mutex_timedlock(pthread_mutex_t *mutex, xnticks_t to)
{
    int err;
    pthread_t cur = pse51_current_thread();
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);
    err=mutex_timedlock_internal(mutex, to, &__imutex);
    if(err == EBUSY)
        switch(mutex->attr.type) {
        case PTHREAD_MUTEX_NORMAL:
            /* Deadlock. */
            do {
                xnthread_clear_flags(&cur->threadbase, XNBREAK | XNTIMEO);
                xnsynch_sleep_on(&mutex->synchbase,
                                 to==XN_INFINITE?to:to-xnpod_get_time(),
                                 &__imutex);
            } while(!xnthread_test_flags(&cur->threadbase, XNTIMEO));
            err=ETIMEDOUT;
            break;

        case PTHREAD_MUTEX_ERRORCHECK:
            err=EDEADLK;
            break;

        case PTHREAD_MUTEX_RECURSIVE:
            if(mutex->count == UINT_MAX) {
                err=EAGAIN;
                break;
            }
                
            ++mutex->count;
            err = 0;
        }
    xnmutex_unlock(&__imutex);
    return err;
}

int pthread_mutex_trylock(pthread_mutex_t *mutex)
{
    pthread_t cur = pse51_current_thread();
    int err;
    
    xnmutex_lock(&__imutex);
    err = mutex_trylock_internal(mutex, cur);
    if(err == EBUSY && mutex->attr.type == PTHREAD_MUTEX_RECURSIVE
       && mutex->owner == cur) {
        if(mutex->count == UINT_MAX)
            err=EAGAIN;
        else {
            ++mutex->count;
            err = 0;
        }
    }
    xnmutex_unlock(&__imutex);

    return err;
}

int pthread_mutex_lock(pthread_mutex_t *mutex)
{
    return mutex_timedlock(mutex, XN_INFINITE);
}

int pthread_mutex_timedlock(pthread_mutex_t *mutex, const struct timespec *to)
{
    return mutex_timedlock(mutex, timespec2ticks(to));
}

int pthread_mutex_unlock(pthread_mutex_t *mutex)
{
    int err;
    
    xnmutex_lock(&__imutex);
    err=mutex_unlock_internal(mutex, &__imutex);
    if(err == EPERM && mutex->attr.type == PTHREAD_MUTEX_RECURSIVE) {
        if(mutex->owner == pse51_current_thread() && mutex->count) {
            --mutex->count;
            err = 0;
        }
    }
    xnmutex_unlock(&__imutex);
    return err;
}



static void pse51_mutex_destroy_internal(pthread_mutex_t *mutex)
{
    pse51_mark_deleted(mutex);
    xnsynch_destroy(&mutex->synchbase);
    removeq(&pse51_mutexq, &mutex->link);
}
