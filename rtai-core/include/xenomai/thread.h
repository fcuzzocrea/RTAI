/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _xenomai_thread_h
#define _xenomai_thread_h

#include "xenomai/timer.h"

/* Status flags */
#define XNSUSP    0x00000001	/* Suspended */
#define XNPEND    0x00000002	/* Sleep-wait for a resource */
#define XNDELAY   0x00000004	/* Delayed */
#define XNREADY   0x00000008	/* Linked to the ready queue */
#define XNDORMANT 0x00000010	/* In startable state */
#define XNWMUTEX  0x00000020	/* Ready-wait for a kernel mutex */
#define XNFROZEN  0x00000040	/* Frozen by debugger */
#define XNZOMBIE  0x00000080	/* Self-deleting running thread */
#define XNRESTART 0x00000100	/* Restarting thread */
#define XNSTARTED 0x00000200	/* Can be restarted */
#define XNRELAX   0x00000400	/* Relaxed scheduling mode (a blocking bit) */
#define XNAUTOSW  0x00000800	/* Auto-switch to RTAI on return from Linux */

#define XNTIMEO   0x00001000	/* Woken up due to a timeout condition */
#define XNRMID    0x00002000	/* Pending on a removed resource */
#define XNBREAK   0x00004000	/* Forcibly woken up from a wait state */
#define XNBOOST   0x00008000	/* Undergoes regular PIP boost */
#define XNSYSSW   0x00010000	/* Migration due to syscall handling */

/* Mode flags. */
#define XNLOCK    0x00020000	/* Not preemptable */
#define XNRRB     0x00040000	/* Undergoes a round-robin scheduling */
#define XNASDI    0x00080000	/* ASR are disabled */

#define XNFPU     0x00100000	/* Thread uses FPU */
#define XNISVC    0x00200000	/* Interrupt svc thread -- Don't freeze */
#define XNSHADOW  0x00400000	/* Shadow thread */
#define XNROOT    0x00800000	/* Root thread (i.e. Linux/IDLE) */

#define XNTHREAD_BLOCK_BITS  (XNSUSP|XNPEND|XNDELAY|XNDORMANT|XNFROZEN|XNRELAX)
#define XNTHREAD_MODE_BITS   (XNLOCK|XNRRB|XNASDI)
#define XNTHREAD_SYSTEM_BITS (XNISVC|XNROOT)

/* These flags are available to the real-time interfaces */
#define XNTHREAD_SPARE0  0x10000000
#define XNTHREAD_SPARE1  0x20000000
#define XNTHREAD_SPARE2  0x40000000
#define XNTHREAD_SPARE3  0x80000000

#define XNRUNNING  XNTHREAD_SPARE0	/* Pseudo-status (must not conflict with system bits) */
#define XNDELETED  XNTHREAD_SPARE1	/* idem. */

#define XNTHREAD_INVALID_ASR  ((void (*)(xnsigmask_t))0)

#define XNTHREAD_SHADOW_SIGKILL  0x1

struct xnsched;
struct xnsynch;
struct xnmutex;

typedef void (*xnasr_t)(xnsigmask_t sigs);

typedef struct xnthread {

    xnarchtcb_t tcb;		/* Architecture-dependent block -- Must be first */

    xnflags_t status;		/* Thread status flags */

    struct xnsched *sched;	/* Thread scheduler */

    int bprio;			/* Base priority (before PIP boost) */

    int cprio;			/* Current priority */

    unsigned magic;		/* Skin magic. */

    char name[XNOBJECT_NAME_LEN]; /* Symbolic name of thread */

    xnticks_t rrperiod;		/* Allotted round-robin period (ticks) */

    xnticks_t rrcredit;		/* Remaining round-robin time credit (ticks) */

    xnholder_t slink;		/* Thread holder in suspend queue */

    xnpholder_t rlink;		/* Thread holder in ready queue */

    xnpholder_t plink;		/* Thread holder in synchronization queue(s) */

    xnholder_t glink;		/* Thread holder in global queue */

/* We don't want side-effects on laddr here! */
#define link2thread(laddr,link) \
((xnthread_t *)(((char *)laddr) - (int)(&((xnthread_t *)0)->link)))

    xnpqueue_t claimq;		/* Owned resources claimed by others (PIP) */

    struct xnsynch *wchan;	/* Resource the thread pends on */

    xntimer_t timer;		/* Delay timer */

    xntimer_t atimer;		/* Asynchronous timer (shadow only) */

    xnsigmask_t signals;	/* Pending signals */

    xnasr_t asr;		/* Asynchronous service routine */

    xnflags_t asrmode;		/* Thread's mode for ASR */

    int asrimask;		/* Thread's interrupt mask for ASR */

    unsigned asrlevel;		/* ASR execution level (ASRs are reentrant) */

    int imask;			/* Initial interrupt mask */

    int imode;			/* Initial mode */

    int iprio;			/* Initial priority */

    xntime_t stime;		/* Start time */

    void (*entry)(void *cookie); /* Thread entry routine */

    void *cookie;		/* Cookie to pass to the entry routine */

    void *extinfo;		/* Extended information -- user-defined */

    void *adcookie;		/* Arch-dependent system cookie. */

    XNARCH_DECL_DISPLAY_CONTEXT();

} xnthread_t;

#define XNHOOK_THREAD_START  1
#define XNHOOK_THREAD_SWITCH 2
#define XNHOOK_THREAD_DELETE 3

typedef struct xnhook {

    xnholder_t link;

#define link2hook(laddr) \
((xnhook_t *)(((char *)laddr) - (int)(&((xnhook_t *)0)->link)))

    void (*routine)(xnthread_t *thread);

} xnhook_t;

#define xnthread_name(thread)              ((thread)->name)
#define xnthread_sched(thread)             ((thread)->sched)
#define xnthread_start_time(thread)        ((thread)->stime)
#define xnthread_status_flags(thread)      ((thread)->status)
#define xnthread_test_flags(thread,flags)  testbits((thread)->status,flags)
#define xnthread_set_flags(thread,flags)   setbits((thread)->status,flags)
#define xnthread_clear_flags(thread,flags) clrbits((thread)->status,flags)
#define xnthread_initial_priority(thread)  ((thread)->iprio)
#define xnthread_base_priority(thread)     ((thread)->bprio)
#define xnthread_current_priority(thread)  ((thread)->cprio)
#define xnthread_time_slice(thread)        ((thread)->rrperiod)
#define xnthread_time_credit(thread)       ((thread)->rrcredit)
#define xnthread_archtcb(thread)           (&((thread)->tcb))
#define xnthread_asr_level(thread)         ((thread)->asrlevel)
#define xnthread_pending_signals(thread)   ((thread)->signals)
#define xnthread_timeout(thread)           (xntimer_date(&(thread)->timer) - nkpod->jiffies)
#define xnthread_stack_size(thread)        xnarch_stack_size(xnthread_archtcb(thread))
#define xnthread_extended_info(thread)     ((thread)->extinfo)
#define xnthread_magic(thread)             ((thread)->magic)

#ifdef __cplusplus
extern "C" {
#endif

int xnthread_init(xnthread_t *thread,
		  const char *name,
		  int prio,
		  xnflags_t flags,
		  unsigned stacksize,
		  void *adcookie,
		  unsigned magic);

void xnthread_cleanup_tcb(xnthread_t *thread);

#ifdef __cplusplus
}
#endif

#endif /* !_xenomai_thread_h */
