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

#ifndef _RTAI_NUCLEUS_SYSCALL_H
#define _RTAI_NUCLEUS_SYSCALL_H

#include <nucleus/asm/syscall.h>

/* RTAI/fusion multiplexer syscall. */
#define __xn_sys_mux    555
/* RTAI/fusion nucleus syscalls. */
#define __xn_sys_attach     0	/* muxid = xnshadow_attach_interface(magic,infp) */
#define __xn_sys_detach     1	/* xnshadow_detach_interface(muxid) */
#define __xn_sys_sync       2	/* xnshadow_sync(&syncflag) */
#define __xn_sys_migrate    3	/* switched = xnshadow_relax/harden() */
#define __xn_sys_barrier    4	/* started = xnshadow_wait_barrier(&entry,&cookie) */
#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
#define __xn_sys_timestamps 5	/* xnpod_get_timestamps(&timestamps) */
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */

typedef struct xnsysinfo {

    unsigned long long cpufreq;	/* CPU frequency */
    unsigned long tickval;	/* Tick duration (ns) */

} xnsysinfo_t;

typedef struct xninquiry {

    char name[32];
    int prio;
    unsigned long status;
    void *khandle;
    void *uhandle;

} xninquiry_t;

#ifdef __KERNEL__

struct task_struct;

#define XNARCH_MAX_SYSENT 255

typedef struct _xnsysent {

    int (*svc)(struct task_struct *task,
	       struct pt_regs *regs);

/* Syscall must run into the Linux domain. */
#define __xn_flag_lostage    0x1
/* Syscall must run into the RTAI domain. */
#define __xn_flag_histage    0x2
/* Shadow syscall; caller must be mapped. */
#define __xn_flag_shadow     0x4
/* Context-agnostic syscall. */
#define __xn_flag_anycall    0x0
/* Short-hand for shadow initializing syscall. */
#define __xn_flag_init       __xn_flag_lostage
/* Short-hand for pure shadow syscall in RTAI space. */
#define __xn_flag_regular   (__xn_flag_shadow|__xn_flag_histage)

    u_long flags;

} xnsysent_t;

extern int nkgkptd;

#define xnshadow_ptd(t)    ((t)->ptd[nkgkptd])
#define xnshadow_thread(t) ((xnthread_t *)xnshadow_ptd(t))

#endif /* __KERNEL__ */

#endif /* !_RTAI_NUCLEUS_SYSCALL_H */
