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
#include "psos+/tm.h"

static xnqueue_t psostaskq;

static u_long psos_time_slice;

static void psostask_delete_hook (xnthread_t *thread)

{
    /* The scheduler is locked while hooks are running */
    psostask_t *task;
    psostm_t *tm;

    if (xnthread_magic(thread) != PSOS_SKIN_MAGIC)
	return;

    task = thread2psostask(thread);

    removeq(&psostaskq,&task->link);

    while ((tm = (psostm_t *)getgq(&task->alarmq)) != NULL)
	tm_destroy_internal(tm);

    ev_destroy(&task->evgroup);
    xnarch_delete_display(&task->threadbase);
    psos_mark_deleted(task);
    xnfree(task);
}

void psostask_init (u_long rrperiod)

{
    initq(&psostaskq);
    psos_time_slice = rrperiod;
    xnpod_add_hook(XNHOOK_THREAD_DELETE,psostask_delete_hook);
}

void psostask_cleanup (void)

{
    xnholder_t *holder;

    while ((holder = getheadq(&psostaskq)) != NULL)
	t_delete((u_long)link2psostask(holder));

    xnpod_remove_hook(XNHOOK_THREAD_DELETE,psostask_delete_hook);
}

u_long t_create (char name[4],
		 u_long prio,
		 u_long sstack,
		 u_long ustack,
		 u_long flags,
		 u_long *tid)
{
    xnflags_t bflags = 0;
    psostask_t *task;
    char aname[5];
    int n;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (prio < 1 || prio > 255)
	return ERR_PRIOR;

    aname[0] = name[0];
    aname[1] = name[1];
    aname[2] = name[2];
    aname[3] = name[3];
    aname[4] = '\0';

    task = (psostask_t *)xnmalloc(sizeof(*task));

    if (!task)
	return ERR_NOTCB;

    if (!(flags & T_SHADOW))
	{
	ustack += sstack;

	if (ustack < 1024)
	    {
	    xnfree(task);
	    return ERR_TINYSTK;
	    }

	if (flags & T_FPU)
	    bflags |= XNFPU;

	if (xnpod_init_thread(&task->threadbase,
			      aname,
			      prio,
			      bflags,
			      ustack,
			      NULL,
			      PSOS_SKIN_MAGIC) != XN_OK)
	    {
	    xnfree(task);
	    return ERR_NOSTK; /* Assume this is the only possible failure */
	    }
	}

    xnthread_time_slice(&task->threadbase) = psos_time_slice;

    ev_init(&task->evgroup);
    inith(&task->link);

    for (n = 0; n < PSOSTASK_NOTEPAD_REGS; n++)
	task->notepad[n] = 0;

    initgq(&task->alarmq,
	   &xnmod_glink_queue,
	   xnmod_alloc_glinks,
	   XNMOD_GHOLDER_THRESHOLD,
	   xnpod_get_qdir(nkpod));

    task->magic = PSOS_TASK_MAGIC;

    xnmutex_lock(&__imutex);
    appendq(&psostaskq,&task->link);
    *tid = (u_long)task;
    xnmutex_unlock(&__imutex);

    xnarch_create_display(&task->threadbase,aname,psostask);

    return SUCCESS;
}

static void psostask_trampoline (void *cookie) {

    psostask_t *task = (psostask_t *)cookie;

    task->entry(task->args[0],
		task->args[1],
		task->args[2],
		task->args[3]);

    t_delete(0);
}

u_long t_start (u_long tid,
		u_long mode,
		void (*startaddr)(u_long,u_long,u_long,u_long),
		u_long targs[])
{
    xnflags_t xnmode;
    psostask_t *task;
    int n;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (!xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return ERR_ACTIVE; /* Task already started */
	}

    xnmutex_unlock(&__imutex);

    xnmode = psos_mode_to_xeno(mode);

    for (n = 0; n < 4; n++)
	task->args[n] = targs ? targs[n] : 0;

    task->entry = startaddr;

    xnpod_start_thread(&task->threadbase,
		       xnmode,
		       (int)((mode >> 8) & 0x7),
		       psostask_trampoline,
		       task);

    return SUCCESS;
}

u_long t_restart (u_long tid,
		  u_long targs[])
{
    psostask_t *task;
    int n;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (xnthread_test_flags(&task->threadbase,XNDORMANT))
	{
	xnmutex_unlock(&__imutex);
	return ERR_NACTIVE;
	}

    for (n = 0; n < 4; n++)
	task->args[n] = targs ? targs[n] : 0;

    xnpod_restart_thread(&task->threadbase,&__imutex);

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long t_delete (u_long tid)

{
    psostask_t *task;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (tid == 0)
	xnpod_delete_self(NULL); /* Never returns */

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    xnpod_delete_thread(&task->threadbase,&__imutex);

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long t_ident (char name[4],
		u_long node,
		u_long *tid)
{
    xnholder_t *holder;
    psostask_t *task;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (node > 1)
	return ERR_NODENO;

    if (!name)
	{
	*tid = (u_long)psos_current_task();
	return SUCCESS;
	}

    xnmutex_lock(&__imutex);

    for (holder = getheadq(&psostaskq);
	 holder; holder = nextq(&psostaskq,holder))
	{
	task = link2psostask(holder);

	if (task->threadbase.name[0] == name[0] &&
	    task->threadbase.name[1] == name[1] &&
	    task->threadbase.name[2] == name[2] &&
	    task->threadbase.name[3] == name[3])
	    {
	    *tid = (u_long)task;
	    xnmutex_unlock(&__imutex);
	    return SUCCESS;
	    }
	}

    xnmutex_unlock(&__imutex);

    return ERR_OBJNF;
}

u_long t_mode (u_long clrmask,
	       u_long setmask,
	       u_long *oldmode)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    *oldmode = xeno_mode_to_psos(xnpod_set_thread_mode(&psos_current_task()->threadbase,
						       psos_mode_to_xeno(clrmask),
						       psos_mode_to_xeno(setmask)));
    *oldmode |= ((psos_current_task()->threadbase.imask & 0x7) << 8);

    return SUCCESS;
}

u_long t_getreg (u_long tid,
		 u_long regnum,
		 u_long *regvalue)
{
    psostask_t *task;

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (regnum >= PSOSTASK_NOTEPAD_REGS)
	{
	xnmutex_unlock(&__imutex);
	return ERR_REGNUM;
	}

    *regvalue = task->notepad[regnum];

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long t_resume (u_long tid)

{
    psostask_t *task;

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (!xnthread_test_flags(&task->threadbase,XNSUSP))
	{
	xnmutex_unlock(&__imutex);
	return ERR_NOTSUSP; /* Task not suspended. */
	}

    xnpod_resume_thread(&task->threadbase,XNSUSP);
    xnmutex_unlock(&__imutex);
    xnpod_schedule(NULL);

    return SUCCESS;
}

u_long t_suspend (u_long tid)

{
    psostask_t *task;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (tid == 0)
	{
	xnpod_suspend_self(NULL);
	return SUCCESS;
	}

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (xnthread_test_flags(&task->threadbase,XNSUSP))
	{
	xnmutex_unlock(&__imutex);
	return ERR_SUSP; /* Task already suspended. */
	}

    if (!xnpod_suspend_thread(&task->threadbase,
			      XNSUSP,
			      XN_INFINITE,
			      NULL,
			      &__imutex))
	xnpod_schedule(&__imutex);

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long t_setpri (u_long tid,
		 u_long newprio,
		 u_long *oldprio)
{
    psostask_t *task;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnmutex_lock(&__imutex);

    if (tid == 0)
	task = psos_current_task();
    else
	{
	task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

	if (!task)
	    {
	    u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	    xnmutex_unlock(&__imutex);
	    return err;
	    }
	}

    *oldprio = xnthread_current_priority(&task->threadbase);

    if (newprio != 0)
	{
	if (newprio < 1 || newprio > 255)
	    {
	    xnmutex_unlock(&__imutex);
	    return ERR_SETPRI;
	    }

	if (newprio != *oldprio)
	    {
	    xnpod_renice_thread(&task->threadbase,newprio);
	    xnpod_schedule(&__imutex);
	    }
	}

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

u_long t_setreg (u_long tid,
		 u_long regnum,
		 u_long regvalue)
{
    psostask_t *task;

    xnmutex_lock(&__imutex);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	u_long err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	xnmutex_unlock(&__imutex);
	return err;
	}

    if (regnum >= PSOSTASK_NOTEPAD_REGS)
	{
	xnmutex_unlock(&__imutex);
	return ERR_REGNUM;
	}

    task->notepad[regnum] = regvalue;

    xnmutex_unlock(&__imutex);

    return SUCCESS;
}

/*
 * IMPLEMENTATION NOTES:
 *
 * - Code executing on behalf of interrupt context is currently not
 * allowed to scan/alter the global psos task queue (psostaskq).
 */
