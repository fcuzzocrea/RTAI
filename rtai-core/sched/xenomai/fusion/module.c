/*
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
 */

#include <xenomai/pod.h>
#include <xenomai/mutex.h>
#include <xenomai/fusion.h>

MODULE_DESCRIPTION("XENOMAI-based FUSION interface");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

static u_long tick_hz_arg = 1000; /* Default tick = 1ms, 1kHz */
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz)");

int fusion_register_skin(void);

void fusion_unregister_skin(void);

int __init dbridge_init(void);

int __exit dbridge_exit(void);

static xnpod_t pod;

xnmutex_t __imutex;

static void fusion_shutdown (int xtype)

{
    xnpod_stop_timer();
    xnpod_shutdown(xtype);
}

int init_module (void)

{
    u_long nstick = XNPOD_DEFAULT_TICK;
    int err;

    err = xnpod_init(&pod,0,4095,0); /* 4096 priority levels should be
					more than enough to map any
					VM priority space. */
    if (err != XN_OK)
	return err;

    xnmutex_init(&__imutex);

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
	nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);

    pod.svctable.shutdown = &fusion_shutdown;

    err = fusion_register_skin();

    if (err == XN_OK)
	err = dbridge_init();

    if (err != XN_OK)
	xnpod_shutdown(XNPOD_FATAL_EXIT);

    xnprintf("RTAI/fusion: interface loaded (tick=%luus)\n",nstick / 1000);

    return err;
}

void cleanup_module (void)

{
    dbridge_exit();
    xnprintf("RTAI/fusion: interface unloaded\n");
    fusion_unregister_skin();
    fusion_shutdown(XNPOD_NORMAL_EXIT);
}
