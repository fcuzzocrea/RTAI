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
#include "uITRON/mbx.h"

static xnqueue_t uimbxq;

static uimbx_t *uimbxmap[uITRON_MAX_MBXID];

void uimbx_init (void) {
    initq(&uimbxq);
}

void uimbx_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&uimbxq)) != NULL)
	del_mbx(link2uimbx(holder)->mbxid);
}

ER cre_mbx (ID mbxid, T_CMBX *pk_cmbx)

{
    uimbx_t *mbx;
    T_MSG **ring;

    if (xnpod_asynch_p())
	return EN_CTXID;

    if (mbxid <= 0 || mbxid > uITRON_MAX_MBXID)
	return E_ID;

    if (pk_cmbx->bufcnt <= 0)
	return E_PAR;

    if (pk_cmbx->mbxatr & TA_MPRI)
	return E_RSATR;

    xnmutex_lock(&__imutex);

    if (uimbxmap[mbxid - 1] != NULL)
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    uimbxmap[mbxid - 1] = (uimbx_t *)1; /* Reserve slot */

    xnmutex_unlock(&__imutex);

    mbx = (uimbx_t *)xnmalloc(sizeof(*mbx));

    if (!mbx)
	{
	uimbxmap[mbxid - 1] = NULL;
	return E_NOMEM;
	}

    ring = (T_MSG **)xnmalloc(sizeof(T_MSG *) * pk_cmbx->bufcnt);

    if (!ring)
	{
	uimbxmap[mbxid - 1] = NULL;
	return E_NOMEM;
	}

    xnsynch_init(&mbx->synchbase,
		 (pk_cmbx->mbxatr & TA_TPRI) ? XNSYNCH_PRIO : XNSYNCH_FIFO);

    inith(&mbx->link);
    mbx->mbxid = mbxid;
    mbx->exinf = pk_cmbx->exinf;
    mbx->mbxatr = pk_cmbx->mbxatr;
    mbx->bufcnt = pk_cmbx->bufcnt;
    mbx->rdptr = 0;
    mbx->wrptr = 0;
    mbx->mcount = 0;
    mbx->ring = ring;
    mbx->magic = uITRON_MBX_MAGIC;

    xnmutex_lock(&__imutex);
    uimbxmap[mbxid - 1] = mbx;
    appendq(&uimbxq,&mbx->link);
    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER del_mbx (ID mbxid)

{
    uimbx_t *mbx;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (mbxid <= 0 || mbxid > uITRON_MAX_MBXID)
	return E_ID;

    xnmutex_lock(&__imutex);

    mbx = uimbxmap[mbxid - 1];

    if (!mbx)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    uimbxmap[mbxid - 1] = NULL;

    ui_mark_deleted(mbx);

    if (xnsynch_destroy(&mbx->synchbase) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    xnfree(mbx->ring);
    xnfree(mbx);

    return E_OK;
}

ER snd_msg (ID mbxid, T_MSG *pk_msg)

{
    uitask_t *sleeper;
    int err, wrptr;
    uimbx_t *mbx;
    
    if (mbxid <= 0 || mbxid > uITRON_MAX_MBXID)
	return E_ID;

    xnmutex_lock(&__imutex);

    mbx = uimbxmap[mbxid - 1];

    if (!mbx)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    sleeper = thread2uitask(xnsynch_wakeup_one_sleeper(&mbx->synchbase));

    if (sleeper)
	{
	sleeper->wargs.msg = pk_msg;
	xnmutex_unlock(&__imutex);
	xnpod_schedule(NULL);
	return E_OK;
	}

    err = E_OK;
    wrptr = mbx->wrptr;

    if (mbx->mcount > 0 && wrptr == mbx->rdptr)
	err = E_QOVR;
    else
	{
	mbx->ring[wrptr] = pk_msg;
	mbx->wrptr = (wrptr + 1) % mbx->bufcnt;
	mbx->mcount++;
	}

    xnmutex_unlock(&__imutex);

    return err;
}

static ER rcv_msg_helper (T_MSG **ppk_msg, ID mbxid, TMO tmout)

{
    xnticks_t timeout;
    uitask_t *task;
    uimbx_t *mbx;
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

    if (mbxid <= 0 || mbxid > uITRON_MAX_MBXID)
	return E_ID;

    xnmutex_lock(&__imutex);

    mbx = uimbxmap[mbxid - 1];

    if (!mbx)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    err = E_OK;

    if (mbx->mcount > 0)
	{
	*ppk_msg = mbx->ring[mbx->rdptr];
	mbx->rdptr = (mbx->rdptr + 1) % mbx->bufcnt;
	mbx->mcount--;
	}
    else if (timeout == XN_NONBLOCK)
	err = E_TMOUT;
    else
	{
	task = ui_current_task();

	xnsynch_sleep_on(&mbx->synchbase,timeout,&__imutex);

	if (xnthread_test_flags(&task->threadbase,XNRMID))
	    err = E_DLT; /* Flag deleted while pending. */
	else if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	    err = E_TMOUT; /* Timeout.*/
	else if (xnthread_test_flags(&task->threadbase,XNBREAK))
	    err = E_RLWAI; /* rel_wai() received while waiting.*/
	else
	    *ppk_msg = task->wargs.msg;
	}

    xnmutex_unlock(&__imutex);

    return err;
}

ER rcv_msg (T_MSG **ppk_msg, ID mbxid) {

    return rcv_msg_helper(ppk_msg,mbxid,TMO_FEVR);
}

ER prcv_msg(T_MSG **ppk_msg, ID mbxid) {

    return rcv_msg_helper(ppk_msg,mbxid,0);
}

ER trcv_msg (T_MSG **ppk_msg, ID mbxid, TMO tmout) {

    return rcv_msg_helper(ppk_msg,mbxid,tmout);
}

ER ref_mbx (T_RMBX *pk_rmbx, ID mbxid)

{
    uitask_t *sleeper;
    uimbx_t *mbx;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (mbxid <= 0 || mbxid > uITRON_MAX_FLAGID)
	return E_ID;

    xnmutex_lock(&__imutex);

    mbx = uimbxmap[mbxid - 1];

    if (!mbx)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    sleeper = thread2uitask(link2thread(getheadpq(xnsynch_wait_queue(&mbx->synchbase)),plink));
    pk_rmbx->exinf = mbx->exinf;
    pk_rmbx->pk_msg = mbx->mcount > 0 ? mbx->ring[mbx->rdptr] : (T_MSG *)NADR;
    pk_rmbx->wtsk = sleeper ? sleeper->tskid : FALSE;

    xnmutex_unlock(&__imutex);

    return E_OK;
}
