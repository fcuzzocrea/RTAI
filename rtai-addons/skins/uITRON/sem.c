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

#include "uITRON/task.h"
#include "uITRON/sem.h"

static xnqueue_t uisemq;

static uisem_t *uisemmap[uITRON_MAX_SEMID];

void uisem_init (void) {
    initq(&uisemq);
}

void uisem_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&uisemq)) != NULL)
	del_sem(link2uisem(holder)->semid);
}

ER cre_sem (ID semid, T_CSEM *pk_csem)

{
    uisem_t *sem;

    if (xnpod_asynch_p())
	return EN_CTXID;

    if (pk_csem->isemcnt < 0 ||
	pk_csem->maxsem < 0 ||
	pk_csem->isemcnt > pk_csem->maxsem)
	return E_PAR;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnmutex_lock(&__imutex);

    if (uisemmap[semid - 1] != NULL)
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    uisemmap[semid - 1] = (uisem_t *)1; /* Reserve slot */

    xnmutex_unlock(&__imutex);

    sem = (uisem_t *)xnmalloc(sizeof(*sem));

    if (!sem)
	{
	uisemmap[semid - 1] = NULL;
	return E_NOMEM;
	}

    xnsynch_init(&sem->synchbase,
		 (pk_csem->sematr & TA_TPRI) ? XNSYNCH_PRIO : XNSYNCH_FIFO);

    inith(&sem->link);
    sem->semid = semid;
    sem->exinf = pk_csem->exinf;
    sem->sematr = pk_csem->sematr;
    sem->semcnt = pk_csem->isemcnt;
    sem->maxsem = pk_csem->maxsem;
    sem->magic = uITRON_SEM_MAGIC;

    xnmutex_lock(&__imutex);
    uisemmap[semid - 1] = sem;
    appendq(&uisemq,&sem->link);
    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER del_sem (ID semid)

{
    uisem_t *sem;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnmutex_lock(&__imutex);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    uisemmap[semid - 1] = NULL;

    ui_mark_deleted(sem);

    if (xnsynch_destroy(&sem->synchbase) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    xnfree(sem);

    return E_OK;
}

ER sig_sem (ID semid)

{
    uitask_t *sleeper;
    uisem_t *sem;
    int err;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnmutex_lock(&__imutex);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    sleeper = thread2uitask(xnsynch_wakeup_one_sleeper(&sem->synchbase));

    if (sleeper)
	{
	xnmutex_unlock(&__imutex);
	xnpod_schedule(NULL);
	return E_OK;
	}

    err = E_OK;

    if (++sem->semcnt > sem->maxsem || sem->semcnt < 0)
	{
	sem->semcnt--;
	err = E_QOVR;
	}

    xnmutex_unlock(&__imutex);

    return err;
}

static ER wai_sem_helper (ID semid, TMO tmout)

{
    xnticks_t timeout;
    uitask_t *task;
    uisem_t *sem;
    int err;
    
    if (!xnpod_pendable_p())
	return E_CTX;

    if (tmout == TMO_FEVR)
	timeout = XN_INFINITE;
    else if (tmout == 0)
	timeout = XN_NONBLOCK;
    else if (tmout < TMO_FEVR)
	return E_PAR;
    else
	timeout = (xnticks_t)tmout;

    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnmutex_lock(&__imutex);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    err = E_OK;

    if (sem->semcnt > 0)
	sem->semcnt--;
    else if (timeout == XN_NONBLOCK)
	err = E_TMOUT;
    else
	{
	task = ui_current_task();

	xnsynch_sleep_on(&sem->synchbase,timeout,&__imutex);

	if (xnthread_test_flags(&task->threadbase,XNRMID))
	    err = E_DLT; /* Semaphore deleted while pending. */
	else if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	    err = E_TMOUT; /* Timeout.*/
	else if (xnthread_test_flags(&task->threadbase,XNBREAK))
	    err = E_RLWAI; /* rel_wai() received while waiting.*/
	}

    xnmutex_unlock(&__imutex);

    return err;
}

ER wai_sem (ID semid) {

    return wai_sem_helper(semid,TMO_FEVR);
}

ER preq_sem (ID semid) {

    return wai_sem_helper(semid,0);
}

ER twai_sem (ID semid, TMO tmout) {

    return wai_sem_helper(semid,tmout);
}

ER ref_sem (T_RSEM *pk_rsem, ID semid)

{
    uitask_t *sleeper;
    uisem_t *sem;
    
    if (semid <= 0 || semid > uITRON_MAX_SEMID)
	return E_ID;

    xnmutex_lock(&__imutex);

    sem = uisemmap[semid - 1];

    if (!sem)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    sleeper = thread2uitask(link2thread(getheadpq(xnsynch_wait_queue(&sem->synchbase)),plink));
    pk_rsem->exinf = sem->exinf;
    pk_rsem->semcnt = sem->semcnt;
    pk_rsem->wtsk = sleeper ? sleeper->tskid : FALSE;

    xnmutex_unlock(&__imutex);

    return E_OK;
}
