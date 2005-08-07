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

#ifdef __KERNEL__
#include <posix/syscall.h>
#endif /* __KERNEL__ */
#include <posix/internal.h>
#include <posix/cond.h>
#include <posix/mutex.h>
#include <posix/posix.h>
#include <posix/sem.h>
#include <posix/signal.h>
#include <posix/thread.h>
#include <posix/tsd.h>
#include <posix/mq.h>
#include <posix/intr.h>
#include <posix/timer.h>
#include <posix/registry.h>

MODULE_DESCRIPTION("POSIX/PSE51 interface");
MODULE_AUTHOR("gilles.chanteperdrix@laposte.net");
MODULE_LICENSE("GPL");

#if defined(CONFIG_RTAI_HW_APERIODIC_TIMER)
static u_long tick_hz_arg = 0;
#else
static u_long tick_hz_arg = 1000000000 / XNPOD_DEFAULT_TICK;
#endif

module_param_named(tick_hz,tick_hz_arg,ulong,0444);
MODULE_PARM_DESC(tick_hz,"Clock tick frequency (Hz), 0 for aperiodic mode");

static u_long time_slice_arg = 1; /* Default (round-robin) time slice */
module_param_named(time_slice,time_slice_arg,ulong,0444);
MODULE_PARM_DESC(time_slice,"Default time slice (in ticks)");

#if !defined(__KERNEL__) || !defined(CONFIG_RTAI_OPT_FUSION)
static xnpod_t pod;
#endif /* !defined(__KERNEL__) || !defined(CONFIG_RTAI_OPT_FUSION) */

static void pse51_shutdown(int xtype)
{
    xnpod_stop_timer();

    pse51_thread_cleanup();
/*     pse51_timer_obj_cleanup(); */
/*     pse51_intr_obj_cleanup(); */
    pse51_tsd_cleanup();
    pse51_cond_obj_cleanup();
    pse51_sem_obj_cleanup();
    pse51_mutex_obj_cleanup();
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    pse51_syscall_cleanup();
    xnfusion_detach();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
    pse51_reg_pkg_cleanup();

    xnpod_shutdown(xtype);
}

int __fusion_skin_init(void)
{
    u_long nstick;
    int err;

    xnprintf("starting POSIX services.\n");

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* The POSIX skin is stacked over the fusion framework. */
    err = xnfusion_attach();
#else /* !(__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */
    /* The POSIX skin is standalone. */
    err = xnpod_init(&pod,PSE51_MIN_PRIORITY,PSE51_MAX_PRIORITY,0);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    if (err != 0)
	return err;

    if (module_param_value(tick_hz_arg) > 0)
	nstick = 1000000000 / module_param_value(tick_hz_arg);
    else
        nstick = XN_APERIODIC_TICK;

    err = xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);
    
    if(err == -EBUSY)
        {
        err = 0;
        if (testbits(nkpod->status, XNTIMED))
            xnprintf("POSIX: Warning: aperiodic timer was already "
                     "running.\n");
        else
            xnprintf("POSIX: Warning: periodic timer was already running "
                     "(period %lu us).\n",
                     xnpod_get_tickval() / 1000);
        }

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    if (!err)
	err = pse51_syscall_init();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
    
    if (err != 0)
        {
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
	xnfusion_detach();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
        xnpod_shutdown(err);    
	return err;
        }

    pse51_reg_pkg_init(64, 128); /* FIXME: replace with compilation constants. */
    pse51_signal_init();
    pse51_mutex_obj_init();
    pse51_sem_obj_init();
    pse51_tsd_init();
    pse51_cond_obj_init();
    pse51_mq_pkg_init();
    pse51_intr_obj_init();
    pse51_timer_obj_init();

    pse51_thread_init(module_param_value(time_slice_arg));

    return 0;
}

void __fusion_skin_exit(void)
{
    xnprintf("stopping POSIX services.\n");
    pse51_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__fusion_skin_init);
module_exit(__fusion_skin_exit);
