/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
#include "psos+/task.h"
#include "psos+/sem.h"

static xnqueue_t psossemq;

static int sm_destroy_internal(psossem_t *sem);

void psossem_init (void) {
    initq(&psossemq);
}

void psossem_cleanup (void) {

    xnholder_t *holder;

    while ((holder = getheadq(&psossemq)) != NULL)
	sm_destroy_internal(link2psossem(holder));
}

u_long sm_create (char name[4],
		  u_long icount,
		  u_long flags,
		  u_long *smid)
{
    psossem_t *sem;
    int bflags = 0;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    sem = (psossem_t *)xnmalloc(sizeof(*sem));

    if (!sem)
	return ERR_NOSCB;

    if (flags & SM_PRIOR)
	bflags |= XNSYNCH_PRIO;

    xnsynch_init(&sem->synchbase,bflags);

    inith(&sem->link);
    sem->count = icount;
    sem->magic = PSOS_SEM_MAGIC;
    sem->name[0] = name[0];
    sem->name[1] = name[1];
    sem->name[2] = name[2];
    sem->name[3] = name[3];
    sem->name[4] = '\0';
    xnmutex_lock(&__imutex);
    appendq(&psossemq,&sem->link);
    xnmutex_unlock(&__imutex);
    *smid = (u_long)sem;

    return SUCCESS;
}

static int sm_destroy_internal (psossem_t *sem)

{
    int s;

    xnmutex_lock(&__imutex);
    removeq(&psossemq,&sem->link);
    s = xnsynch_destroy(&sem->synchbase);
    psos_mark_deleted(sem);
    xnmutex_unlock(&__imutex);

    xnfree(sem);

    return s;
}

u_long sm_delete (u_long smid)

{
    psossem_t *sem;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    sem = psos_h2obj_active(smid,PSOS_SEM_MAGIC,psossem_t);

    if (!sem)
	{
	u_long err = psos_handle_error(smid,PSOS_SEM_MAGIC,psossem_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (sm_destroy_internal(sem) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long sm_ident (char name[4],
		 u_long node,
		 u_long *smid)
{
    xnholder_t *holder;
    psossem_t *sem;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (node > 1)
	return ERR_NODENO;

    xnmutex_lock(&__imutex);

    for (holder = getheadq(&psossemq);
	 holder; holder = nextq(&psossemq,holder))
	{
	sem = link2psossem(holder);

	if (sem->name[0] == name[0] &&
	    sem->name[1] == name[1] &&
	    sem->name[2] == name[2] &&
	    sem->name[3] == name[3])
	    {
	    *smid = (u_long)sem;
	    xnmutex_unlock(&__imutex);
	    return SUCCESS;
	    }
	}

    xnmutex_unlock(&__imutex);

    return ERR_OBJNF;
}

u_long sm_p (u_long smid,
	     u_long flags,
	     u_long timeout)
{
    u_long err = SUCCESS;
    psossem_t *sem;

    xnmutex_lock(&__imutex);

    sem = psos_h2obj_active(smid,PSOS_SEM_MAGIC,psossem_t);

    if (!sem)
	{
	err = psos_handle_error(smid,PSOS_SEM_MAGIC,psossem_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (flags & SM_NOWAIT)
	{
	if (sem->count > 0)
	    sem->count--;
	else
	    err = ERR_NOSEM;
	}
    else
	{
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

	if (sem->count > 0)
	    sem->count--;
	else
	    {
	    xnsynch_sleep_on(&sem->synchbase,timeout,&__imutex);

	    if (xnthread_test_flags(&psos_current_task()->threadbase,XNRMID))
		err = ERR_SKILLD; /* Semaphore deleted while pending. */
	    else if (xnthread_test_flags(&psos_current_task()->threadbase,XNTIMEO))
		err = ERR_TIMEOUT; /* Timeout.*/
	    }
	}

    xnmutex_unlock(&__imutex);

    return err;
}

u_long sm_v (u_long smid)

{
    psossem_t *sem;

    xnmutex_lock(&__imutex);

    sem = psos_h2obj_active(smid,PSOS_SEM_MAGIC,psossem_t);

    if (!sem)
	{
	u_long err = psos_handle_error(smid,PSOS_SEM_MAGIC,psossem_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (xnsynch_wakeup_one_sleeper(&sem->synchbase) != NULL)
	xnpod_schedule(&__imutex);
    else
	sem->count++;

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently not
 * allowed to scan/alter the global sema4 queue (psossemq).
 */
