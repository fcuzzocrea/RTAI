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
#include "psos+/event.h"
#include "psos+/task.h"
#include "psos+/sem.h"
#include "psos+/asr.h"
#include "psos+/queue.h"
#include "psos+/pt.h"
#include "psos+/rn.h"
#include "psos+/tm.h"

MODULE_DESCRIPTION("XENOMAI-based pSOS+(R) API emulator");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static u_long rn0_size_arg = 32 * 1024; /* Default size of region #0 */
MODULE_PARM(rn0_size_arg,"i");
MODULE_PARM_DESC(rn0_size_arg,"Size of pSOS+ region #0 (in bytes)");

static u_long tick_hz_arg = 1000; /* Default tick period */
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz)");

static u_long time_slice_arg = 10; /* Default (round-robin) time slice */
MODULE_PARM(time_slice_arg,"i");
MODULE_PARM_DESC(time_slice_arg,"Default time slice (in ticks)");

static xnpod_t pod;

xnmutex_t __imutex;

static void psos_shutdown (int xtype)

{
    xnpod_lock_sched();
    xnpod_stop_timer();

    psostask_cleanup();
    psostm_cleanup();
    psosasr_cleanup();
    psospt_cleanup();
    psosqueue_cleanup();
    psossem_cleanup();
    psosrn_cleanup();

    xnpod_shutdown(xtype);
}

void k_fatal (u_long err_code, u_long flags) {

    xnpod_fatal("pSOS/VM: fatal error, code 0x%x",err_code);
}

int SKIN_INIT_MODULE (void)

{
    u_long nstick = XNPOD_DEFAULT_TICK;
    int err;

    err = xnpod_init(&pod,1,255,0);

    if (err != XN_OK)
	return err;

    xnmutex_init(&__imutex);

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
	nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);

    err = xnpod_start_timer(nstick,(xnist_t)&tm_tick);

    if (err == XN_OK)
	{
	err = psosrn_init(MODULE_PARM_VALUE(rn0_size_arg));

	if (err != XN_OK)
	    {
	    xnpod_shutdown(XNPOD_FATAL_EXIT);
	    return err;
	    }
	}

    psossem_init();
    psosqueue_init();
    psospt_init();
    psosasr_init();
    psostm_init();
    psostask_init(MODULE_PARM_VALUE(time_slice_arg));

    pod.svctable.shutdown = &psos_shutdown;

    xnprintf("pSOS/VM: starting services.\n");

    return err;
}

void SKIN_CLEANUP_MODULE (void) {

    xnprintf("pSOS/VM: stopping services.\n");
    psos_shutdown(XNPOD_NORMAL_EXIT);
}

EXPORT_SYMBOL(as_catch);
EXPORT_SYMBOL(as_send);
EXPORT_SYMBOL(ev_receive);
EXPORT_SYMBOL(ev_send);
EXPORT_SYMBOL(k_fatal);
EXPORT_SYMBOL(pt_create);
EXPORT_SYMBOL(pt_delete);
EXPORT_SYMBOL(pt_getbuf);
EXPORT_SYMBOL(pt_ident);
EXPORT_SYMBOL(pt_retbuf);
EXPORT_SYMBOL(q_broadcast);
EXPORT_SYMBOL(q_create);
EXPORT_SYMBOL(q_delete);
EXPORT_SYMBOL(q_ident);
EXPORT_SYMBOL(q_receive);
EXPORT_SYMBOL(q_send);
EXPORT_SYMBOL(q_urgent);
EXPORT_SYMBOL(q_vbroadcast);
EXPORT_SYMBOL(q_vcreate);
EXPORT_SYMBOL(q_vdelete);
EXPORT_SYMBOL(q_vident);
EXPORT_SYMBOL(q_vreceive);
EXPORT_SYMBOL(q_vsend);
EXPORT_SYMBOL(q_vurgent);
EXPORT_SYMBOL(rn_create);
EXPORT_SYMBOL(rn_delete);
EXPORT_SYMBOL(rn_getseg);
EXPORT_SYMBOL(rn_ident);
EXPORT_SYMBOL(rn_retseg);
EXPORT_SYMBOL(sm_create);
EXPORT_SYMBOL(sm_delete);
EXPORT_SYMBOL(sm_ident);
EXPORT_SYMBOL(sm_p);
EXPORT_SYMBOL(sm_v);
EXPORT_SYMBOL(t_create);
EXPORT_SYMBOL(t_delete);
EXPORT_SYMBOL(t_getreg);
EXPORT_SYMBOL(t_ident);
EXPORT_SYMBOL(t_mode);
EXPORT_SYMBOL(t_restart);
EXPORT_SYMBOL(t_resume);
EXPORT_SYMBOL(t_setpri);
EXPORT_SYMBOL(t_setreg);
EXPORT_SYMBOL(t_start);
EXPORT_SYMBOL(t_suspend);
EXPORT_SYMBOL(tm_cancel);
EXPORT_SYMBOL(tm_evafter);
EXPORT_SYMBOL(tm_evevery);
EXPORT_SYMBOL(tm_evwhen);
EXPORT_SYMBOL(tm_get);
EXPORT_SYMBOL(tm_set);
EXPORT_SYMBOL(tm_tick);
EXPORT_SYMBOL(tm_wkafter);
EXPORT_SYMBOL(tm_wkwhen);
