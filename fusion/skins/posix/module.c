/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "posix/internal.h"
#include "posix/cond.h"
#include "posix/mutex.h"
#include "posix/posix.h"
#include "posix/sem.h"
#include "posix/signal.h"
#include "posix/thread.h"
#include "posix/tsd.h"

MODULE_DESCRIPTION("XENOMAI-based PSE51 API.");
MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_LICENSE("GPL");

static u_long tick_hz_arg = XNPOD_DEFAULT_TICK; /* Default tick period */
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz)");

static u_long time_slice_arg = 1; /* Default (round-robin) time slice */
MODULE_PARM(time_slice_arg,"i");
MODULE_PARM_DESC(time_slice_arg,"Default time slice (in ticks)");

static xnpod_t pod;

static void pse51_shutdown(int xtype)
{
    xnpod_stop_timer();

    pse51_thread_cleanup();
    pse51_tsd_cleanup();
    pse51_cond_obj_cleanup();
    pse51_sem_obj_cleanup();
    pse51_mutex_obj_cleanup();

    xnpod_shutdown(xtype);
}

int __xeno_skin_init(void)
{
    u_long nstick = XNPOD_DEFAULT_TICK;
    int err;

    xnprintf("POSIX %s: Starting skin\n",PSE51_SKIN_VERSION_STRING);

    err = xnpod_init(&pod,PSE51_MIN_PRIORITY,PSE51_MAX_PRIORITY,0);

    if (err != 0)
	return err;

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
	nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);
    
    if (err != 0)
        {
        xnpod_shutdown(err);    
	return err;
        }

    pse51_signal_init();
    pse51_mutex_obj_init();
    pse51_sem_obj_init();
    pse51_tsd_init();
    pse51_cond_obj_init();

    pse51_thread_init(MODULE_PARM_VALUE(time_slice_arg));

    pod.svctable.shutdown = &pse51_shutdown;

    return 0;
}

void __xeno_skin_exit(void)
{
    xnprintf("POSIX %s: Stopping skin\n",PSE51_SKIN_VERSION_STRING);
    pse51_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__xeno_skin_init);
module_exit(__xeno_skin_exit);
