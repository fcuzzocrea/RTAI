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

static xnqueue_t uitaskq;

static uitask_t *uitaskmap[uITRON_MAX_TASKID];

static int uicpulck;

static void uitask_delete_hook (xnthread_t *thread)

{
    uitask_t *task;

    if (xnthread_magic(thread) != uITRON_SKIN_MAGIC)
	return;

    task = thread2uitask(thread);

    removeq(&uitaskq,&task->link);
#if 0
    xnarch_delete_display(&task->threadbase);
#endif
    ui_mark_deleted(task);
    xnfree(task);
}

void uitask_init (void)

{
    initq(&uitaskq);
    xnpod_add_hook(XNHOOK_THREAD_DELETE,uitask_delete_hook);
}

void uitask_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&uitaskq)) != NULL)
	del_tsk(link2uitask(holder)->tskid);

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,uitask_delete_hook);
}

ER cre_tsk (ID tskid, T_CTSK *pk_ctsk)

{
    uitask_t *task;
    char aname[16];

    if (xnpod_asynch_p())
	return EN_CTXID;

    /* uITRON uses a (rather widespread) reverse priority scheme: the
       lower the value, the higher the priority. */
    if (pk_ctsk->itskpri < uITRON_MAX_PRI ||
	pk_ctsk->itskpri > uITRON_MIN_PRI)
	return E_PAR;

    if (pk_ctsk->stksz < 1024)
	return E_PAR;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    xnmutex_lock(&__imutex);

    if (uitaskmap[tskid - 1] != NULL)
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    uitaskmap[tskid - 1] = (uitask_t *)1; /* Reserve slot */

    xnmutex_unlock(&__imutex);

    task = (uitask_t *)xnmalloc(sizeof(*task));

    if (!task)
	{
	uitaskmap[tskid - 1] = NULL;
	return E_NOMEM;
	}

    sprintf(aname,"t%d",tskid);

    if (xnpod_init_thread(&task->threadbase,
			  aname,
			  pk_ctsk->itskpri,
			  XNFPU,
			  pk_ctsk->stksz,
			  NULL,
			  uITRON_SKIN_MAGIC) != XN_OK)
	{
	uitaskmap[tskid - 1] = NULL;
	xnfree(task);
	return E_NOMEM;
	}

    inith(&task->link);
    task->tskid = tskid;
    task->entry = pk_ctsk->task;
    task->exinf = pk_ctsk->exinf;
    task->tskatr = pk_ctsk->tskatr;
    task->suspcnt = 0;
    task->wkupcnt = 0;
    task->waitinfo = 0;
    task->magic = uITRON_TASK_MAGIC;

    xnmutex_lock(&__imutex);
    uitaskmap[tskid - 1] = task;
    appendq(&uitaskq,&task->link);
    xnmutex_unlock(&__imutex);

#if 0
    xnarch_create_display(&task->threadbase,aname,uitask);
#endif

    return E_OK;
}

ER del_tsk (ID tskid)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    xnmutex_lock(&__imutex);

    task = uitaskmap[tskid - 1];

    if (!task)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    if (!xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    uitaskmap[tskid - 1] = NULL;

    xnpod_delete_thread(&task->threadbase,&__imutex);

    xnmutex_unlock(&__imutex);

    return E_OK;
}

static void uitask_trampoline (void *cookie)

{
    uitask_t *task = (uitask_t *)cookie;
    void (*entry)(INT) = (void (*)(INT))task->entry;
    entry(task->stacd);
    ext_tsk();
}

ER sta_tsk (ID tskid, INT stacd)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    xnmutex_lock(&__imutex);

    task = uitaskmap[tskid - 1];

    if (!task)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    if (!xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    task->suspcnt = 0;
    task->wkupcnt = 0;
    task->waitinfo = 0;
    task->stacd = stacd;

    xnmutex_unlock(&__imutex);

    xnpod_start_thread(&task->threadbase,
		       0,
		       0,
		       uitask_trampoline,
		       task);
    
    xnpod_resume_thread(&task->threadbase,XNDORMANT);

    return E_OK;
}

void ext_tsk (void)

{
    if (xnpod_asynch_p())
	{
	xnpod_fatal("ext_tsk() not called on behalf of a task");
	return;
	}

    if (xnpod_locked_p())
	{
	xnpod_fatal("ext_tsk() called while in dispatch-disabled state");
	return;
	}

    xnpod_suspend_thread(&ui_current_task()->threadbase,
			 XNDORMANT,
			 XN_INFINITE,
			 NULL,
			 NULL);
}

void exd_tsk (void)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	{
	xnpod_fatal("exd_tsk() not called on behalf of a task");
	return;
	}

    if (xnpod_locked_p())
	{
	xnpod_fatal("exd_tsk() called while in dispatch-disabled state");
	return;
	}

    task = ui_current_task();
    xnmutex_lock(&__imutex);
    uitaskmap[task->tskid - 1] = NULL;
    xnpod_delete_thread(&task->threadbase,&__imutex);
    xnmutex_unlock(&__imutex);
}

/* Helper routine for the task termination -- must be called
   on behalf a safe context since it does not enforce any
   critical section. */

static void ter_tsk_helper (uitask_t *task)

{
    xnmutex_lock(&__imutex);

    xnthread_clear_flags(&task->threadbase,uITRON_TERM_HOLD);

    if (xnthread_test_flags(&task->threadbase,XNSUSP))
	xnpod_resume_thread(&task->threadbase,XNSUSP);

    xnpod_unblock_thread(&task->threadbase);

    if (!xnpod_suspend_thread(&task->threadbase,
			      XNDORMANT,
			      XN_INFINITE,
			      NULL,
			      &__imutex))
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);
}

ER ter_tsk (ID tskid)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    if (tskid == ui_current_task()->tskid)
	return E_OBJ;

    xnmutex_lock(&__imutex);

    task = uitaskmap[tskid - 1];

    if (!task)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    if (xnthread_test_flags(&task->threadbase,XNLOCK))
	{
	/* We must be running on behalf of an IST here, so we only
	   mark the target task as held for termination. The actual
	   termination code will be applied by the task itself when it
	   re-enables dispatching. */
	xnmutex_unlock(&__imutex);
	xnthread_set_flags(&task->threadbase,uITRON_TERM_HOLD);
	return E_OK;
	}

    ter_tsk_helper(task);

    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER dis_dsp (void)

{
    if (xnpod_asynch_p() || uicpulck)
	return E_CTX;

    xnmutex_lock(&__imutex);

    if (!xnpod_locked_p())
	xnpod_lock_sched();

    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER ena_dsp (void)

{ 
    if (xnpod_asynch_p() || uicpulck)
	return E_CTX;

    if (xnpod_locked_p())
	{
	xnpod_unlock_sched();

	if (xnthread_test_flags(&ui_current_task()->threadbase,
				uITRON_TERM_HOLD))
	    ter_tsk_helper(ui_current_task());
	}

    return E_OK;
}

ER chg_pri (ID tskid, PRI tskpri)

{
    uitask_t *task;

    if (tskpri != TPRI_INI)
	{
	/* uITRON uses a (rather widespread) reverse priority scheme: the
	   lower the value, the higher the priority. */
	if (tskpri < uITRON_MAX_PRI || tskpri > uITRON_MIN_PRI)
	    return E_PAR;
	}

    if (tskid == TSK_SELF)
	{
	if (xnpod_asynch_p())
	    return E_ID;

	task = ui_current_task();
	xnmutex_lock(&__imutex);
	}
    else
	{
	if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	    return E_ID;

	xnmutex_lock(&__imutex);

	task = uitaskmap[tskid - 1];

	if (!task)
	    {
	    xnmutex_unlock(&__imutex);
	    return E_NOEXS;
	    }

	if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	    {
	    xnmutex_unlock(&__imutex);
	    return E_OBJ;
	    }
	}

    if (tskpri == TPRI_INI)
	tskpri = xnthread_initial_priority(&task->threadbase);

    /* uITRON specs explicitely states: "If the priority specified is
       the same as the current priority, the task will still be moved
       behind other tasks of the same priority". This allows for
       manual round-robin. Cool! :o) */
    xnpod_renice_thread(&task->threadbase,tskpri);
    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);

    return E_OK;
}

ER rot_rdq (PRI tskpri)

{
    if (tskpri != TPRI_RUN)
	{
	/* uITRON uses a (rather widespread) reverse priority scheme: the
	   lower the value, the higher the priority. */
	if (tskpri < uITRON_MAX_PRI || tskpri > uITRON_MIN_PRI)
	    return E_PAR;
	}
    else if (xnpod_asynch_p())
	tskpri = XNPOD_RUNPRI;
    else
	tskpri = xnthread_current_priority(&ui_current_task()->threadbase);

    xnpod_rotate_readyq(tskpri);
    xnpod_schedule(NULL);

    return E_OK;
}

ER rel_wai (ID tskid)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    if (tskid == ui_current_task()->tskid)
	return E_OBJ;

    xnmutex_lock(&__imutex);

    task = uitaskmap[tskid - 1];

    if (!task)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    xnpod_unblock_thread(&task->threadbase);
    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);

    return E_OK;
}

ER get_tid (ID *p_tskid)

{
    if (xnpod_asynch_p())
	*p_tskid = FALSE;
    else
	*p_tskid = ui_current_task()->tskid;

    return E_OK;
}

ER ref_tsk (T_RTSK *pk_rtsk, ID tskid)

{
    UINT tskstat = 0;
    uitask_t *task;

    if (tskid == TSK_SELF)
	{
	if (xnpod_asynch_p())
	    return E_ID;

	task = ui_current_task();
	xnmutex_lock(&__imutex);
	}
    else
	{
	if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	    return E_ID;

	xnmutex_lock(&__imutex);

	task = uitaskmap[tskid - 1];

	if (!task)
	    {
	    xnmutex_unlock(&__imutex);
	    return E_NOEXS;
	    }
	}

    if (task == ui_current_task())
	setbits(tskstat,TTS_RUN);
    else if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	setbits(tskstat,TTS_DMT);
    else if (xnthread_test_flags(&task->threadbase,XNREADY))
	setbits(tskstat,TTS_RDY);
    else
	{
	if (xnthread_test_flags(&task->threadbase,XNPEND))
	    setbits(tskstat,TTS_WAI);
	if (xnthread_test_flags(&task->threadbase,XNSUSP))
	    setbits(tskstat,TTS_SUS);
	}

    pk_rtsk->exinf = task->exinf;
    pk_rtsk->tskpri = xnthread_current_priority(&task->threadbase);
    pk_rtsk->tskstat = tskstat;
    pk_rtsk->suscnt = task->suspcnt;
    pk_rtsk->wupcnt = task->wkupcnt;
    pk_rtsk->tskwait = testbits(tskstat,TTS_WAI) ? task->waitinfo : 0;
    pk_rtsk->wid = 0;		/* FIXME */
    pk_rtsk->tskatr = task->tskatr;
    pk_rtsk->task = task->entry;
    pk_rtsk->itskpri = xnthread_initial_priority(&task->threadbase);
    pk_rtsk->stksz = (INT)xnthread_stack_size(&task->threadbase);

    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER sus_tsk (ID tskid)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    if (tskid == ui_current_task()->tskid)
	return E_OBJ;

    xnmutex_lock(&__imutex);

    task = uitaskmap[tskid - 1];

    if (!task)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    if (task->suspcnt >= 0x7fffffff)
	{
	xnmutex_unlock(&__imutex);
	return E_QOVR;
	}

    if (task->suspcnt++ == 0)
	{
	xnpod_suspend_thread(&task->threadbase,
			     XNSUSP,
			     XN_INFINITE,
			     NULL,
			     &__imutex);
	xnpod_schedule(&__imutex);
	}

    xnmutex_unlock(&__imutex);

    return E_OK;
}

static ER rsm_tsk_helper (ID tskid, int force)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    if (tskid == ui_current_task()->tskid)
	return E_OBJ;

    xnmutex_lock(&__imutex);

    task = uitaskmap[tskid - 1];

    if (!task)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    if (task->suspcnt == 0 ||
	xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    if (force || --task->suspcnt == 0)
	{
	task->suspcnt = 0;
	xnpod_resume_thread(&task->threadbase,XNSUSP);
	xnpod_schedule(&__imutex);
	}

    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER rsm_tsk (ID tskid) {
    return rsm_tsk_helper(tskid,0);
}

ER frsm_tsk (ID tskid) {
    return rsm_tsk_helper(tskid,1);
}

ER slp_tsk (void)

{
    uitask_t *task;

    if (!xnpod_pendable_p())
	return E_CTX;

    task = ui_current_task();

    xnmutex_lock(&__imutex);

    if (task->wkupcnt > 0)
	{
	task->wkupcnt--;
	xnmutex_unlock(&__imutex);
	return E_OK;
	}

    xnthread_set_flags(&task->threadbase,uITRON_TASK_SLEEP);

    xnpod_suspend_thread(&task->threadbase,
			 XNDELAY,
			 XN_INFINITE,
			 NULL,
			 &__imutex);

    xnthread_clear_flags(&task->threadbase,uITRON_TASK_SLEEP);

    xnmutex_unlock(&__imutex);

    if (xnthread_test_flags(&task->threadbase,XNBREAK))
	return E_RLWAI;

    return E_OK;
}

ER tslp_tsk (TMO tmout)

{
    uitask_t *task;

    if (!xnpod_pendable_p())
	return E_CTX;

    if (tmout == 0)
	return E_TMOUT;

    if (tmout < TMO_FEVR)
	return E_PAR;

    task = ui_current_task();

    xnmutex_lock(&__imutex);

    if (task->wkupcnt > 0)
	{
	task->wkupcnt--;
	xnmutex_unlock(&__imutex);
	return E_OK;
	}

    if (tmout == TMO_FEVR)
	tmout = XN_INFINITE;

    xnthread_set_flags(&task->threadbase,uITRON_TASK_SLEEP);

    xnpod_suspend_thread(&task->threadbase,
			 XNDELAY,
			 tmout,
			 NULL,
			 &__imutex);

    xnthread_clear_flags(&task->threadbase,uITRON_TASK_SLEEP);

    xnmutex_unlock(&__imutex);

    if (xnthread_test_flags(&task->threadbase,XNBREAK))
	return E_RLWAI;

    if (xnthread_test_flags(&task->threadbase,XNTIMEO))
	return E_TMOUT;

    return E_OK;
}

ER wup_tsk (ID tskid)

{
    uitask_t *task;
    
    if (xnpod_asynch_p())
	return EN_CTXID;

    if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	return E_ID;

    if (tskid == ui_current_task()->tskid)
	return E_OBJ;

    xnmutex_lock(&__imutex);

    task = uitaskmap[tskid - 1];

    if (!task)
	{
	xnmutex_unlock(&__imutex);
	return E_NOEXS;
	}

    if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return E_OBJ;
	}

    if (!xnthread_test_flags(&task->threadbase,uITRON_TASK_SLEEP))
	{
	if (task->wkupcnt >= 0x7fffffff)
	    {
	    xnmutex_unlock(&__imutex);
	    return E_QOVR;
	    }

	task->wkupcnt++;
	}
    else
	{
	xnpod_resume_thread(&task->threadbase,XNDELAY);
	xnpod_schedule(&__imutex);
	}

    xnmutex_unlock(&__imutex);

    return E_OK;
}

ER can_wup (INT *p_wupcnt, ID tskid)

{
    uitask_t *task;
    
    if (tskid == TSK_SELF)
	{
	if (xnpod_asynch_p())
	    return E_ID;

	task = ui_current_task();
	xnmutex_lock(&__imutex);
	}
    else
	{
	if (tskid <= 0 || tskid > uITRON_MAX_TASKID)
	    return E_ID;

	xnmutex_lock(&__imutex);

	task = uitaskmap[tskid - 1];

	if (!task)
	    {
	    xnmutex_unlock(&__imutex);
	    return E_NOEXS;
	    }

	if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	    {
	    xnmutex_unlock(&__imutex);
	    return E_OBJ;
	    }
	}

    *p_wupcnt = task->wkupcnt;
    task->wkupcnt = 0;

    xnmutex_unlock(&__imutex);

    return E_OK;
}
