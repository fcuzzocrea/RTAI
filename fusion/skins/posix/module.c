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

#include <posix/internal.h>
#include <posix/cond.h>
#include <posix/mutex.h>
#include <posix/posix.h>
#include <posix/sem.h>
#include <posix/signal.h>
#include <posix/thread.h>
#include <posix/tsd.h>

MODULE_DESCRIPTION("XENOMAI-based PSE51 API.");
MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_LICENSE("GPL");

static u_long tick_hz_arg = 1000000000 / XNPOD_DEFAULT_TICK;
MODULE_PARM(tick_hz_arg,"i");
MODULE_PARM_DESC(tick_hz_arg,"Clock tick frequency (Hz), 0 for aperiodic mode");

static u_long time_slice_arg = 1; /* Default (round-robin) time slice */
MODULE_PARM(time_slice_arg,"i");
MODULE_PARM_DESC(time_slice_arg,"Default time slice (in ticks)");

#if !defined(__KERNEL__) || !defined(CONFIG_RTAI_OPT_FUSION)
static xnpod_t pod;
#endif /* !defined(__KERNEL__) || !defined(CONFIG_RTAI_OPT_FUSION) */

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

int __fusion_skin_init(void)
{
    u_long nstick;
    int err;

    xnprintf("POSIX %s: Starting skin\n",PSE51_SKIN_VERSION_STRING);

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* The POSIX skin is stacked over the fusion framework. */
    err = xnfusion_attach();
#else /* !(__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */
    /* The POSIX skin is standalone. */
    err = xnpod_init(&pod,PSE51_MIN_PRIORITY,PSE51_MAX_PRIORITY,0);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    if (err != 0)
	return err;

    if (MODULE_PARM_VALUE(tick_hz_arg) > 0)
	nstick = 1000000000 / MODULE_PARM_VALUE(tick_hz_arg);
    else
        nstick = XN_APERIODIC_TICK;

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);
    
    if(err == -EBUSY)
        {
        err = 0;
        if (testbits(nkpod->status, XNTIMED))
            xnprintf("POSIX %s: Warning: aperiodic timer was already "
                     "running.\n", PSE51_SKIN_VERSION_STRING);
        else
            xnprintf("POSIX %s: Warning: periodic timer was already running "
                     "(period %lu us).\n", PSE51_SKIN_VERSION_STRING,
                     xnpod_get_tickval() / 1000);
        }

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

    nkpod->svctable.shutdown = &pse51_shutdown;

    return 0;
}

void __fusion_skin_exit(void)
{
    xnprintf("POSIX %s: Stopping skin\n",PSE51_SKIN_VERSION_STRING);
    pse51_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__fusion_skin_init);
module_exit(__fusion_skin_exit);
