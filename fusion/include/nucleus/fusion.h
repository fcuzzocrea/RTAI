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
#define __xn_fusion_exit         1
#define __xn_fusion_create       2
#define __xn_fusion_start        3
#define __xn_fusion_time         4
#define __xn_fusion_cputime      5
#define __xn_fusion_start_timer  6
#define __xn_fusion_stop_timer   7
#define __xn_fusion_sleep        8
#define __xn_fusion_inquire      9
#define __xn_fusion_set_periodic 10
#define __xn_fusion_wait_period  11
#define __xn_fusion_idle         12
#define __xn_fusion_cancel       13
#define __xn_fusion_activate     14
#define __xn_fusion_hold         15
#define __xn_fusion_release      16

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
#include <sys/mman.h>
#include <pthread.h>

    /* Internal RTAI/fusion interface. */

int fusion_probe(xnsysinfo_t *infop);

int fusion_thread_shadow(const char *name,
			 void *uhandle,
			 void **khandlep);

int fusion_thread_release(void);

int fusion_thread_create(const char *name,
			 void *uhandle,
			 xncompletion_t *completionp,
			 void **khandlep);

int fusion_thread_barrier(void);

int fusion_thread_start(void *khandle);

int fusion_thread_sync(xncompletion_t *completionp);

int fusion_thread_migrate(int domain);

int fusion_thread_sleep(nanotime_t ticks);

int fusion_thread_inquire(xninquiry_t *infop);

int fusion_thread_set_periodic(nanotime_t idate,
			       nanotime_t period);

int fusion_thread_wait_period(void);

int fusion_timer_start(nanotime_t nstick);

int fusion_timer_stop(void);

int fusion_timer_read(nanotime_t *tp);

int fusion_timer_tsc(nanotime_t *tp);

int fusion_timer_ns2tsc(nanostime_t ns,
			nanostime_t *ptsc);

int fusion_timer_tsc2ns(nanostime_t tsc,
			nanostime_t *pns);

int fusion_uvm_idle(int *lockp);

int fusion_uvm_cancel(void *deadhandle,
		      void *nexthandle);

int fusion_uvm_activate(void *nexthandle,
			void *prevhandle);

int fusion_uvm_hold(int *pendp);

int fusion_uvm_release(int *lockp);

#endif /* __KERNEL__ */

#ifdef __cplusplus
};
#endif /* __cplusplus */

#endif /* !__RTAI_SIM__ */

#endif /* !_RTAI_NUCLEUS_FUSION_H */
