/*!\file pod.h
 * \brief Real-time pod interface header.
 * \author Philippe Gerum
 *
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
 *
 * \ingroup pod
 */

#ifndef _xenomai_pod_h
#define _xenomai_pod_h

/*! \addtogroup pod
 *@{*/

#include "xenomai/thread.h"

/* Creation flags */
#define XNDREORD 0x00000001	/* Don't reorder pend queues upon prio change */

/* Status flags */
#define XNRPRIO  0x00000002	/* Reverse priority scheme */
#define XNTIMED  0x00000004	/* Timer started */
#define XNTMSET  0x00000008	/* Pod time has been set */
#define XNSCHED  0x00000010	/* Pod needs rescheduling */
#define XNFATAL  0x00000020	/* Pod encountered a fatal error */
#define XNKCOUT  0x00000040	/* Kernel callout context */

/* These flags are available to the real-time interfaces */
#define XNPOD_SPARE0  0x01000000
#define XNPOD_SPARE1  0x02000000
#define XNPOD_SPARE2  0x04000000
#define XNPOD_SPARE3  0x08000000
#define XNPOD_SPARE4  0x10000000
#define XNPOD_SPARE5  0x20000000
#define XNPOD_SPARE6  0x40000000
#define XNPOD_SPARE7  0x80000000

/* Flags for context checking */
#define XNPOD_THREAD_CONTEXT     0x1 /* Regular thread */
#define XNPOD_INTERRUPT_CONTEXT  0x2 /* Interrupt service thread */
#define XNPOD_HOOK_CONTEXT       0x4 /* Nanokernel hook */

#define XNPOD_NORMAL_EXIT  0x0
#define XNPOD_FATAL_EXIT   0x1

/* Error codes thrown by the POD management routines */
#define XNPOD_ERRBASE  0xffff1000

#define XNERR_POD_NOMEM   (XNPOD_ERRBASE + 1)
#define XNERR_POD_BUSY    (XNPOD_ERRBASE + 2)
#define XNERR_POD_UNINIT  (XNPOD_ERRBASE + 3)
#define XNERR_POD_NOHOOK  (XNPOD_ERRBASE + 4)

#define XNPOD_DEFAULT_TICK         0  /* i.e. Use XNARCH_DEFAULT_TICK */
#define XNPOD_DEFAULT_TICKHANDLER  (&xnpod_announce_tick)

#define XNPOD_HEAPSIZE  (128 * 1024)
#define XNPOD_PAGESIZE  512
#define XNPOD_RUNPRI    0x80000000 /* Placeholder for "stdthread priority" */

/* Flags for xnpod_schedule_runnable() */
#define XNPOD_SCHEDFIFO 0x0
#define XNPOD_SCHEDLIFO 0x1
#define XNPOD_NOSWITCH  0x2

/* Normal root thread priority == min_std_prio - 1 */
#define XNPOD_ROOT_PRIO_BASE   ((nkpod)->root_prio_base)

/* Kernel debugger priority == max_std_prio + 999 */
#define XNPOD_KDEBUG_PRIO_BASE ((nkpod)->kdbg_prio_base)

/* Idle svc thread priority == min_std_prio - 2 */
#define XNPOD_ISVC_PRIO_IDLE     ((nkpod)->isvc_prio_idle)

/* max_std_prio + 1 <= prio < max_std_prio + max_irq_prio + 1 */
#define XNPOD_ISVC_PRIO_BASE(iprio) \
xnpod_get_maxprio(nkpod,iprio + 1)

/*! 
 * \brief Scheduling information structure.
 */

typedef struct xnsched {

    xnqueue_t suspendq;		/*!< Suspended (blocked) threads. */

    xnpqueue_t readyq;		/*!< Ready-to-run threads (prioritized). */

    xnthread_t rootcb;		/*!< Root thread control block. */

    xnthread_t *runthread;	/*!< Current thread (service or user). */

    xnthread_t *usrthread;	/*!< Last regular real-time thread scheduled. */

    xnthread_t *fpuholder;	/*!< Thread owning the current FPU context. */

    unsigned inesting;		/*!< Interrupt nesting level. */

} xnsched_t;

struct xnsynch;
struct xnintr;
struct xnmutex;

/*! 
 * \brief Real-time pod descriptor.
 *
 * The source of all Xenomai magic.
 */

typedef struct xnpod {

    xnsched_t sched;		/*!< The UP scheduler. */

    xnflags_t status;		/*!< Status bitmask. */

    xnqueue_t threadq;		/*!< All existing threads. */

    xnqueue_t timerwheel[XNTIMER_WHEELSIZE]; /*!< BSDish timer wheel. */

    xnticks_t jiffies;		/*!< Periodic ticks elapsed since boot. */

    xnticks_t wallclock;	/*!< Wallclock time in ticks. */

    atomic_counter_t schedlck;	/*!< Scheduler lock count. */

    xnqueue_t tstartq,		/*!< Thread start hook queue. */
	      tswitchq,		/*!< Thread switch hook queue. */
	      tdeleteq;		/*!< Thread delete hook queue. */

    int minpri,			/*!< Minimum priority value. */
        maxpri;			/*!< Maximum priority value. */

    int root_prio_base,		/*!< ROOT thread standard base priority. */
	isvc_prio_idle,		/*!< ISVC thread idle priority. */
	kdbg_prio_base;		/*!< Debugger thread standard base priority. */

    u_long tickvalue;		/*!< Tick duration (ns). */

    xnticks_t ticks2sec;	/*!< Number of ticks per second. */

    struct {
	void (*tickhandler)(struct xnintr *); /*!< Periodic tick handler. */
	void (*shutdown)(int xtype); /*!< Shutdown hook. */
	void (*settime)(xnticks_t newtime); /*!< Clock setting hook. */
	int (*faulthandler)(xnarch_fltinfo_t *fltinfo); /*!< Trap/exception handler. */
    } svctable;			/*!< Table of overridable service entry points. */

    void (*schedhook)(xnthread_t *thread,
		      xnflags_t mask); /*!< Internal scheduling hook. */

} xnpod_t;

extern xnpod_t *nkpod;

#define xnprintf       xnarch_printf
#define xnmalloc(size) xnheap_alloc(&kheap,size,XNHEAP_WAIT)
#define xnfree(ptr)    xnheap_free(&kheap,ptr)

#ifdef __cplusplus
extern "C" {
#endif

void xnpod_schedule_runnable(xnthread_t *thread,
			     int flags);

static inline int xnpod_get_qdir (xnpod_t *pod) {
    /* Returns the queuing direction of threads for a given pod */
    return testbits(pod->status,XNRPRIO) ? xnqueue_up : xnqueue_down;
}

static inline int xnpod_get_minprio (xnpod_t *pod, int incr) {
    return xnpod_get_qdir(pod) == xnqueue_up ?
	pod->minpri + incr :
	pod->minpri - incr;
}

static inline int xnpod_get_maxprio (xnpod_t *pod, int incr) {
    return xnpod_get_qdir(pod) == xnqueue_up ?
	pod->maxpri - incr :
	pod->maxpri + incr;
}

static inline int xnpod_get_relprio (int aprio) {
    /* Returns a normalized 0-based relative priority value computed
       from a skin-based absolute priority level. */
    int delta = aprio - nkpod->minpri;
    return testbits(nkpod->status,XNRPRIO) ? -delta : delta;
}

static inline int xnpod_get_absprio (int rprio) {
    /* Returns a normalized absolute skin-based priority value
       computed from a relative 0-based priority level. */
    int aprio = xnpod_get_minprio(nkpod,rprio);

    if (testbits(nkpod->status,XNRPRIO))
	return aprio < nkpod->maxpri ? nkpod->maxpri : aprio;

    return aprio > nkpod->maxpri ? nkpod->maxpri : aprio;
}

static inline int xnpod_priocompare (int inprio, int outprio) {
    /* Returns a negative, null or positive value whether inprio is
       lower than, equal to or greater than outprio. */
    int delta = inprio - outprio;
    return testbits(nkpod->status,XNRPRIO) ? -delta : delta;
}

static inline void xnpod_renice_isvc (xnthread_t *thread, int prio) {

    spl_t s;

    splhigh(s);
    thread->cprio = prio;
    xnpod_schedule_runnable(thread,XNPOD_SCHEDFIFO);
    splexit(s);
}

static inline void xnpod_renice_root (int prio) {

    spl_t s;

    splhigh(s);
    nkpod->sched.rootcb.cprio = prio;
    xnpod_schedule_runnable(&nkpod->sched.rootcb,XNPOD_SCHEDLIFO|XNPOD_NOSWITCH);
    splexit(s);
}

    /* -- Beginning of the exported interface */

#define xnpod_current_sched() \
(&nkpod->sched)

#define xnpod_interrupt_p() \
(xnpod_current_sched()->inesting > 0)

#define xnpod_callout_p() \
(!!testbits(nkpod->status,XNKCOUT))

#define xnpod_asynch_p() \
(xnpod_interrupt_p() || xnpod_callout_p())

#define xnpod_current_thread() \
(xnpod_current_sched()->runthread)

#define xnpod_current_root() \
(&xnpod_current_sched()->rootcb)

#define xnpod_locked_p() \
(!!testbits(xnpod_current_thread()->status,XNLOCK))

#define xnpod_pendable_p() \
(!(xnpod_asynch_p() || testbits(xnpod_current_thread()->status,XNLOCK)))

#define xnpod_root_p() \
    (!!testbits(xnpod_current_thread()->status,XNROOT))

#define xnpod_shadow_p() \
    (!!testbits(xnpod_current_thread()->status,XNSHADOW))

#define xnpod_userspace_p() \
    (!!testbits(xnpod_current_thread()->status,XNROOT|XNSHADOW))

#define xnpod_idle_p() xnpod_root_p()

#define xnpod_timeset_p() \
(!!testbits(nkpod->status,XNTMSET))

static inline xnticks_t xnpod_get_ticks2sec (void) {
    return nkpod->ticks2sec;
}

static inline u_long xnpod_get_tickval (void) {
    /* Returns the duration of a tick in nanoseconds */
    return nkpod->tickvalue;
}

static inline xntime_t xnpod_ticks2time (xnticks_t ticks) {
    /* Convert a count of ticks in nanoseconds */
    return ticks * xnpod_get_tickval();
}

static inline xnticks_t xnpod_time2ticks (xntime_t t) {
    unsigned long r;
    return xnarch_ulldiv(t,xnpod_get_tickval(),&r);
}

int xnpod_init(xnpod_t *pod,
	       int minpri,
	       int maxpri,
	       xnflags_t flags);

int xnpod_start_timer(u_long nstick,
		      void (*handler)(struct xnintr *));

void xnpod_stop_timer(void);

void xnpod_freeze(void);

void xnpod_unfreeze(void);

void xnpod_shutdown(int xtype);

int xnpod_init_thread(xnthread_t *thread,
		      const char *name,
		      int prio,
		      xnflags_t flags,
		      unsigned stacksize,
		      void *adcookie,
		      unsigned magic);

void xnpod_start_thread(xnthread_t *thread,
			xnflags_t mode,
			int imask,
			void (*entry)(void *cookie),
			void *cookie);

void xnpod_restart_thread(xnthread_t *thread,
			  struct xnmutex *imutex);

void xnpod_delete_thread(xnthread_t *thread,
			 struct xnmutex *imutex);

xnflags_t xnpod_set_thread_mode(xnthread_t *thread,
				xnflags_t clrmask,
				xnflags_t setmask);

int xnpod_suspend_thread(xnthread_t *thread,
			 xnflags_t mask,
			 xnticks_t timeout,
			 struct xnsynch *resource,
			 struct xnmutex *imutex);

void xnpod_resume_thread(xnthread_t *thread,
			 xnflags_t mask);

void xnpod_unblock_thread(xnthread_t *thread);

void xnpod_renice_thread(xnthread_t *thread,
			 int prio);

void xnpod_rotate_readyq(int prio);

void xnpod_schedule(struct xnmutex *imutex);

static inline void xnpod_lock_sched (void) {

    /* Don't swap these two lines... */
    xnarch_atomic_inc(&nkpod->schedlck);
    setbits(xnpod_current_sched()->runthread->status,XNLOCK);
}

static inline void xnpod_unlock_sched (void) {

    if (xnarch_atomic_dec_and_test(&nkpod->schedlck))
	{
	clrbits(xnpod_current_sched()->runthread->status,XNLOCK);

	if (testbits(nkpod->status,XNSCHED))
	    xnpod_schedule(NULL);
	}
}

void xnpod_announce_tick(struct xnintr *intr);

void xnpod_activate_rr(xnticks_t quantum);

void xnpod_deactivate_rr(void);

void xnpod_set_time(xnticks_t newtime);

xnticks_t xnpod_get_time(void);

static inline xntime_t xnpod_get_cpu_time(void) {
    return xnarch_get_cpu_time();
}

int xnpod_add_hook(int type,
		   void (*routine)(xnthread_t *));

int xnpod_remove_hook(int type,
		      void (*routine)(xnthread_t *));

void xnpod_check_context(int mask);

int xnpod_register_debugger(xnthread_t *thread,
			    unsigned stacksize,
			    void (*fentry)(void *cookie),
			    void (*fexit)(void));

void xnpod_unregister_debugger(void);

static inline void xnpod_yield (void) {
    xnpod_resume_thread(xnpod_current_thread(),0);
    xnpod_schedule(NULL);
}

static inline void xnpod_delay (xnticks_t timeout) {
    xnpod_suspend_thread(xnpod_current_thread(),XNDELAY,timeout,NULL,NULL);
}

static inline void xnpod_suspend_self (struct xnmutex *imutex) {
    xnpod_suspend_thread(xnpod_current_thread(),XNSUSP,XN_INFINITE,NULL,imutex);
}

static inline void xnpod_delete_self (struct xnmutex *imutex) {
    xnpod_delete_thread(xnpod_current_thread(),imutex);
}

#ifdef __cplusplus
}
#endif

/*@{*/

#endif /* !_xenomai_pod_h */
