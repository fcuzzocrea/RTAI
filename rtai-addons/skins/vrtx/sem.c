/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
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
#include "vrtx/task.h"
#include "vrtx/sem.h"

static xnqueue_t vrtxsemq;

static int sem_destroy_internal(vrtxsem_t *sem);

void vrtxsem_init (void) {
    initq(&vrtxsemq);
}

void vrtxsem_cleanup (void) {

    xnholder_t *holder;

    while ((holder = getheadq(&vrtxsemq)) != NULL)
	sem_destroy_internal(link2vrtxsem(holder));
}

static int sem_destroy_internal (vrtxsem_t *sem)

{
    int s;

    xnmutex_lock(&__imutex);
    removeq(&vrtxsemq,&sem->link);
    vrtx_release_id(sem->semid);
    s = xnsynch_destroy(&sem->synchbase);
    vrtx_mark_deleted(sem);
    xnmutex_unlock(&__imutex);

    xnfree(sem);

    return s;
}

int sc_screate (unsigned initval, int opt, int *perr)

{
    int bflags = 0, semid;
    vrtxsem_t *sem;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);
    if ((opt != 1) && (opt != 0))
	{
	*perr = ER_IIP;
	return 0;
	}

    sem = (vrtxsem_t *)xnmalloc(sizeof(*sem));

    if (!sem)
	{
	*perr = ER_NOCB;
	return 0;
	}

    semid = vrtx_alloc_id(sem);

    if (semid < 0)
	{
	*perr = ER_NOCB;
	xnfree(sem);
	return 0;
	}

    if (opt == 0)
	bflags = XNSYNCH_PRIO;
    else
	bflags = XNSYNCH_FIFO;

    xnsynch_init(&sem->synchbase,bflags);
    inith(&sem->link);
    sem->semid = semid;
    sem->magic = VRTX_SEM_MAGIC;
    sem->count = initval;

    xnmutex_lock(&__imutex);
    appendq(&vrtxsemq,&sem->link);
    xnmutex_unlock(&__imutex);

    *perr = RET_OK;

    return semid;
}

void sc_sdelete(int semid, int opt, int *errp)
{
    vrtxsem_t *sem;

    if ((opt != 0) && (opt != 1))
	{
	*errp = ER_IIP;
	return;
	}

    xnmutex_lock(&__imutex);

    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;

    if (opt == 0 && xnsynch_nsleepers(&sem->synchbase) > 0)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_PND;
	return;
	}

    /* forcing delete or no task pending */
    if (sem_destroy_internal(sem) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);
}    

void sc_spend(int semid, long timeout, int *errp)
{
    vrtxsem_t *sem;
    vrtxtask_t *task;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;
    if (sem->count > 0)
	sem->count--;
    else
	{
	task = vrtx_current_task();
	task->vrtxtcb.TCBSTAT = TBSSEMA;
	if (timeout)
	    task->vrtxtcb.TCBSTAT |= TBSDELAY;
	xnsynch_sleep_on(&sem->synchbase,timeout,&__imutex);

	if (xnthread_test_flags(&task->threadbase, XNRMID))
	    *errp = ER_DEL; /* Semaphore deleted while pending. */
	else if (xnthread_test_flags(&task->threadbase, XNTIMEO))
	    *errp = ER_TMO; /* Timeout.*/
	}

    xnmutex_unlock(&__imutex);
}

void sc_saccept(int semid, int *errp)
{
    vrtxsem_t *sem;

    xnmutex_lock(&__imutex);
    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return;
	}

    if (sem->count > 0)
	{
	sem->count--;
	}
    else
	{
	*errp = ER_NMP;
	}
    xnmutex_unlock(&__imutex);
}

void sc_spost(int semid, int *errp)
{
    xnthread_t *waiter;
    vrtxsem_t *sem;

    xnmutex_lock(&__imutex);
    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	xnmutex_unlock(&__imutex);
	*errp = ER_ID;
	return;
	}

    *errp = RET_OK;

    waiter = xnsynch_wakeup_one_sleeper(&sem->synchbase);

    if (waiter)
	{
	xnpod_schedule(&__imutex);
	}
    else
	{
	if (sem->count == MAX_SEM_VALUE)
	    {
	    *errp = ER_OVF;
	    }
	else
	    {
	    sem->count++;
	    }
	}

    xnmutex_unlock(&__imutex);
}

int sc_sinquiry (int semid, int *errp)
{
    vrtxsem_t *sem;
    int count;

    xnmutex_lock(&__imutex);

    sem = (vrtxsem_t *)vrtx_find_object_by_id(semid);
    if (sem == NULL)
	{
	*errp = ER_ID;
	count = 0;
	}
    else
	count = sem->count;

    xnmutex_unlock(&__imutex);

    *errp = RET_OK;
    
    return count;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently
 * allowed to scan the global vrtx semaphore queue (vrtxsemq).
 */
