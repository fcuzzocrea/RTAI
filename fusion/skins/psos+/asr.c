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

#include "psos+/task.h"
#include "psos+/asr.h"

void psosasr_init (void) {
}

void psosasr_cleanup (void) {
}

u_long as_catch (void (*routine)(void),
		 u_long mode)
{
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);
    psos_current_task()->threadbase.asr = (xnasr_t)routine;
    psos_current_task()->threadbase.asrmode = psos_mode_to_xeno(mode);
    psos_current_task()->threadbase.asrimask = ((mode >> 8) & 0x7);
    splexit(s);

    /* The rescheduling procedure checks for pending signals. */
    xnpod_schedule();

    return SUCCESS;
}

u_long as_send (u_long tid,
		u_long signals)
{
    u_long err = SUCCESS;
    psostask_t *task;
    spl_t s;

    splhigh(s);

    task = psos_h2obj_active(tid,PSOS_TASK_MAGIC,psostask_t);

    if (!task)
	{
	err = psos_handle_error(tid,PSOS_TASK_MAGIC,psostask_t);
	goto unlock_and_exit;
	}

    if (task->threadbase.asr == XNTHREAD_INVALID_ASR)
	{
	err = ERR_NOASR;
	goto unlock_and_exit;
	}

    if (signals > 0)
	{
	task->threadbase.signals |= signals;

	if (xnpod_current_thread() == &task->threadbase)
	    xnpod_schedule();
	}

 unlock_and_exit:

    splexit(s);

    return err;
}
