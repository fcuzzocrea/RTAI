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

#ifndef PSE51_MUTEX_H
#define PSE51_MUTEX_H

#include "pse51/internal.h"
#include "pse51/thread.h"

/* must be called with __imutex locked. */
static inline int mutex_trylock_internal(pthread_mutex_t *mutex, pthread_t cur)
{
    if(!pse51_obj_active(mutex, PSE51_MUTEX_MAGIC, pthread_mutex_t))
        return EINVAL;
    
    if(mutex->count)
        return EBUSY;

    xnsynch_set_owner(&mutex->synchbase, &cur->threadbase);
    mutex->owner=cur;
    mutex->count=1;
    return 0;
}


/* must be called with imutex locked. */
static inline int
mutex_timedlock_internal(pthread_mutex_t *mutex, xnticks_t to, xnmutex_t *imutex)
{
    pthread_t cur = pse51_current_thread();
    int err ;

    if((err=mutex_trylock_internal(mutex, cur))==EBUSY && mutex->owner != cur) {
        do {
            xnthread_clear_flags(&cur->threadbase, XNBREAK | XNTIMEO);
            xnsynch_sleep_on(&mutex->synchbase,
                             to==XN_INFINITE?to:to-xnpod_get_time(),
                             imutex);
            if(mutex->count &&
               xnthread_test_flags(&cur->threadbase, XNTIMEO))
                return ETIMEDOUT;

        } while((err=mutex_trylock_internal(mutex, cur))==EBUSY);
    }

    return err;
}


/* must be called with imutex locked. */
static inline int
mutex_unlock_internal(pthread_mutex_t *mutex, xnmutex_t *imutex)
{
    if(!pse51_obj_active(mutex, PSE51_MUTEX_MAGIC, pthread_mutex_t))
        return EINVAL;
    
    if(mutex->owner != pse51_current_thread() || mutex->count != 1)
        return EPERM;
    
    mutex->owner = NULL;
    mutex->count = 0;
    if(xnsynch_wakeup_one_sleeper(&mutex->synchbase))
        xnpod_schedule(imutex);
    else
        xnsynch_set_owner(&mutex->synchbase, NULL);

    return 0;
}

void pse51_mutex_obj_init(void);

void pse51_mutex_obj_cleanup(void);
    
#endif /*PSE51_MUTEX_H*/
