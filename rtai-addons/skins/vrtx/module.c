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
#include "vrtx/event.h"
#include "vrtx/task.h"
#include "vrtx/sem.h"
#include "vrtx/mb.h"
#include "vrtx/mx.h"
#include "vrtx/queue.h"
#include "vrtx/pt.h"
#include "vrtx/heap.h"

MODULE_DESCRIPTION("XENOMAI-based VRTX(R) API emulator");
MODULE_AUTHOR("jpinon@idealx.com");
MODULE_LICENSE("GPL");

static u_long workspace_size_arg = 32 * 1024; /* Default size of VRTX workspace */
MODULE_PARM(workspace_size_arg,"i");
MODULE_PARM_DESC(workspace_size_arg,"Size of VRTX workspace (in bytes)");

static u_long tick_hz_arg = 100; /* Default tick period */
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz)");

static u_long task_stacksize_arg = 4096; /* Default size of VRTX tasks */
MODULE_PARM(task_stacksize_arg,"i");
MODULE_PARM_DESC(task_stacksize_arg,"Default size of VRTX task stack (in bytes)");

static xnpod_t pod;

static short vrtxidgen[VRTX_MAX_CB], vrtxidfree;

xnmutex_t __imutex;

void *vrtxobjmap[VRTX_MAX_CB];

int vrtx_alloc_id (void *refobject)

{
    int freeid;

    xnmutex_lock(&__imutex);

    freeid = vrtxidfree;

    if (freeid >= 0)
	{
	vrtxobjmap[freeid] = refobject;
	vrtxidfree = vrtxidgen[freeid];
	}

    xnmutex_unlock(&__imutex);

    return freeid;
}

void vrtx_release_id (int id)

{
    xnmutex_lock(&__imutex);
    vrtxobjmap[id] = NULL;
    vrtxidgen[id] = vrtxidfree;
    vrtxidfree = id;
    xnmutex_unlock(&__imutex);
}

void *vrtx_find_object_by_id (int id)

{
    if (id < 0 || id >= VRTX_MAX_CB)
	return NULL;

    return vrtxobjmap[id];
}

static void vrtx_shutdown (int xtype)

{
    xnpod_lock_sched();
    xnpod_stop_timer();

    vrtxtask_cleanup();
    vrtxpt_cleanup();
    vrtxqueue_cleanup();
    vrtxmb_cleanup();
    vrtxmx_cleanup();
    vrtxsem_cleanup();
    vrtxevent_cleanup();
    vrtxheap_cleanup();

    xnpod_shutdown(xtype);
}

int INIT_MODULE (void)

{
    u_long nstick = XNPOD_DEFAULT_TICK;
    int err, n;

    err = xnpod_init(&pod,255,0,XNDREORD);

    if (err != XN_OK)
	return err;

    xnmutex_init(&__imutex);

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
	nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);

    if (err != XN_OK)
	return err;

    for (n = 0; n < VRTX_MAX_CB - 1; n++)
	vrtxidgen[n] = n + 1;

    vrtxidgen[VRTX_MAX_CB - 1] = -1;
    vrtxidfree = 0;

    /* the VRTX workspace, or sysheap, is accessed (sc_halloc) with hid 0.
     * We avoid a test by ensuring it is the first object in vrtxobjmap,
     * so vrtxheap_init must be called right now. 
     */
    err = vrtxheap_init(MODULE_PARM_VALUE(workspace_size_arg));
    if (err != XN_OK)
	return err;

    vrtxevent_init();
    vrtxsem_init();
    vrtxqueue_init();
    vrtxpt_init();
    vrtxmb_init();
    vrtxmx_init();
    vrtxtask_init(MODULE_PARM_VALUE(task_stacksize_arg));

    pod.svctable.shutdown = &vrtx_shutdown;

    xnprintf("VRTX/VM: starting services.\n");

    return 0;
}

void CLEANUP_MODULE (void) {

    xnprintf("VRTX/VM: stopping services.\n");
    vrtx_shutdown(XNPOD_NORMAL_EXIT);
}

int sc_gversion (void)
{
    return VRTX_VERSION;
}

EXPORT_SYMBOL(sc_accept);
EXPORT_SYMBOL(sc_adelay);
EXPORT_SYMBOL(sc_delay);
EXPORT_SYMBOL(sc_fclear);
EXPORT_SYMBOL(sc_fcreate);
EXPORT_SYMBOL(sc_fdelete);
EXPORT_SYMBOL(sc_finquiry);
EXPORT_SYMBOL(sc_fpend);
EXPORT_SYMBOL(sc_fpost);
EXPORT_SYMBOL(sc_gblock);
EXPORT_SYMBOL(sc_gclock);
EXPORT_SYMBOL(sc_gtime);
EXPORT_SYMBOL(sc_gversion);
EXPORT_SYMBOL(sc_halloc);
EXPORT_SYMBOL(sc_hcreate);
EXPORT_SYMBOL(sc_hdelete);
EXPORT_SYMBOL(sc_hfree);
EXPORT_SYMBOL(sc_hinquiry);
EXPORT_SYMBOL(sc_lock);
EXPORT_SYMBOL(sc_maccept);
EXPORT_SYMBOL(sc_mcreate);
EXPORT_SYMBOL(sc_mdelete);
EXPORT_SYMBOL(sc_minquiry);
EXPORT_SYMBOL(sc_mpend);
EXPORT_SYMBOL(sc_mpost);
EXPORT_SYMBOL(sc_pcreate);
EXPORT_SYMBOL(sc_pdelete);
EXPORT_SYMBOL(sc_pend);
EXPORT_SYMBOL(sc_pextend);
EXPORT_SYMBOL(sc_pinquiry);
EXPORT_SYMBOL(sc_post);
EXPORT_SYMBOL(sc_qaccept);
EXPORT_SYMBOL(sc_qbrdcst);
EXPORT_SYMBOL(sc_qcreate);
EXPORT_SYMBOL(sc_qdelete);
EXPORT_SYMBOL(sc_qecreate);
EXPORT_SYMBOL(sc_qinquiry);
EXPORT_SYMBOL(sc_qjam);
EXPORT_SYMBOL(sc_qpend);
EXPORT_SYMBOL(sc_qpost);
EXPORT_SYMBOL(sc_rblock);
EXPORT_SYMBOL(sc_saccept);
EXPORT_SYMBOL(sc_sclock);
EXPORT_SYMBOL(sc_screate);
EXPORT_SYMBOL(sc_sdelete);
EXPORT_SYMBOL(sc_sinquiry);
EXPORT_SYMBOL(sc_spend);
EXPORT_SYMBOL(sc_spost);
EXPORT_SYMBOL(sc_stime);
EXPORT_SYMBOL(sc_tcreate);
EXPORT_SYMBOL(sc_tdelete);
EXPORT_SYMBOL(sc_tecreate);
EXPORT_SYMBOL(sc_tinquiry);
EXPORT_SYMBOL(sc_tpriority);
EXPORT_SYMBOL(sc_tresume);
EXPORT_SYMBOL(sc_tslice);
EXPORT_SYMBOL(sc_tsuspend);
EXPORT_SYMBOL(sc_unlock);
EXPORT_SYMBOL(ui_timer);
