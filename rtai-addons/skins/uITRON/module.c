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
#include "uITRON/task.h"
#include "uITRON/sem.h"
#include "uITRON/flag.h"
#include "uITRON/mbx.h"

MODULE_DESCRIPTION("XENOMAI-based uITRON API");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static u_long tick_hz_arg = 100; /* Default tick period */
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz)");

static xnpod_t pod;

xnmutex_t __imutex;

static void uitron_shutdown (int xtype)

{
    xnpod_stop_timer();

    uimbx_cleanup();
    uiflag_cleanup();
    uisem_cleanup();
    uitask_cleanup();

    xnpod_shutdown(xtype);
}

int __xeno_skin_init (void)

{
    u_long nstick = XNPOD_DEFAULT_TICK;
    int err;

    err = xnpod_init(&pod,uITRON_MIN_PRI,uITRON_MAX_PRI,0);

    if (err != XN_OK)
	return err;

    xnmutex_init(&__imutex);

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
	nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);

    pod.svctable.shutdown = &uitron_shutdown;

    uitask_init();
    uisem_init();
    uiflag_init();
    uimbx_init();

    xnprintf("uITRON/VM: starting services.\n");

    return 0;
}

void __xeno_skin_exit (void) {

    xnprintf("uITRON/VM: stopping services\n");
    uitron_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__xeno_skin_init);
module_exit(__xeno_skin_exit);
