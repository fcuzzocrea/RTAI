/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <nucleus/fusion.h>

static int __fusion_muxid;

static xnsysinfo_t __fusion_info;

static unsigned long long ullimd(unsigned long long ull, u_long m, u_long d)

{
    u_long h, l, mlh, mll, qh, r, ql;
    unsigned long long mh, ml;

    h = ull >> 32; l = ull & 0xffffffff; /* Split ull. */
    mh = (unsigned long long) h * m;
    ml = (unsigned long long) l * m;
    mlh = ml >> 32; mll = ml & 0xffffffff; /* Split ml. */
    mh += mlh;
    qh = mh / d;
    r = mh % d;
    ml = (((unsigned long long) r) << 32) + mll; /* assemble r and mll */
    ql = ml / d;

    return (((unsigned long long) qh) << 32) + ql;
}

static long long llimd(long long ll, u_long m, u_long d) {

    if(ll < 0)
        return -ullimd(-ll, m, d);
    return ullimd(ll, m, d);
}

static inline int __init_skin (void)

{
    __fusion_muxid = XENOMAI_SYSCALL2(__xn_sys_bind,FUSION_SKIN_MAGIC,&__fusion_info);
    return __fusion_muxid;
}

/* Public RTAI/fusion interface. */

int fusion_probe (xnsysinfo_t *infop)

{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -1;

    memcpy(infop,&__fusion_info,sizeof(*infop));

    return 0;
}

int fusion_thread_shadow (const char *name,
			  void *uhandle,
			  void **khandlep)
{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    /* Move the current Linux task into the RTAI realm. */
    return XENOMAI_SKINCALL3(__fusion_muxid,__xn_fusion_init,name,khandlep,uhandle);
}

int fusion_thread_release (void)

{
    return XENOMAI_SKINCALL0(__fusion_muxid,__xn_fusion_exit);
}

int fusion_thread_create (const char *name,
			  void *uhandle,
			  xncompletion_t *completionp,
			  void **khandlep)
{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    XENOMAI_SYSCALL1(__xn_sys_migrate,FUSION_LINUX_DOMAIN);

    /* Move the current Linux task into the RTAI realm, but do not
       start the mated shadow thread. Caller will need to wait on the
       barrier (fusion_thread_barrier()) for the start event
       (fusion_thread_start()). */
    return XENOMAI_SKINCALL4(__fusion_muxid,__xn_fusion_create,name,khandlep,uhandle,completionp);
}

int fusion_thread_barrier (void)

{
    void (*entry)(void *), *cookie;
    int err;

    /* Make the current Linux task wait on the barrier for its mated
       shadow thread to be started. The barrier could be released in
       order to process Linux signals while the fusion shadow is still
       dormant; in such a case, resume wait. */

    do
	err = XENOMAI_SYSCALL2(__xn_sys_barrier,&entry,&cookie);
    while (err == -EINTR);

    return err;
}

int fusion_thread_start (void *khandle)

{
    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_start,khandle);
}

int fusion_thread_sync (xncompletion_t *completionp)

{
    return XENOMAI_SYSCALL1(__xn_sys_completion,completionp);
}

int fusion_thread_sleep (nanotime_t delay)
{
    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_sleep,&delay);
}

int fusion_thread_inquire (xninquiry_t *buf)
{
    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_inquire,buf);
}

int fusion_thread_migrate (int domain)

{
    switch (domain)
	{
	case FUSION_LINUX_DOMAIN:
	case FUSION_RTAI_DOMAIN:

	    return XENOMAI_SYSCALL1(__xn_sys_migrate,domain);

	default:

	    return -EINVAL;
	}
}

int fusion_timer_read (nanotime_t *tp)

{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_time,tp);
}

int fusion_timer_tsc (nanotime_t *tp)

{
#ifdef CONFIG_RTAI_HW_DIRECT_TSC
    *tp = __xn_rdtsc();
    return 0;
#else /* !CONFIG_RTAI_HW_DIRECT_TSC */
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_cputime,tp);
#endif /* CONFIG_RTAI_HW_DIRECT_TSC */
}

int fusion_timer_start (nanotime_t nstick)

{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_start_timer,&nstick);
}

int fusion_timer_stop (void)
{

    return XENOMAI_SKINCALL0(__fusion_muxid,__xn_fusion_stop_timer);
}

int fusion_timer_ns2tsc (nanostime_t ns, nanostime_t *ptsc)

{
    if (__fusion_muxid == 0)
	return -ENOSYS;

    *ptsc = llimd(ns, __fusion_info.cpufreq, 1000000000);

    return 0;
}

int fusion_timer_tsc2ns (nanostime_t tsc, nanostime_t *pns)

{
    if (__fusion_muxid == 0)
	return -ENOSYS;

    *pns = llimd(tsc, 1000000000, __fusion_info.cpufreq);

    return 0;
}

int fusion_thread_set_periodic (nanotime_t idate,
				   nanotime_t period)
{
    return XENOMAI_SKINCALL2(__fusion_muxid,
			     __xn_fusion_set_periodic,
			     &idate,
			     &period);
}

int fusion_thread_wait_period (void)

{
    return XENOMAI_SKINCALL0(__fusion_muxid,
			     __xn_fusion_wait_period);
}

/* Private UVM interface. */

int fusion_uvm_idle (int *lockp)
{
    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_idle,lockp);
}

int fusion_uvm_cancel (void *deadhandle, void *nexthandle)
{
    return XENOMAI_SKINCALL2(__fusion_muxid,__xn_fusion_cancel,deadhandle,nexthandle);
}

int fusion_uvm_activate (void *nexthandle, void *prevhandle)
{
    return XENOMAI_SKINCALL2(__fusion_muxid,__xn_fusion_activate,nexthandle,prevhandle);
}

int fusion_uvm_hold (int *pendp)
{
    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_hold,pendp);
}

int fusion_uvm_release (int *lockp)
{
    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_release,lockp);
}
