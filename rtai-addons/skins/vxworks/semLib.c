/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#include "rtai_config.h"
#include "vxworks/defs.h"


#define WIND_SEMB_OPTION_MASK (SEM_Q_FIFO|SEM_Q_PRIORITY)
#define WIND_SEMC_OPTION_MASK (SEM_Q_FIFO|SEM_Q_PRIORITY)
#define WIND_SEMM_OPTION_MASK SEM_OPTION_MASK

#define WIND_SEM_DEL_SAFE XNSYNCH_SPARE0
#define WIND_SEM_FLUSH XNSYNCH_SPARE1


struct wind_sem;
typedef struct wind_sem wind_sem_t;


typedef struct sem_vtbl
{
    STATUS (*take) (wind_sem_t *, xnticks_t);
    STATUS (*give) (wind_sem_t *);
    STATUS (*flush) (wind_sem_t *);
} sem_vtbl_t;


struct wind_sem 
{
    unsigned int magic;

    xnholder_t link;

#define link2wind_sem(laddr) \
((wind_sem_t *)(((char *)laddr) - (int)(&((wind_sem_t *)0)->link)))

    xnsynch_t synchbase;

#define synch2wind_sem(saddr) \
((wind_sem_t *)(((char *)saddr) - (int)(&((wind_sem_t *)0)->synchbase)))

    unsigned count;
    /* count has a different meaning for the different kinds of semaphores :
       binary semaphore : binary state of the semaphore,
       counting semaphore : the semaphore count,
       mutex : the number of times the semaphore has been recursively taken.
    */
    
    xnthread_t * owner;

    const sem_vtbl_t * vtbl;
};




static xnqueue_t wind_sem_q;

static const sem_vtbl_t semb_vtbl;
static const sem_vtbl_t semc_vtbl;
static const sem_vtbl_t semm_vtbl;

static void sem_destroy_internal(wind_sem_t *sem);
static int sem_create_internal(int flags, const sem_vtbl_t * vtbl, int count);




void wind_sem_init (void)
{
    initq(&wind_sem_q);
}


void wind_sem_cleanup (void)
{
    xnholder_t *holder;

    while ((holder = getheadq(&wind_sem_q)) != NULL)
	sem_destroy_internal(link2wind_sem(holder));
}




int semBCreate(int flags, SEM_B_STATE state)
{
    int bflags = 0;

    error_check( flags & ~WIND_SEMB_OPTION_MASK, S_semLib_INVALID_OPTION,
                 return 0);

    error_check( state!=SEM_EMPTY && state!=SEM_FULL, S_semLib_INVALID_STATE,
                 return 0);
    
    if (flags & SEM_Q_PRIORITY)
	bflags |= XNSYNCH_PRIO;

    
    return sem_create_internal(bflags, &semb_vtbl, (int) state);
}


int semCCreate(int flags, int count)
{
    int bflags = 0;

    error_check( flags & ~WIND_SEMC_OPTION_MASK, S_semLib_INVALID_OPTION,
                 return 0 );

    if (flags & SEM_Q_PRIORITY)
	bflags |= XNSYNCH_PRIO;

    return sem_create_internal(bflags, &semc_vtbl, count);
}


int semMCreate(int flags)
{
    int bflags = 0;

    if (flags & ~WIND_SEMM_OPTION_MASK)
        goto invalid_op;
        
    if (flags & SEM_Q_PRIORITY)
	bflags |= XNSYNCH_PRIO;

    if (flags & SEM_INVERSION_SAFE) {
        if( !(flags & SEM_Q_PRIORITY) )
            goto invalid_op;
        
        bflags |= XNSYNCH_PIP;
    }

    if (flags & SEM_DELETE_SAFE)
        bflags |= WIND_SEM_DEL_SAFE;

    return sem_create_internal(bflags, &semm_vtbl, 0);

 invalid_op:
    wind_errnoset(S_semLib_INVALID_OPTION);
    return 0;
    
}


STATUS semDelete(int sem_id)
{
    wind_sem_t * sem;
    check_NOT_ISR_CALLABLE(return ERROR);

    xnmutex_lock(&__imutex);
    check_OBJ_ID_ERROR(sem_id, wind_sem_t, sem, WIND_SEM_MAGIC, goto error);
    sem_destroy_internal(sem);
    xnmutex_unlock(&__imutex);

    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS semTake(int sem_id, int timeout)
{
    wind_sem_t * sem;
    xnticks_t xntimeout;
    
    check_NOT_ISR_CALLABLE(return ERROR);

    switch(timeout) {
    case WAIT_FOREVER:
        xntimeout = XN_INFINITE;
        break;
    case NO_WAIT:
        xntimeout = XN_NONBLOCK;
        break;
    default:
        xntimeout = timeout;
    }
    
    xnmutex_lock(&__imutex);
    check_OBJ_ID_ERROR(sem_id, wind_sem_t, sem, WIND_SEM_MAGIC, goto error);
    return sem->vtbl->take(sem, xntimeout);
    
 error:
    xnmutex_unlock(&__imutex);
    return ERROR;

}


STATUS semGive(int sem_id)
{
    wind_sem_t * sem;

    xnmutex_lock(&__imutex);
    check_OBJ_ID_ERROR(sem_id, wind_sem_t, sem, WIND_SEM_MAGIC, goto error);
    return sem->vtbl->give(sem);
    
 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


STATUS semFlush(int sem_id)
{
    wind_sem_t * sem;

    xnmutex_lock(&__imutex);
    check_OBJ_ID_ERROR(sem_id, wind_sem_t, sem, WIND_SEM_MAGIC, goto error);
    return sem->vtbl->flush(sem);

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}




static STATUS semb_take(wind_sem_t *sem, xnticks_t to)
{
    xnthread_t * thread = &wind_current_task()->threadbase;

    if (sem->count > 0)
	--sem->count;
    else
    {
        error_check(to == XN_NONBLOCK, S_objLib_OBJ_UNAVAILABLE, goto error);
        
	xnsynch_sleep_on(&sem->synchbase, to, &__imutex);

	error_check(xnthread_test_flags(thread,XNRMID), S_objLib_OBJ_DELETED, 
                    goto error);

        error_check(xnthread_test_flags(thread,XNTIMEO), S_objLib_OBJ_TIMEOUT,
                    goto error);
    }

    xnmutex_unlock(&__imutex);
    return OK;
    
 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


static STATUS semb_give(wind_sem_t * sem)
{
    if (xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
	xnpod_schedule(&__imutex);
    else {
        if(sem->count != 0)
        {
            wind_errnoset(S_semLib_INVALID_OPERATION);
	    xnmutex_unlock(&__imutex);
            return ERROR;
        }
        sem->count=1;
    }

    xnmutex_unlock(&__imutex);
    return OK;
}


static STATUS semb_flush(wind_sem_t * sem)
{
    xnsynch_flush(&sem->synchbase, WIND_SEM_FLUSH);
    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);
    return OK;
}


static const sem_vtbl_t semb_vtbl= {
    take: &semb_take,
    give: &semb_give,
    flush: &semb_flush
};




static STATUS semc_give(wind_sem_t * sem)
{
    if (xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
	xnpod_schedule(&__imutex);
    else
	++sem->count;

    xnmutex_unlock(&__imutex);
    return OK;
}


static const sem_vtbl_t semc_vtbl = {
    take: &semb_take,
    give: &semc_give,
    flush: &semb_flush
};




static STATUS semm_take(wind_sem_t *sem, xnticks_t to)
{
    xnthread_t * thread = &wind_current_task()->threadbase;

    if (sem->count != 0 && sem->owner != thread)
    {
        error_check(to == XN_NONBLOCK, S_objLib_OBJ_UNAVAILABLE, goto error);
        
	xnsynch_sleep_on(&sem->synchbase, to, &__imutex);

	error_check(xnthread_test_flags(thread,XNRMID), S_objLib_OBJ_DELETED, 
                    goto error);

        error_check(xnthread_test_flags(thread,XNTIMEO), S_objLib_OBJ_TIMEOUT,
                    goto error);
    }

    if( sem->count == 0 )
    {
        sem->owner = thread;
        if( xnsynch_test_flags(&sem->synchbase, XNSYNCH_PIP) )
            xnsynch_set_owner(&sem->synchbase, thread);
    }
    
    if(xnsynch_test_flags(&sem->synchbase, WIND_SEM_DEL_SAFE))
        /* we can do this here because we already are in a critical section
           and thus do not need to call taskSafe */
        ++wind_current_task()->safecnt;
    ++sem->count;

    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


static STATUS semm_give(wind_sem_t * sem)
{
    int need_resched = 0;
    xnsynch_t *sem_synch = &sem->synchbase;
    
    check_NOT_ISR_CALLABLE(goto error);

    if(&wind_current_task()->threadbase != sem->owner || sem->count == 0 ) {
        wind_errnoset(S_semLib_INVALID_OPERATION);
        goto error;
    }

    if ( --sem->count == 0 && xnsynch_wakeup_one_sleeper(sem_synch) != NULL)
            need_resched = 1;
    
    if( xnsynch_test_flags(sem_synch, WIND_SEM_DEL_SAFE) )
    {
        if( wind_current_task()->safecnt == 0 )
            goto error;

        if( --wind_current_task()->safecnt == 0 &&
            xnsynch_flush(&wind_current_task()->safesync,0) == XNSYNCH_RESCHED )
            need_resched = 1;
    }
    
    if( need_resched )
        xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);
    return OK;

 error:
    xnmutex_unlock(&__imutex);
    return ERROR;
}


static STATUS semm_flush(wind_sem_t * sem __attribute__((unused)) )
{
    wind_errnoset(S_semLib_INVALID_OPERATION);
    xnmutex_unlock(&__imutex);
    return ERROR;
}


static const sem_vtbl_t semm_vtbl = {
    take: &semm_take,
    give: &semm_give,
    flush: &semm_flush
};




static int sem_create_internal(int flags, const sem_vtbl_t * vtbl, int count)
{
    wind_sem_t *sem;
    
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    check_alloc(wind_sem_t, sem, return 0);

    xnsynch_init(&sem->synchbase,(xnflags_t) flags);
    inith(&sem->link);
    sem->magic = WIND_SEM_MAGIC;
    sem->count = count;
    sem->vtbl = vtbl;

    xnmutex_lock(&__imutex);
    appendq(&wind_sem_q,&sem->link);
    xnmutex_unlock(&__imutex);

    return (int) sem;
}


static void sem_destroy_internal (wind_sem_t *sem)
{
    xnmutex_lock(&__imutex);
    xnsynch_destroy(&sem->synchbase);
    wind_mark_deleted(sem);
    removeq(&wind_sem_q,&sem->link);
    xnmutex_unlock(&__imutex);

    xnfree(sem);
}
