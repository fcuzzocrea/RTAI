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
#include <sys/mman.h>
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
    __fusion_muxid = XENOMAI_SYSCALL2(__xn_sys_attach,FUSION_SKIN_MAGIC,&__fusion_info);
    return __fusion_muxid;
}

/* Public RTAI/fusion interface. */

int pthread_info_rt (xnsysinfo_t *infop)

{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -1;

    memcpy(infop,&__fusion_info,sizeof(*infop));

    return 0;
}

int pthread_init_rt (const char *name,
		     void *uhandle,
		     void **khandlep)
{
    char stack[PTHREAD_STACK_MIN * 2];

    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    mlockall(MCL_CURRENT|MCL_FUTURE);
    memset(stack,0,sizeof(stack));

    /* Move the current Linux task into the RTAI realm. */
    return XENOMAI_SKINCALL3(__fusion_muxid,__xn_fusion_init,name,khandlep,uhandle);
}

int pthread_exit_rt (void)

{
    return XENOMAI_SKINCALL0(__fusion_muxid,
			     __xn_fusion_exit);
}

int pthread_create_rt (const char *name,
		       void *uhandle,
		       pid_t syncpid,
		       int *syncp,
		       void **khandlep)
{
    char stack[PTHREAD_STACK_MIN * 2];

    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    XENOMAI_SYSCALL1(__xn_sys_migrate,FUSION_LINUX_DOMAIN);

    mlockall(MCL_CURRENT|MCL_FUTURE);
    memset(stack,0,sizeof(stack));

    /* Move the current Linux task into the RTAI realm, but do not
       start the mated shadow thread. Caller will need to wait on the
       barrier (pthread_barrier_rt()) for the start event
       (pthread_start_rt()). */
    return XENOMAI_SKINCALL5(__fusion_muxid,__xn_fusion_create,name,khandlep,uhandle,syncpid,syncp);
}

int pthread_barrier_rt (void)

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

int pthread_start_rt (void *khandle)

{
    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_start,khandle);
}

int pthread_sync_rt (int *syncp)

{
    return XENOMAI_SYSCALL1(__xn_sys_sync,syncp);
}

int pthread_time_rt (nanotime_t *tp)

{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_time,tp);
}

int pthread_cputime_rt (nanotime_t *tp)

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

int pthread_start_timer_rt (nanotime_t nstick)

{
    if (__fusion_muxid == 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_start_timer,&nstick);
}

int pthread_stop_timer_rt (void) {

    return XENOMAI_SKINCALL0(__fusion_muxid,__xn_fusion_stop_timer);
}

int pthread_sleep_rt (nanotime_t delay) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_sleep,&delay);
}

int pthread_ns2tsc_rt(nanostime_t ns, nanostime_t *ptsc)

{
    if (__fusion_muxid == 0)
	return -ENOSYS;

    *ptsc = llimd(ns, __fusion_info.cpufreq, 1000000000);

    return 0;
}

int pthread_tsc2ns_rt(nanostime_t tsc, nanostime_t *pns)

{
    if (__fusion_muxid == 0)
	return -ENOSYS;

    *pns = llimd(tsc, 1000000000, __fusion_info.cpufreq);

    return 0;
}

int pthread_inquire_rt (xninquiry_t *buf) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_inquire,buf);
}

int pthread_migrate_rt (int domain)

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

int pthread_set_periodic_rt (nanotime_t idate,
			     nanotime_t period)
{
    return XENOMAI_SKINCALL2(__fusion_muxid,
			     __xn_fusion_set_periodic,
			     &idate,
			     &period);
}

int pthread_wait_period_rt (void)

{
    return XENOMAI_SKINCALL0(__fusion_muxid,
			     __xn_fusion_wait_period);
}

/* Private RTAI/vm interface. */

int __pthread_idle_uvm (int *lockp) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_idle,lockp);
}

int __pthread_cancel_uvm (void *deadhandle, void *nexthandle) {

    return XENOMAI_SKINCALL2(__fusion_muxid,__xn_fusion_cancel,deadhandle,nexthandle);
}

int __pthread_activate_uvm (void *nexthandle, void *prevhandle) {

    return XENOMAI_SKINCALL2(__fusion_muxid,__xn_fusion_activate,nexthandle,prevhandle);
}

int __pthread_hold_uvm (int *pendp) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_hold,pendp);
}

int __pthread_release_uvm (int *lockp) {

    return XENOMAI_SKINCALL1(__fusion_muxid,__xn_fusion_release,lockp);
}
