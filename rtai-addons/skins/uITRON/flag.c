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
#include "uITRON/flag.h"

static xnqueue_t uiflagq;

static uiflag_t *uiflagmap[uITRON_MAX_FLAGID];

void uiflag_init (void) {
    initq(&uiflagq);
}

void uiflag_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&uiflagq)) != NULL)
	del_flg(link2uiflag(holder)->flgid);
}

ER cre_flg (ID flgid, T_CFLG *pk_cflg)

{
    uiflag_t *flg;

    if (xnpod_asynch_p())
	return EN_CTXID;

    if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
	return E_ID;

    xnmutex_lock(&__imutex);

    if (uiflagmap[flgid - 1] != NULL)
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    uiflagmap[flgid - 1] = (uiflag_t *)1; /* Reserve slot */

    xnmutex_unlock(&__imutex);

    flg = (uiflag_t *)xnmalloc(sizeof(*flg));

    if (!flg)
	{
	uiflagmap[flgid - 1] = NULL;
	return E_NOMEM;
	}

    xnsynch_init(&flg->synchbase,XNSYNCH_FIFO);

    inith(&flg->link);
    flg->flgid = flgid;
    flg->exinf = pk_cflg->exinf;
    flg->flgatr = pk_cflg->flgatr;
    flg->flgvalue = pk_cflg->iflgptn;
    flg->magic = uITRON_FLAG_MAGIC;

    xnmutex_lock(&__imutex);
    uiflagmap[flgid - 1] = flg;
    appendq(&uiflagq,&flg->link);
    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER del_flg (ID flgid)

{
    uiflag_t *flg;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
	return E_ID;

    xnmutex_lock(&__imutex);

    flg = uiflagmap[flgid - 1];

    if (!flg)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    uiflagmap[flgid - 1] = NULL;

    ui_mark_deleted(flg);

    if (xnsynch_destroy(&flg->synchbase) == XNSYNCH_RESCHED)
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    xnfree(flg);

    return E_OK;
}

ER set_flg (ID flgid, UINT setptn)

{
    xnpholder_t *holder, *nholder;
    uiflag_t *flg;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
	return E_ID;

    if (setptn == 0)
	return E_OK;

    xnmutex_lock(&__imutex);

    flg = uiflagmap[flgid - 1];

    if (!flg)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    flg->flgvalue |= setptn;

    if (xnsynch_nsleepers(&flg->synchbase) > 0)
	{
	for (holder = getheadpq(xnsynch_wait_queue(&flg->synchbase));
	     holder; holder = nholder)
	    {
	    uitask_t *sleeper = thread2uitask(link2thread(holder,plink));
	    UINT wfmode = sleeper->wargs.flag.wfmode;
	    UINT waiptn = sleeper->wargs.flag.waiptn;

	    if (((wfmode & TWF_ORW) && (waiptn & flg->flgvalue) != 0) ||
		(!(wfmode & TWF_ORW) && ((waiptn & flg->flgvalue) == waiptn)))
		{
		nholder = xnsynch_wakeup_this_sleeper(&flg->synchbase,holder);
		sleeper->wargs.flag.waiptn = flg->flgvalue;

		if (wfmode & TWF_CLR)
		    flg->flgvalue = 0;
		}
	    else
		nholder = nextpq(xnsynch_wait_queue(&flg->synchbase),holder);
	    }

	xnpod_schedule(&__imutex);
	}

    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER clr_flg (ID flgid, UINT clrptn)

{
    uiflag_t *flg;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
	return E_ID;

    xnmutex_lock(&__imutex);

    flg = uiflagmap[flgid - 1];

    if (!flg)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    flg->flgvalue &= clrptn;

    xnmutex_unlock(&__imutex);

    return E_OK;
}

static ER wai_flg_helper (UINT *p_flgptn,
			  ID flgid,
			  UINT waiptn,
			  UINT wfmode,
			  TMO tmout)
{
    xnticks_t timeout;
    uitask_t *task;
    uiflag_t *flg;
    int err;

    if (!xnpod_pendable_p())
	return E_CTX;

    if (waiptn == 0)
	return E_PAR;

    if (tmout == TMO_FEVR)
	timeout = XN_INFINITE;
    else if (tmout == 0)
	timeout = XN_NONBLOCK;
    else if (tmout < TMO_FEVR)
	return E_PAR;
    else
	timeout = (xnticks_t)tmout;

    if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
	return E_ID;

    xnmutex_lock(&__imutex);

    flg = uiflagmap[flgid - 1];

    if (!flg)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    err = E_OK;

    if (((wfmode & TWF_ORW) && (waiptn & flg->flgvalue) != 0) ||
	(!(wfmode & TWF_ORW) && ((waiptn & flg->flgvalue) == waiptn)))
	{
	*p_flgptn = flg->flgvalue;

	if (wfmode & TWF_CLR)
	    flg->flgvalue = 0;
	}
    else if (timeout == XN_NONBLOCK)
	err = E_TMOUT;
    else if (xnsynch_nsleepers(&flg->synchbase) > 0 &&
	     !(flg->flgatr & TA_WMUL))
	err = E_OBJ;
    else
	{
	task = ui_current_task();

	xnsynch_sleep_on(&flg->synchbase,timeout,&__imutex);

	if (xnthread_test_flags(&task->threadbase,XNRMID))
	    err = E_DLT; /* Flag deleted while pending. */
	else if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	    err = E_TMOUT; /* Timeout.*/
	else if (xnthread_test_flags(&task->threadbase,XNBREAK))
	    err = E_RLWAI; /* rel_wai() received while waiting.*/
	else
	    *p_flgptn = task->wargs.flag.waiptn;
	}

    xnmutex_unlock(&__imutex);

    return err;
}

ER wai_flg (UINT *p_flgptn,
	    ID flgid,
	    UINT waiptn,
	    UINT wfmode) {

    return wai_flg_helper(p_flgptn,flgid,waiptn,wfmode,TMO_FEVR);
}

ER pol_flg (UINT *p_flgptn,
	    ID flgid,
	    UINT waiptn,
	    UINT wfmode) {

    return wai_flg_helper(p_flgptn,flgid,waiptn,wfmode,0);
}

ER twai_flg (UINT *p_flgptn,
	     ID flgid,
	     UINT waiptn,
	     UINT wfmode,
	     TMO tmout) {

    return wai_flg_helper(p_flgptn,flgid,waiptn,wfmode,tmout);
}

ER ref_flg (T_RFLG *pk_rflg, ID flgid)

{
    uitask_t *sleeper;
    uiflag_t *flg;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (flgid <= 0 || flgid > uITRON_MAX_FLAGID)
	return E_ID;

    xnmutex_lock(&__imutex);

    flg = uiflagmap[flgid - 1];

    if (!flg)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    sleeper = thread2uitask(link2thread(getheadpq(xnsynch_wait_queue(&flg->synchbase)),plink));
    pk_rflg->exinf = flg->exinf;
    pk_rflg->flgptn = flg->flgvalue;
    pk_rflg->wtsk = sleeper ? sleeper->tskid : FALSE;

    xnmutex_unlock(&__imutex);

    return E_OK;
}
