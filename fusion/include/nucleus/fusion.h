/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#ifndef _RTAI_NUCLEUS_FUSION_H
#define _RTAI_NUCLEUS_FUSION_H

#define FUSION_SKIN_MAGIC    0x504f5358

/* Thread priority levels. */
#define FUSION_LOW_PRIO     1
#define FUSION_HIGH_PRIO    99
/* Extra level for IRQ servers in user-space. */
#define FUSION_IRQ_PRIO     (FUSION_HIGH_PRIO + 1)

#define FUSION_MIN_PRIO     FUSION_LOW_PRIO
#define FUSION_MAX_PRIO     FUSION_IRQ_PRIO

#ifndef __RTAI_SIM__

#include <nucleus/asm/syscall.h>

#define FUSION_APERIODIC_TIMER 0

#define FUSION_LINUX_DOMAIN    0
#define FUSION_RTAI_DOMAIN     1

#define __xn_fusion_init         0
#define __xn_fusion_create       1
#define __xn_fusion_start        2
#define __xn_fusion_set_periodic 3
#define __xn_fusion_wait_period  4
#define __xn_fusion_time         5
#define __xn_fusion_cputime      6
#define __xn_fusion_start_timer  7
#define __xn_fusion_stop_timer   8
#define __xn_fusion_sleep        9
#define __xn_fusion_ns2ticks     10
#define __xn_fusion_ticks2ns     11
#define __xn_fusion_inquire      12
#define __xn_fusion_idle         13
#define __xn_fusion_cancel       14
#define __xn_fusion_activate     15
#define __xn_fusion_hold         16
#define __xn_fusion_release      17

typedef unsigned long long nanotime_t;

typedef long long nanostime_t;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#ifdef __KERNEL__

int xnfusion_mount(void);

int xnfusion_umount(void);

int xnfusion_attach(void);

#else /* !__KERNEL__ */

#include <sys/types.h>
#include <pthread.h>

    /* Public RTAI/fusion interface. */

int pthread_info_rt(xnsysinfo_t *infop);

int pthread_init_rt(const char *name,
		    void *uhandle,
		    void **khandlep);

int pthread_create_rt(const char *name,
		      void *uhandle,
		      pid_t syncpid,
		      int *syncp,
		      void **khandlep);

int pthread_barrier_rt(void);

int pthread_start_rt(void *khandle);

int pthread_sync_rt(int *syncp);

int pthread_migrate_rt(int domain);

int pthread_start_timer_rt(nanotime_t nstick);

int pthread_stop_timer_rt(void);

int pthread_time_rt(nanotime_t *tp);

int pthread_cputime_rt(nanotime_t *tp);

int pthread_ns2ticks_rt(nanostime_t ns,
			nanostime_t *pticks);

int pthread_ticks2ns_rt(nanostime_t ticks,
			nanostime_t *pns);

int pthread_ns2tsc_rt(nanostime_t ns,
                      nanostime_t *ptsc);

int pthread_tsc2ns_rt(nanostime_t tsc,
                      nanostime_t *pns);

int pthread_sleep_rt(nanotime_t ticks);

int pthread_inquire_rt(xninquiry_t *infop);

int pthread_set_periodic_rt(nanotime_t idate,
			    nanotime_t period);

int pthread_wait_period_rt(void);

    /* The following routines are private to the RTAI/vm control layer
       based on the RTAI/fusion interface. Do not use them in
       applications. */

int __pthread_idle_vm(int *lockp);

int __pthread_cancel_vm(void *deadhandle,
			void *nexthandle);

int __pthread_activate_vm(void *nexthandle,
			  void *prevhandle);

int __pthread_hold_vm(int *pendp);

int __pthread_release_vm(int *lockp);

#endif /* __KERNEL__ */

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* !__RTAI_SIM__ */

#endif /* !_RTAI_NUCLEUS_FUSION_H */
