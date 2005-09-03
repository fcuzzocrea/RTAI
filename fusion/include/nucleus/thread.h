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

#ifndef _RTAI_NUCLEUS_THREAD_H
#define _RTAI_NUCLEUS_THREAD_H

#include <nucleus/timer.h>

/* Status flags */
#define XNSUSP    0x00000001	/* Suspended */
#define XNPEND    0x00000002	/* Sleep-wait for a resource */
#define XNDELAY   0x00000004	/* Delayed */
#define XNREADY   0x00000008	/* Linked to the ready queue */
#define XNDORMANT 0x00000010	/* Not started yet or killed */
#define XNZOMBIE  0x00000020	/* Zombie thread in deletion process */
#define XNRESTART 0x00000040	/* Restarting thread */
#define XNSTARTED 0x00000080	/* Could be restarted */
#define XNRELAX   0x00000100	/* Relaxed shadow thread (blocking bit) */

#define XNTIMEO   0x00000200	/* Woken up due to a timeout condition */
#define XNRMID    0x00000400	/* Pending on a removed resource */
#define XNBREAK   0x00000800	/* Forcibly awaken from a wait state */
#define XNKICKED  0x00001000	/* Kicked upon signal (shadow only) */
#define XNBOOST   0x00002000	/* Undergoes regular PIP boost */
#define XNDEBUG   0x00004000	/* Hit debugger breakpoint (shadow only) */

/* Mode flags. */
#define XNLOCK    0x00008000	/* Not preemptible */
#define XNRRB     0x00010000	/* Undergoes a round-robin scheduling */
#define XNASDI    0x00020000	/* ASR are disabled */
#define XNSHIELD  0x00040000	/* IRQ shield is enabled (shadow only) */
#define XNTRAPSW  0x00080000	/* Trap execution mode switches */

#define XNFPU     0x00100000	/* Thread uses FPU */
#define XNSHADOW  0x00200000	/* Shadow thread */
#define XNROOT    0x00400000	/* Root thread (i.e. Linux/IDLE) */

/*
  Must follow the declaration order of the above bits. Status symbols
  are defined as follows:
  'S' -> forcibly suspended.
  'w'/'W' -> waiting for a resource, with or without timeout.
  'D' -> delayed (without any other wait condition).
  'R' -> runnable.
  'U' -> unstarted or dormant.
  'X' -> Relaxed shadow.
  'b' -> Priority boost undergoing.
  'T' -> ptraced and stopped.
  'l' -> locks scheduler.
  'r' -> undergoes round-robin .
  's' -> interrupt shield enabled.
  't' -> mode switches trapped.
  'f' -> FPU enabled (for kernel threads).
*/
#define XNTHREAD_SLABEL_INIT { \
  'S', 'W', 'D', 'R', 'U', \
  '.', '.', '.', 'X', '.', \
  '.', '.', '.', 'b', 'T', \
  'l', 'r', '.', 's', 't', \
  'f', '.', '.' \
}

#define XNTHREAD_BLOCK_BITS   (XNSUSP|XNPEND|XNDELAY|XNDORMANT|XNRELAX)
#define XNTHREAD_MODE_BITS    (XNLOCK|XNRRB|XNASDI|XNSHIELD|XNTRAPSW)
#define XNTHREAD_SYSTEM_BITS  (XNROOT)

/* These flags are available to the real-time interfaces */
#define XNTHREAD_SPARE0  0x10000000
#define XNTHREAD_SPARE1  0x20000000
#define XNTHREAD_SPARE2  0x40000000
#define XNTHREAD_SPARE3  0x80000000
#define XNTHREAD_SPARES  0xf0000000

#if defined(__KERNEL__) || defined(__RTAI_UVM__) || defined(__RTAI_SIM__)

#ifdef __RTAI_SIM__
#define XNRUNNING  XNTHREAD_SPARE0	/* Pseudo-status (must not conflict with system bits) */
#define XNDELETED  XNTHREAD_SPARE1	/* idem. */
#endif /* __RTAI_SIM__ */

#define XNTHREAD_INVALID_ASR  ((void (*)(xnsigmask_t))0)

struct xnsched;
struct xnsynch;

typedef void (*xnasr_t)(xnsigmask_t sigs);

typedef struct xnthread {

    xnarchtcb_t tcb;		/* Architecture-dependent block -- Must be first */

    xnflags_t status;		/* Thread status flags */

    struct xnsched *sched;	/* Thread scheduler */

    xnarch_cpumask_t affinity;	/* Processor affinity. */

    int bprio;			/* Base priority (before PIP boost) */

    int cprio;			/* Current priority */

    xnholder_t slink;		/* Thread holder in suspend queue */

    xnpholder_t rlink;		/* Thread holder in ready queue */

    xnpholder_t plink;		/* Thread holder in synchronization queue(s) */

    xnholder_t glink;		/* Thread holder in global queue */

/* We don't want side-effects on laddr here! */
#define link2thread(laddr,link) \
((xnthread_t *)(((char *)laddr) - (int)(&((xnthread_t *)0)->link)))

    xnpqueue_t claimq;		/* Owned resources claimed by others (PIP) */

    struct xnsynch *wchan;	/* Resource the thread pends on */

    xntimer_t rtimer;		/* Resource timer */

    xntimer_t ptimer;		/* Periodic timer */

    int poverrun;		/* Periodic timer overrun. */

    xnsigmask_t signals;	/* Pending signals */

    xnticks_t rrperiod;		/* Allotted round-robin period (ticks) */

    xnticks_t rrcredit;		/* Remaining round-robin time credit (ticks) */

#ifdef CONFIG_RTAI_OPT_STATS
    struct {
	unsigned long psw;	/* Primary mode switch count */
	unsigned long ssw;	/* Secondary mode switch count */
	unsigned long csw;	/* Context switches (includes
				   secondary -> primary switches) */
	unsigned long pf;	/* Number of page faults */
    } stat;
#endif /* CONFIG_RTAI_OPT_STATS */

    xnasr_t asr;		/* Asynchronous service routine */

    xnflags_t asrmode;		/* Thread's mode for ASR */

    int asrimask;		/* Thread's interrupt mask for ASR */

    unsigned asrlevel;		/* ASR execution level (ASRs are reentrant) */

    int imask;			/* Initial interrupt mask */

    int imode;			/* Initial mode */

    int iprio;			/* Initial priority */

    unsigned magic;		/* Skin magic. */

    char name[XNOBJECT_NAME_LEN]; /* Symbolic name of thread */

    xnticks_t stime;		/* Start time */

    void (*entry)(void *cookie); /* Thread entry routine */

    void *cookie;		/* Cookie to pass to the entry routine */

    void *extinfo;		/* Extended information -- user-defined */

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
#define xnthread_set_flags(thread,flags)   __setbits((thread)->status,flags)
#define xnthread_clear_flags(thread,flags) __clrbits((thread)->status,flags)
#define xnthread_initial_priority(thread)  ((thread)->iprio)
#define xnthread_base_priority(thread)     ((thread)->bprio)
#define xnthread_current_priority(thread)  ((thread)->cprio)
#define xnthread_time_slice(thread)        ((thread)->rrperiod)
#define xnthread_time_credit(thread)       ((thread)->rrcredit)
#define xnthread_archtcb(thread)           (&((thread)->tcb))
#define xnthread_asr_level(thread)         ((thread)->asrlevel)
#define xnthread_pending_signals(thread)   ((thread)->signals)
#define xnthread_timeout(thread)           xntimer_get_timeout(&(thread)->rtimer)
#define xnthread_stack_size(thread)        xnarch_stack_size(xnthread_archtcb(thread))
#define xnthread_extended_info(thread)     ((thread)->extinfo)
#define xnthread_set_magic(thread,m)       do { (thread)->magic = (m); } while(0)
#define xnthread_get_magic(thread)         ((thread)->magic)
#define xnthread_signaled_p(thread)        ((thread)->signals != 0)
#define xnthread_user_task(thread)         xnarch_user_task(xnthread_archtcb(thread))
#define xnthread_user_pid(thread) \
    (testbits((thread)->status,XNROOT) || !xnthread_user_task(thread) ? \
    0 : xnarch_user_pid(xnthread_archtcb(thread)))

#ifdef CONFIG_RTAI_OPT_STATS
#define xnthread_inc_psw(thread)     ++(thread)->stat.psw
#define xnthread_inc_ssw(thread)     ++(thread)->stat.ssw
#define xnthread_inc_csw(thread)     ++(thread)->stat.csw
#define xnthread_inc_pf(thread)      ++(thread)->stat.pf
#else /* CONFIG_RTAI_OPT_STATS */
#define xnthread_inc_psw(thread)     do { } while(0)
#define xnthread_inc_ssw(thread)     do { } while(0)
#define xnthread_inc_csw(thread)     do { } while(0)
#define xnthread_inc_pf(thread)      do { } while(0)
#endif /* CONFIG_RTAI_OPT_STATS */

#ifdef __cplusplus
extern "C" {
#endif

int xnthread_init(xnthread_t *thread,
		  const char *name,
		  int prio,
		  xnflags_t flags,
		  unsigned stacksize);

void xnthread_cleanup_tcb(xnthread_t *thread);

char *xnthread_symbolic_status(xnflags_t status, char *buf, int size);

static inline xnticks_t xnthread_get_timeout(xnthread_t *thread, xnticks_t now)
{
    xnticks_t timeout;

    if (!testbits(thread->status,XNDELAY))
	return 0LL;

    timeout = (xntimer_get_date(&thread->rtimer) ? : xntimer_get_date(&thread->ptimer));

    if (timeout <= now)
	return 1;

    return timeout - now;
}

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __RTAI_UVM__ || __RTAI_SIM__ */

#endif /* !_RTAI_NUCLEUS_THREAD_H */
