/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "rtai_config.h"
#include "uitron/task.h"
#include "uitron/sem.h"
#include "uitron/flag.h"
#include "uitron/mbx.h"

MODULE_DESCRIPTION("XENOMAI-based uITRON API");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static u_long tick_hz_arg = 1000000000 / XNPOD_DEFAULT_TICK; /* Default tick period */
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz)");

static xnpod_t pod;

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

    if (err != 0)
	return err;

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
	nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);

    if(err != 0)
        {
        xnpod_shutdown(err);
        return err;
        }

    pod.svctable.shutdown = &uitron_shutdown;

    uitask_init();
    uisem_init();
    uiflag_init();
    uimbx_init();

    xnprintf("uitron/VM: starting services.\n");

    return 0;
}

void __xeno_skin_exit (void) {

    xnprintf("uitron/VM: stopping services\n");
    uitron_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__xeno_skin_init);
module_exit(__xeno_skin_exit);
