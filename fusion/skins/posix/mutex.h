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

#ifndef _POSIX_MUTEX_H
#define _POSIX_MUTEX_H

#include "posix/internal.h"
#include "posix/thread.h"

/* must be called with nklock locked, interrupts off. */
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


/* must be called with nklock locked, interrupts off. */
static inline int mutex_timedlock_internal(pthread_mutex_t *mutex, xnticks_t to)

{
    pthread_t cur = pse51_current_thread();
    int err ;

    err = mutex_trylock_internal(mutex, cur);

    if(mutex->owner != cur)
        while (err == EBUSY)
            {
            xnsynch_sleep_on(&mutex->synchbase,
                             to==XN_INFINITE?to:to-xnpod_get_time());

            err=mutex_trylock_internal(mutex, cur);   

            if (err == EBUSY &&
                xnthread_test_flags(&cur->threadbase, XNTIMEO))
                return ETIMEDOUT;
            }

    return err;
}


/* must be called with nklock locked, interrupts off. */
static inline int mutex_unlock_internal(pthread_mutex_t *mutex)

{
    if(!pse51_obj_active(mutex, PSE51_MUTEX_MAGIC, pthread_mutex_t))
        return EINVAL;
    
    if(mutex->owner != pse51_current_thread() || mutex->count != 1)
        return EPERM;
    
    mutex->owner = NULL;
    mutex->count = 0;
    if(xnsynch_wakeup_one_sleeper(&mutex->synchbase))
        xnpod_schedule();
    else
        xnsynch_set_owner(&mutex->synchbase, NULL);

    return 0;
}


/* must be called with nklock locked, interrupts off. */
static inline int mutex_save_count(pthread_mutex_t *mutex, unsigned *count_ptr)

{
    if(mutex->count == 0)       /* Mutex is not locked. */
        return EINVAL;

    /* Save the count and force it to 1, so that mutex_unlock_internal can
       do its job. */
    *count_ptr=mutex->count;
    mutex->count=1;

    return mutex_unlock_internal(mutex);
}


/* must be called with nklock locked, interrupts off. */
static inline void mutex_restore_count(pthread_mutex_t *mutex, unsigned count)

{
    /* Relock the mutex */
    mutex_timedlock_internal(mutex, XN_INFINITE);

    /* Restore the mutex lock count. */
    mutex->count = count;
}


void pse51_mutex_obj_init(void);

void pse51_mutex_obj_cleanup(void);
    
#endif /* !_POSIX_MUTEX_H */
