/*!\file pod.c
 * \brief Real-time pod services.
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

/*!
 * \defgroup xenomai Xenomai scheduler.
 *
 * A scheduler for mimicking real-time operating systems.
 *
 * Xenomai is an RTAI scheduler which provides generic real-time operating
 * services suited for the implementation of real-time operating system
 * interfaces.
 */

/*!
 * \ingroup xenomai
 * \defgroup pod Real-time pod services.
 *
 * Real-time pod services.
 *@{*/

#define XENO_POD_MODULE

#include <stdarg.h>
#include "rtai_config.h"
#include "xenomai/pod.h"
#include "xenomai/mutex.h"
#include "xenomai/synch.h"
#include "xenomai/heap.h"
#include "xenomai/intr.h"
#include "xenomai/module.h"

xnpod_t *nkpod = NULL;

xnintr_t nkclock;

const char *xnpod_fatal_helper (const char *format, ...)

{
    static char buf[256];
    xnholder_t *holder;
    va_list ap;
    spl_t s;

    splhigh(s);

    va_start(ap,format);
    vsprintf(buf,format,ap);
    va_end(ap);

    if (!nkpod || testbits(nkpod->status,XNFATAL))
	goto out;

    setbits(nkpod->status,XNFATAL);
    xntimer_freeze();

    xnprintf("--%-12s PRI   STATUS\n","THREAD");

    holder = getheadq(&nkpod->threadq);

    while (holder)
	{
	xnthread_t *thread = link2thread(holder,glink);
	xnprintf("%c %-12s %4d  0x%lx\n",
		 thread == xnpod_current_thread() ? '>' : ' ',
		 thread->name,
		 thread->cprio,thread->status);
	holder = nextq(&nkpod->threadq,holder);
	}

    if (testbits(nkpod->status,XNTIMED))
	{
	if (testbits(nkpod->status,XNTMPER))
	    xnprintf("Timer: periodic [tickval=%luus, elapsed=%Lu]\n",
		     xnpod_get_tickval() / 1000,
		     nkpod->jiffies);
	else
	    xnprintf("Timer: aperiodic.\n");
	}
    else
	xnprintf("Timer: none.\n");

 out:

    splexit(s);

    return buf;
}

/*
 * xnpod_fault_handler -- The default fault handler.
 */

static int xnpod_fault_handler (xnarch_fltinfo_t *fltinfo)

{
    if (!xnpod_userspace_p())
	{
	xnprintf("Xenomai: suspending kernel thread %p ('%s') at 0x%lx after exception #%u\n",
		 xnpod_current_thread(),
		 xnpod_current_thread()->name,
		 xnarch_fault_pc(fltinfo),
		 xnarch_fault_trap(fltinfo));

	/* Put the faulting thread in dormant state since XNSUSP might
	   be cleared as the application continues. */

	xnpod_suspend_thread(xnpod_current_thread(),XNDORMANT,XN_INFINITE,NULL,NULL);

	return 1;
	}

#ifdef __KERNEL__
    /* If we experienced a trap on behalf of a shadow thread, just
       move the second to the Linux domain, so that the host O/S
       (e.g. Linux) can attempt to process the first. This is
       especially useful in order to handle user-space errors
       gracefully. */

    if (xnpod_shadow_p())
	{
	xnshadow_relax();
	return 1;
	}
#endif /* __KERNEL__ */

    return 0;
}

static void xnpod_host_tick (void *cookie) { /* Host tick relay handler */

    xnarch_relay_tick();
}

/*! 
 * \fn void xnpod_init(xnpod_t *pod, int minpri, int maxpri, xnflags_t flags);
 * \brief Initialize a new pod.
 *
 * Initializes a new pod which can subsequently be used to start
 * real-time activities. Once a pod is active, real-time APIs can be
 * stacked over. There can only be a single pod active in the host
 * environment. Such environment can be confined to a process though
 * (e.g. MVM or POSIX layers), or expand to the whole system
 * (e.g. Adeos or RTAI).
 *
 * @param pod The address of a pod descriptor Xenomai will use to
 * store the pod-specific data.  This descriptor must always be valid
 * while the pod is active therefore it must be allocated in permanent
 * memory.
 *
 * @param minpri The value of the lowest priority level which is valid
 * for threads created on behalf of this pod.
 *
 * @param maxpri The value of the highest priority level which is
 * valid for threads created on behalf of this pod.
 *
 * @param flags A set of creation flags affecting the operation.  The
 * only defined flag is XNDREORD (Disable REORDering), which tells the
 * nanokernel that the (xnsynch_t) pend queue should not be reordered
 * whenever the priority of a blocked thread it holds is changed. If
 * this flag is not specified, changing the priority of a blocked
 * thread using xnpod_renice_thread() will cause the pended queue to
 * be reordered according to the new priority level, provided the
 * synchronization object makes the waiters wait by priority order on
 * the awaited resource (XNSYNCH_PRIO).
 *
 * minpri might be numerically higher than maxpri if the upper
 * real-time interface exhibits a reverse priority scheme. For
 * instance, some APIs may define a range like minpri=255, maxpri=0
 * specifying that thread priorities increase as the priority level
 * decreases numerically.
 *
 * Context: This routine must be called on behalf of a context
 * allowing immediate memory allocation requests (e.g. an
 * init_module() routine).
 */

int xnpod_init (xnpod_t *pod, int minpri, int maxpri, xnflags_t flags)

{
    xnsched_t *sched;
    u_long rem;
    int rc, n;

    nkpod = pod;

    if (minpri > maxpri)
	/* The lower the value, the higher the priority */
	flags |= XNRPRIO;

    /* Flags must be set before xnpod_get_qdir() is called */
    pod->status = (flags & (XNRPRIO|XNDREORD));

    initq(&xnmod_glink_queue);
    initq(&pod->threadq);
    initq(&pod->tstartq);
    initq(&pod->tswitchq);
    initq(&pod->tdeleteq);

    for (n = 0; n < XNTIMER_WHEELSIZE; n++)
	initq(&pod->timerwheel[n]);

    xntimer_init(&pod->htimer,&xnpod_host_tick,NULL);

    xnarch_atomic_set(&pod->schedlck,0);
    pod->minpri = minpri;
    pod->maxpri = maxpri;
    pod->jiffies = 0;
    pod->wallclock = 0;
    pod->tickvalue = XNARCH_DEFAULT_TICK;
    pod->ticks2sec = xnarch_ulldiv(1000000000LL,XNARCH_DEFAULT_TICK,&rem);

    pod->svctable.shutdown = &xnpod_shutdown;
    pod->svctable.settime = &xnpod_set_time;
    pod->svctable.tickhandler = NULL;
    pod->svctable.faulthandler = &xnpod_fault_handler;
    pod->schedhook = NULL;
    pod->latency = xnarch_ns_to_tsc(XNARCH_SCHED_LATENCY);

    sched = &pod->sched;
    initq(&sched->suspendq);
    initpq(&sched->readyq,xnpod_get_qdir(pod));

    sched->inesting = 0;
    sched->runthread = NULL;	/* This fools xnheap_init() */
    sched->usrthread = NULL;

    pod->root_prio_base = xnpod_get_minprio(pod,1);
    pod->isvc_prio_idle = xnpod_get_minprio(pod,2);

    if (xnheap_init(&kheap,NULL,XNPOD_HEAPSIZE,XNPOD_PAGESIZE) != XN_OK)
	return XNERR_NOMEM;

    /* Create the root thread -- it might be a placeholder for the
       current context or a real thread, it depends on the real-time
       layer. */

    rc = xnthread_init(&sched->rootcb,
		       "ROOT",
		       XNPOD_ROOT_PRIO_BASE,
		       XNROOT|XNSTARTED
#ifdef CONFIG_RTAI_FPU_SUPPORT
		       /* If the host environment has a FPU, the root
			  thread must care for the FPU context. */
		       |XNFPU
#endif /* CONFIG_RTAI_FPU_SUPPORT */
		       ,
		       XNARCH_ROOT_STACKSZ,
		       NULL,
		       0);
    if (rc)
	return rc;

    sched->runthread = &sched->rootcb;
    sched->usrthread = &sched->rootcb;
#ifdef CONFIG_RTAI_FPU_SUPPORT
    sched->fpuholder = &sched->rootcb;
#endif /* CONFIG_RTAI_FPU_SUPPORT */

    appendq(&pod->threadq,&sched->rootcb.glink);

    xnarch_init_root_tcb(xnthread_archtcb(&sched->rootcb),
			 &sched->rootcb,
			 xnthread_name(&sched->rootcb));

    xnarch_notify_ready();

    return XN_OK;
}

/*! 
 * \fn void xnpod_shutdown(int xtype);
 * \brief Default shutdown handler.
 *
 * Forcibly shutdowns the active pod. All existing nanokernel threads
 * (but the root one) are terminated, and the system heap is freed.
 *
 * @param xtype An exit code passed to the host environment who
 * started the nanokernel. Zero is always interpreted as a successful
 * return.
 *
 * The nanokernel will not call this routine directly but rather use
 * the routine pointed at by the pod.svctable.shutdown member in the
 * service table. This allows upper interfaces to interpose their own
 * shutdown handlers so that they have their word before any action is
 * taken. Usually, the interface-defined handlers should end up
 * calling xnpod_shutdown() after their own housekeeping chores have
 * been carried out.
 *
 * Context: This routine must be called on behalf of the root thread
 * (e.g. a cleanup_module() routine).
 */

void xnpod_shutdown (int xtype)

{
    xnholder_t *holder, *nholder;
    xnthread_t *thread;
    spl_t s;

    if (!nkpod)
	return;	/* No-op */

    xnpod_stop_timer();

    splhigh(s);

    nholder = getheadq(&nkpod->threadq);

    while ((holder = nholder) != NULL)
	{
	nholder = nextq(&nkpod->threadq,holder);
	thread = link2thread(holder,glink);

	if (!testbits(thread->status,XNROOT))
	    xnpod_delete_thread(thread,NULL);
	}

    splexit(s);

    xnheap_destroy(&kheap);

    xntimer_destroy(&nkpod->htimer);

    nkpod = NULL;
}

static inline void xnpod_fire_callouts (xnqueue_t *hookq, xnthread_t *thread)

{
    xnholder_t *holder, *nholder;

    setbits(nkpod->status,XNKCOUT);

    /* The callee is allowed to alter the hook queue when running */

    nholder = getheadq(hookq);

    while ((holder = nholder) != NULL)
	{
	xnhook_t *hook = link2hook(holder);
	nholder = nextq(hookq,holder);
	hook->routine(thread);
	}

    clrbits(nkpod->status,XNKCOUT);
}

/*! 
 * \fn void xnpod_preempt_current_thread(void);
 * \brief Preempts the current thread - INTERNAL.
 *
 * Preempts the running thread (because a more prioritary thread has
 * just been readied).  The thread is re-inserted at the front of its
 * priority group in the ready thread queue.
 */

static inline void xnpod_preempt_current_thread (void)

{
    xnthread_t *thread;
    xnsched_t *sched;
    spl_t s;

    sched = xnpod_current_sched();
    thread = sched->runthread;

    splhigh(s);
    insertpql(&sched->readyq,&thread->rlink,thread->cprio);
    setbits(thread->status,XNREADY);
    setbits(nkpod->status,XNSCHED);
    splexit(s);

    if (!nkpod->schedhook)
	return;

    if (getheadpq(&sched->readyq) != &thread->rlink)
	nkpod->schedhook(thread,XNREADY);
    else if (countpq(&sched->readyq) > 1)
	{
	/* The running thread is still heading the ready queue and
	   more than one thread is linked to this queue, so we may
	   refer to the following element as a thread object
	   (obviously distinct from the running thread) safely. */
	thread = link2thread(thread->rlink.plink.next,rlink);
	nkpod->schedhook(thread,XNREADY);
	}
}

/*! 
 * \fn void xnpod_init_thread(xnthread_t *thread,
                              const char *name,
			         int prio,
			         xnflags_t flags,
			         unsigned stacksize,
			         void *adcookie,
				  unsigned magic);
 * \brief Initialize a new thread.
 *
 * Initializes a new thread attached to the active pod. The thread is
 * left in an innocuous mode until it is actually started by
 * xnpod_start_thread().
 *
 * @param thread The address of a thread descriptor Xenomai will use
 * to store the thread-specific data.  This descriptor must always be
 * valid while the thread is active therefore it must be allocated in
 * permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * thread. This name is copied to a safe place into the thread
 * descriptor. This name might be used in various situations by the
 * nanokernel for issuing human-readable diagnostic messages, so it is
 * usually a good idea to provide a sensible value here. The MVM layer
 * even uses this name intensively to identify threads in the
 * debugging GUI it provides. However, passing NULL is always legal
 * and means "anonymous".
 *
 * @param prio The base priority of the new thread. This value must
 * range from [minpri .. maxpri] (inclusive) as specified when calling
 * the xnpod_init() service.
 *
 * @param flags A set of creation flags affecting the operation. The
 * only defined flag available to the upper interfaces is XNFPU
 * (enable FPU), which tells the nanokernel that the new thread will
 * use the floating-point unit. In such a case, the nanokernel will
 * handle the FPU context save/restore ops upon thread switches at the
 * expense of a few additional cycles per context switch. By default,
 * a thread is not expected to use the FPU. This flag is simply
 * ignored when Xenomai runs on behalf of a userspace-based real-time
 * control layer since the FPU management is always active if
 * present.
 *
 * @param stacksize The size of the stack (in bytes) for the new
 * thread. If zero is passed, the nanokernel will use a reasonable
 * pre-defined size depending on the underlying real-time control
 * layer.
 *
 * @param adcookie An architecture-dependent cookie. The caller should
 * pass the XNARCH_THREAD_COOKIE value defined for all real-time
 * control layers in their respective interface file. This
 * system-defined cookie must not be confused with the user-defined
 * thread cookie passed to the xnpod_start_thread() service.
 *
 * @param magic A magic cookie each skin can define to unambiguously
 * identify threads created in their realm. This value is copied as-is
 * to the "magic" field of the thread struct. 0 is a conventional
 * value for "no magic".
 *
 * @return XN_OK is returned on success. Otherwise, one of the
 * following error codes indicates the cause of the failure:
 *
 *         - XNERR_PARAM is returned if @a flags has invalid bits set.
 *
 *         - XNERR_NOMEM is returned if @a thread is NULL or not
 *         enough memory is available from the system heap to create
 *         the new thread's stack.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

int xnpod_init_thread (xnthread_t *thread,
		       const char *name,
		       int prio,
		       xnflags_t flags,
		       unsigned stacksize,
		       void *adcookie,
		       unsigned magic)
{
    spl_t s;
    int err;

    if (!thread)
	/* Allow the caller to bypass parametrical checks... */
	return XNERR_NOMEM;

    if (flags & ~(XNFPU|XNSHADOW|XNISVC))
	return XNERR_PARAM;

    if (stacksize == 0)
	stacksize = XNARCH_THREAD_STACKSZ;

    err = xnthread_init(thread,name,prio,flags,stacksize,adcookie,magic);

    if (err)
	return err;

    splhigh(s);
    appendq(&nkpod->threadq,&thread->glink);
    xnpod_suspend_thread(thread,XNDORMANT,XN_INFINITE,NULL,NULL);
    splexit(s);

    return XN_OK;
}

/*! 
 * \fn void xnpod_start_thread(xnthread_t *thread,
			       xnflags_t mode,
			       int imask,
			       void (*entry)(void *cookie),
			       void *cookie);
 * \brief Initial start of a newly created thread.
 *
 * Starts a (newly) created thread, scheduling it for the first
 * time. This call releases the target thread from the XNDORMANT
 * state. This service also sets the initial mode and interrupt mask
 * for the new thread.
 *
 * @param thread The descriptor address of the affected thread which
 * must have been previously initialized by the xnpod_init_thread()
 * service.
 *
 * @param mode The initial thread mode. The following flags can be
 * part of this bitmask, each of them affecting the nanokernel
 * behaviour regarding the started thread:
 *
 * - XNLOCK causes the thread to lock the scheduler when it starts.
 * The target thread will have to call the xnpod_unlock_sched()
 * service to unlock the scheduler.
 *
 * - XNRRB causes the thread to be marked as undergoing the
 * round-robin scheduling policy at startup.  The contents of the
 * thread.rrperiod field determines the time quantum (in ticks)
 * allowed for its next slice.
 *
 * - XNASDI disables the asynchronous signal handling for this thread.
 * See xnpod_schedule() for more on this.
 *
 * - XNSUSP makes the thread start in a suspended state. In such a
 * case, the thread will have to be explicitely resumed using the
 * xnpod_resume_thread() service for its execution to actually begin.
 *
 * @param imask The interrupt mask that should be asserted when the
 * thread starts. The processor interrupt state will be set to the
 * given value when the thread starts running. The interpretation of
 * this value might be different across real-time layers, but a
 * non-zero value should always mark an interrupt masking in effect
 * (e.g. cli()). Conversely, a zero value should always mark a fully
 * preemptible state regarding interrupts (i.e. sti()).
 *
 * @param entry The address of the thread's body routine. In other
 * words, it is the thread entry point.
 *
 * @param cookie A user-defined opaque cookie the nanokernel will pass
 * to the emerging thread as the sole argument of its entry point.
 *
 * The START hooks are called on behalf of the calling context (if
 * any).
 *
 * Side-effect: This routine calls the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

void xnpod_start_thread (xnthread_t *thread,
			 xnflags_t mode,
			 int imask,
			 void (*entry)(void *cookie),
			 void *cookie)
{
    spl_t s;

    if (!testbits(thread->status,XNDORMANT))
	return;

    splhigh(s);

    if (testbits(thread->status,XNSTARTED))
	{
	splexit(s);
	return;
	}

    /* Setup the TCB and initial stack frame */

    xnarch_init_tcb(xnthread_archtcb(thread),
		    thread->adcookie);

    xnarch_init_thread(xnthread_archtcb(thread),
		       entry,
		       cookie,
		       imask,
		       thread,
		       thread->name);

    setbits(thread->status,(mode & (XNTHREAD_MODE_BITS|XNSUSP))|XNSTARTED);
    thread->imask = imask;
    thread->imode = (mode & XNTHREAD_MODE_BITS);
    thread->entry = entry;
    thread->cookie = cookie;
    thread->stime = xnarch_get_cpu_time();

    if (testbits(thread->status,XNRRB))
	thread->rrcredit = thread->rrperiod;

    xnpod_resume_thread(thread,XNDORMANT);

    splexit(s);

    if (!(mode & XNSUSP) && nkpod->schedhook)
	nkpod->schedhook(thread,XNREADY);

    if (countq(&nkpod->tstartq) > 0 &&
	!testbits(thread->status,XNTHREAD_SYSTEM_BITS))
	xnpod_fire_callouts(&nkpod->tstartq,thread);

    xnpod_schedule(NULL);
}

/*! 
 * \fn void xnpod_restart_thread(xnthread_t *thread, xnmutex_t *imutex);
 * \brief Restart a thread.
 *
 * Restarts a previously started thread.  The thread is first
 * terminated then respawned using the same information that prevailed
 * when it was first started, including the mode bits and interrupt
 * mask initially passed to the xnpod_start_thread() service. As a
 * consequence of this call, the thread entry point is rerun.
 *
 * @param thread The descriptor address of the affected thread which
 * must have been previously started by the xnpod_start_thread()
 * service.
 *
 * @param imutex The address of an interface mutex currently held by
 * the caller which will be subject to a lock-breaking preemption if
 * the current thread restarts itself.  Passing NULL when no
 * lock-breaking preemption is required is valid. See xnpod_schedule()
 * for more on lock-breaking preemption points.
 *
 * Self-restarting a thread is allowed. However, restarting the root
 * thread is not.
 *
 * Side-effect: This routine calls the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

void xnpod_restart_thread (xnthread_t *thread, xnmutex_t *imutex)

{
    atomic_counter_t imutexval;
    int simutex = 0;
    spl_t s;

    if (!testbits(thread->status,XNSTARTED))
	return; /* Not started yet or not restartable. */

    if (testbits(thread->status,XNROOT|XNSHADOW))
	xnpod_fatal("attempt to restart a user-space thread");

    splhigh(s);

    /* Break the thread out of any wait it is currently in. */
    xnpod_unblock_thread(thread);

    /* Release all ownerships held by the thread on synch. objects */
    xnsynch_release_all_ownerships(thread);

    /* If the task has been explicitely suspended, resume it. */
    if (testbits(thread->status,XNSUSP))
	xnpod_resume_thread(thread,XNSUSP);

    /* Reset modebits. */
    clrbits(thread->status,XNTHREAD_MODE_BITS);
    setbits(thread->status,thread->imode);

    /* Reset task priority to the initial one. */
    thread->cprio = thread->iprio;
    thread->bprio = thread->iprio;

    /* Clear pending signals. */
    thread->signals = 0;

    if (thread == xnpod_current_sched()->runthread)
	{
	/* Clear all sched locks held by the restarted thread. */
	if (testbits(thread->status,XNLOCK))
	    {
	    clrbits(thread->status,XNLOCK);
	    xnarch_atomic_set(&nkpod->schedlck,0);
	    }

	setbits(thread->status,XNRESTART);
	}

    if (imutex)
	{
	simutex = xnmutex_clear_lock(imutex,&imutexval);

	if (simutex < 0)
	    xnpod_schedule_runnable(xnpod_current_thread(),XNPOD_SCHEDLIFO);
	}

    /* Reset the initial stack frame. */
    xnarch_init_thread(xnthread_archtcb(thread),
		       thread->entry,
		       thread->cookie,
		       thread->imask,
		       thread,
		       thread->name);

    /* Running this code tells us that xnpod_restart_thread() was not
       self-directed, so we must reschedule now since our priority may
       be lower than the restarted thread's priority, and re-acquire
       the interface mutex as needed. */

    xnpod_schedule(NULL);

    if (simutex)
	xnmutex_set_lock(imutex,&imutexval);

    splexit(s);
}

/*! 
 * \fn void xnpod_set_thread_mode(xnthread_t *thread,
			          xnflags_t clrmask,
			          xnflags_t setmask);
 * \brief Change a thread's control mode.
 *
 * Change the control mode of a given thread. The control mode affects
 * the behaviour of the nanokernel regarding the specified thread.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * @param clrmask Clears the corresponding bits from the control field
 * before setmask is applied. The scheduler lock held by the current
 * thread can be forcibly released by passing the XNLOCK bit in this
 * mask. In this case, the lock nesting count is also reset to zero.
 *
 * @param setmask The new thread mode. The following flags can be part
 * of this bitmask, each of them affecting the nanokernel behaviour
 * regarding the thread:
 *
 * - XNLOCK causes the thread to lock the scheduler.  The target
 * thread will have to call the xnpod_unlock_sched() service to unlock
 * the scheduler or clear the XNLOCK bit forcibly using this service.
 *
 * - XNRRB causes the thread to be marked as undergoing the
 * round-robin scheduling policy.  The contents of the thread.rrperiod
 * field determines the time quantum (in ticks) allowed for its
 * next slice. If the thread is already undergoing the round-robin
 * scheduling policy at the time this service is called, the time
 * quantum remains unchanged.
 *
 * - XNASDI disables the asynchronous signal handling for this thread.
 * See xnpod_schedule() for more on this.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

xnflags_t xnpod_set_thread_mode (xnthread_t *thread,
				 xnflags_t clrmask,
				 xnflags_t setmask)
{
    xnflags_t oldmode;
    spl_t s;

    splhigh(s);

    oldmode = (thread->status & XNTHREAD_MODE_BITS);
    clrbits(thread->status,clrmask & XNTHREAD_MODE_BITS);
    setbits(thread->status,setmask & XNTHREAD_MODE_BITS);

    if (!(oldmode & XNLOCK))
	{
	if (testbits(thread->status,XNLOCK))
	    /* Actually grab the scheduler lock. */
	    xnpod_lock_sched();
	}
    else if (!testbits(thread->status,XNLOCK))
	xnarch_atomic_set(&nkpod->schedlck,0);

    if (!(oldmode & XNRRB) && testbits(thread->status,XNRRB))
	thread->rrcredit = thread->rrperiod;

    splexit(s);

    return oldmode;
}

/*! 
 * \fn void xnpod_delete_thread(xnthread_t *thread,
                                xnmutex_t *imutex)
 * \brief Delete a thread.
 *
 * Terminates a thread and releases all the nanokernel resources it
 * currently holds. A thread exists in the system since
 * xnpod_init_thread() has been called to create it, so this service
 * must be called in order to destroy it afterwards.
 *
 * @param thread The descriptor address of the terminated thread.
 *
 * @param imutex The address of an interface mutex currently held by
 * the caller which will be subject to a lock-breaking preemption if
 * the current thread is deleted. This parameter only makes sense for
 * self-deleting threads. Passing NULL when no lock-breaking
 * preemption is required is valid. See xnpod_schedule() for more on
 * lock-breaking preemption points.
 *
 * The DELETE hooks are called on behalf of the calling context (if
 * any). The information stored in the thread control block remains
 * valid until all hooks have been called.
 *
 * Self-terminating a thread is allowed. In such a case, this service
 * does not return to the caller.
 *
 * Side-effect: This routine calls the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

void xnpod_delete_thread (xnthread_t *thread, xnmutex_t *imutex)

{
    xnsched_t *sched;
    spl_t s;

    if (testbits(thread->status,XNROOT))
	xnpod_fatal("attempt to delete the root thread");

    if (nkpod->schedhook)
	nkpod->schedhook(thread,XNDELETED);

    sched = thread->sched;

    splhigh(s);

    removeq(&nkpod->threadq,&thread->glink);

    if (!testbits(thread->status,XNTHREAD_BLOCK_BITS))
	{
	if (testbits(thread->status,XNREADY))
	    {
	    removepq(&sched->readyq,&thread->rlink);
	    clrbits(thread->status,XNREADY);
	    }
	}
    else
	{
	if (testbits(thread->status,XNDELAY))
	    xntimer_stop(&thread->timer);

	if (testbits(thread->status,XNTHREAD_BLOCK_BITS & ~XNDELAY))
	    removeq(&sched->suspendq,&thread->slink);
	}

    xntimer_stop(&thread->atimer);

    /* Ensure the rescheduling can take place if the deleted thread is
       the running one. */

    if (testbits(thread->status,XNLOCK))
	{
	clrbits(thread->status,XNLOCK);
	xnarch_atomic_set(&nkpod->schedlck,0);
	}

    if (testbits(thread->status,XNPEND))
	xnsynch_forget_sleeper(thread);

    xnsynch_release_all_ownerships(thread);

#ifdef CONFIG_RTAI_FPU_SUPPORT
    if (thread == sched->fpuholder)
	sched->fpuholder = NULL;
#endif /* CONFIG_RTAI_FPU_SUPPORT */

    setbits(thread->status,XNZOMBIE);

    if (sched->runthread == thread)
	{
	/* We first need to elect a new runthread before switching out
	   the current one forever. Use the thread zombie state to go
	   through the rescheduling procedure then actually destroy
	   the thread object. */
	setbits(nkpod->status,XNSCHED);
	/* The interface mutex will be cleared by the rescheduling
           proc. */
	xnpod_schedule(imutex);
	}
    else
	{
	if (countq(&nkpod->tdeleteq) > 0 &&
	    !testbits(thread->status,XNTHREAD_SYSTEM_BITS))
	    xnpod_fire_callouts(&nkpod->tdeleteq,thread);

	/* Note: the thread control block must remain available until
           the user hooks have been called. FIXME: Catch 22 here,
           whether we choose to run on an invalid stack (cleanup then
           hooks), or to access the TCB space shortly after it has
           been freed while non-preemptible (hooks then
           cleanup)... Option #2 is current. */

	xnthread_cleanup_tcb(thread);

	xnarch_finalize_no_switch(xnthread_archtcb(thread));
	}

    splexit(s);
}

/*!
 * \fn int xnpod_suspend_thread(xnthread_t *thread,
                                xnflags_t mask,
                                xnticks_t timeout,
				    xnsynch_t *wchan,
				    xnmutex_t *imutex);
 * \brief Suspend a thread.
 *
 * Suspends the execution of a thread according to a given suspensive
 * condition. This thread will not be eligible for scheduling until it
 * all the pending suspensive conditions set by this service are
 * removed by one or more calls to xnpod_resume_thread().
 *
 * @param thread The descriptor address of the suspended thread.
 *
 * @param mask The suspension mask specifying the suspensive condition
 * to add to the thread's wait mask. Possible values usable by the
 * caller are:
 *
 * - XNSUSP. This flag forcibly suspends a thread, regardless of any
 * resource to wait for. The wchan parameter should not be significant
 * when using this suspension condition. A reverse call to
 * xnpod_resume_thread() specifying the XNSUSP bit must be issued to
 * remove this condition, which is cumulative with other suspension
 * bits.
 *
 * - XNDELAY. This flags denotes a counted delay wait (in ticks) which
 * duration is defined by the value of the timeout parameter.
 *
 * - XNPEND. This flag denotes a wait for a synchronization object to
 * be signaled. The wchan argument must points to this object. A
 * timeout value can be passed to bound the wait. This suspension mode
 * should not be used directly by the upper interface, but rather
 * through the xnsynch_sleep_on() call.
 *
 * @param timeout The timeout which may be used to limit the time the
 * thread pends for a resource. This value is a wait time given in
 * ticks.  Passing XN_INFINITE specifies an unbounded wait. All other
 * values are used to initialize a watchdog timer.
 *
 * @param wchan The address of a pended resource. This parameter is
 * used internally by the synchronization object implementation code
 * to specify on which object the suspended thread pends.
 *
 * @param imutex The address of an interface mutex currently held by
 * the caller which will be subject to a lock-breaking preemption
 * before the current thread is actually switched out. The
 * corresponding kernel mutex will be automatically reacquired by the
 * nanokernel when the suspended thread is eventually resumed, before
 * xnpod_suspend_thread() returns to its caller. This parameter only
 * makes sense for self-suspending threads. Passing NULL when no
 * lock-breaking preemption is required is valid. See xnpod_schedule()
 * for more on lock-breaking preemption points.
 *
 * @return Returns true if a thread switch took place as a
 * result of this call, false otherwise.
 *
 * Side-effect: A rescheduling immediately occurs if the caller
 * self-suspends, in which case true is always returned.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

int xnpod_suspend_thread (xnthread_t *thread,
			  xnflags_t mask,
			  xnticks_t timeout,
			  xnsynch_t *wchan,
			  xnmutex_t *imutex)
{
    xnsched_t *sched;
    spl_t s;

    /* This routine must be free both from interrupt preemption AND
       mutex ops. */

    if (testbits(thread->status,XNSTARTED) &&
	testbits(thread->status,XNROOT|XNISVC))
	xnpod_fatal("attempt to suspend system thread %s (from %s)",
		    thread->name,
		    xnpod_current_thread()->name);

    if (thread->wchan && wchan)
	xnpod_fatal("invalid conjunctive wait on multiple synch. objects");

    splhigh(s);

    sched = thread->sched;

    if (thread == sched->runthread)
	{
	if (xnpod_locked_p())
	    xnpod_fatal("suspensive call issued while the scheduler was locked");

	setbits(nkpod->status,XNSCHED);
	}

    /* We must make sure that we don't clear the wait channel if a
       thread is first blocked (wchan != NULL) then forcibly suspended
       (wchan == NULL), since these are conjunctive conditions. */

    if (wchan)
	thread->wchan = wchan;

    /* Is the thread ready to run? */

    if (!testbits(thread->status,XNTHREAD_BLOCK_BITS))
	{
	/* A newly created thread is not linked to the ready thread
	   queue yet. */

	if (testbits(thread->status,XNREADY))
	    {
	    removepq(&sched->readyq,&thread->rlink);
	    clrbits(thread->status,XNREADY);
	    }

	if ((mask & ~XNDELAY) != 0)
	    /* If the thread is forcibly suspended outside the simple
	       delay condition, link it to suspension queue. */
	    appendq(&sched->suspendq,&thread->slink);

	clrbits(thread->status,XNRMID|XNTIMEO|XNBREAK);
	}
    else if ((mask & ~XNDELAY) != 0 &&
	     !testbits(thread->status,XNTHREAD_BLOCK_BITS & ~XNDELAY))
	/* If the thread is forcibly suspended while undergoing a
	   simple delay condition, link it to suspension queue too. */
	appendq(&sched->suspendq,&thread->slink);

    setbits(thread->status,mask);

    if (timeout != XN_INFINITE)
	{
	/* Don't start the timer for a thread indefinitely delayed by
	   a call to xnpod_suspend_thread(thread,XNDELAY,0,NULL). */
	setbits(thread->status,XNDELAY);
	xntimer_start(&thread->timer,timeout,XN_INFINITE);
	}
    
    if (nkpod->schedhook)
	nkpod->schedhook(thread,mask);

    if (thread == sched->runthread)
	xnpod_schedule(imutex);

    splexit(s);

    return (thread == sched->runthread);
}

/*!
 * \fn void xnpod_resume_thread(xnthread_t *thread,
                                xnflags_t mask);
 * \brief Resume a thread.
 *
 * Resumes the execution of a thread previously suspended by one or
 * more calls to xnpod_suspend_thread(). This call removes a
 * suspensive condition affecting the target thread. When all
 * suspensive conditions are gone, the thread is left in a READY state
 * at which point it becomes eligible anew for scheduling.
 *
 * @param thread The descriptor address of the resumed thread.
 *
 * @param mask The suspension mask specifying the suspensive condition
 * to remove from the thread's wait mask. Possible values usable by
 * the caller are:
 *
 * - XNSUSP. This flag removes the explicit suspension condition. This
 * condition might be additive to the XNPEND condition.
 *
 * - XNDELAY. This flag removes the counted delay wait condition.
 *
 * - XNPEND. This flag removes the resource wait condition. If a
 * watchdog is armed, it is automatically disarmed by this call.
 *
 * When the thread is eventually resumed by one or more calls to
 * xnpod_resume_thread(), the caller of xnpod_suspend_thread() in the
 * awakened thread that suspended itself should check for the
 * following bits in its own status mask to determine what caused its
 * wake up:
 *
 * - XNRMID means that the caller must assume that the pended
 * synchronization object has been destroyed (see xnsynch_flush()).
 *
 * - XNTIMEO means that the delay elapsed, or the watchdog went off
 * before the corresponding synchronization object was signaled.
 *
 * - XNBREAK means that the wait has been forcibly broken by a call to
 * xnpod_unblock_thread().
 *
 * Side-effect: This service does not call the rescheduling procedure
 * but may affect the ready queue.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_resume_thread (xnthread_t *thread,
			  xnflags_t mask)
{
    xnsched_t *sched;
    spl_t s;

    sched = thread->sched;

    splhigh(s);

    if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* Is thread blocked? */
	{
	clrbits(thread->status,mask); /* Remove suspensive condition(s) */

	if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* still blocked? */
	    {
	    if ((mask & XNDELAY) != 0)
		{
		/* Watchdog fired or break requested -- stop waiting
		   for the resource. */

		xntimer_stop(&thread->timer);

		mask = testbits(thread->status,XNPEND);

		if (mask)
		    {
		    if (thread->wchan)
			xnsynch_forget_sleeper(thread);

		    if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* Still blocked? */
			{
			splexit(s);
			return;
			}
		    }
		else
		    {
		    /* The thread is still suspended (XNSUSP) */
		    splexit(s);
		    return;
		    }
		}
	    else if (testbits(thread->status,XNDELAY))
		{
		if ((mask & XNPEND) != 0)
		    {
		    /* The thread is woken up due to the availability
		       of the requested resource. Cancel the watchdog
		       timer. */
		    xntimer_stop(&thread->timer);
		    clrbits(thread->status,XNDELAY);
		    }

		if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* Still blocked? */
		    {
		    splexit(s);
		    return;
		    }
		}
	    else
		{
		/* The thread is still suspended, but is no more
		   pending on a resource. */

		if ((mask & XNPEND) != 0 && thread->wchan)
		    xnsynch_forget_sleeper(thread);

		splexit(s);
		return;
		}
	    }
	else if ((mask & XNDELAY) != 0)
	    /* The delayed thread is woken up before the delay
	       elapsed. */
	    xntimer_stop(&thread->timer);

	if ((mask & ~XNDELAY) != 0)
	    {
	    /* If the thread was actually suspended, remove it from
	       the suspension queue -- this allows requests like
	       xnpod_suspend_thread(...,thread,XNDELAY,0,...) not to
	       run the following code when the suspended thread is
	       woken up while undergoing an infinite delay. */

	    removeq(&sched->suspendq,&thread->slink);

	    if (thread->wchan)
		xnsynch_forget_sleeper(thread);
	    }
	}
    else if (testbits(thread->status,XNREADY))
	{
	removepq(&sched->readyq,&thread->rlink);
	clrbits(thread->status,XNREADY);
	}

    /* The readied thread is always put at the end of its priority
       group. */

    insertpqf(&sched->readyq,&thread->rlink,thread->cprio);

    if (thread == sched->runthread)
	{
	setbits(thread->status,XNREADY);

	splexit(s);

	if (nkpod->schedhook &&
	    getheadpq(&sched->readyq) != &thread->rlink)
	    /* The running thread does not lead no more the ready
	       queue. */
	    nkpod->schedhook(thread,XNREADY);
	}
    else if (!testbits(thread->status,XNREADY))
	{
	setbits(thread->status,XNREADY);

	splexit(s);

	if (nkpod->schedhook)
	    nkpod->schedhook(thread,XNREADY);
	}
    else
	splexit(s);

    setbits(nkpod->status,XNSCHED);
}

/*!
 * \fn void xnpod_unblock_thread(xnthread_t *thread);
 * \brief Unblock a thread.
 *
 * Breaks the thread out of any wait it is currently in.  This call
 * removes the XNDELAY and XNPEND suspensive conditions previously put
 * by xnpod_suspend_thread() on the target thread. If all suspensive
 * conditions are gone, the thread is left in a READY state at which
 * point it becomes eligible anew for scheduling.
 *
 * @param thread The descriptor address of the unblocked thread.
 *
 * This call neither releases the thread from the XNSUSP, XNRELAX,
 * XNFROZEN nor the XNDORMANT suspensive conditions.
 *
 * When the thread resumes execution, the XNBREAK bit is set in the
 * unblocked thread's status mask. Unblocking a non-blocked thread is
 * perfectly harmless.
 *
 * Side-effect: This service does not call the rescheduling procedure
 * but may affect the ready queue.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_unblock_thread (xnthread_t *thread)

{
    /* Attempt to abort an undergoing "counted delay" wait.  If this
       state is due to an alarm that has been armed to limit the
       sleeping thread's waiting time while it pends for a resource,
       the corresponding XNPEND state will be cleared by
       xnpod_resume_thread() in the same move. Otherwise, this call
       may abort an undergoing infinite wait for a resource (if
       any). */

    if (testbits(thread->status,XNDELAY))
	xnpod_resume_thread(thread,XNDELAY);
    else if (testbits(thread->status,XNPEND))
	xnpod_resume_thread(thread,XNPEND);

    setbits(thread->status,XNBREAK);
}

/*!
 * \fn void xnpod_renice_thread(xnthread_t *thread,
                                int prio);
 * \brief Change the base priority of a thread.
 *
 * Changes the base priority of a thread.  If the XNDREORD flag has
 * not been passed to xnpod_init() and the reniced thread is currently
 * blocked waiting in priority-pending mode (XNSYNCH_PRIO) for a
 * synchronization object to be signaled, the nanokernel will attempt
 * to reorder the object's pend queue so that it reflects the new
 * sleeper's priority.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * @param prio The new thread priority.
 *
 * It is absolutely required to use this service to change a thread
 * priority, in order to have all the needed housekeeping chores
 * correctly performed. i.e. Do *not* change the thread.cprio field by
 * hand, unless the thread is known to be in an innocuous state
 * (e.g. dormant).
 *
 * Side-effects:
 *
 * - This service does not call the rescheduling procedure but may
 * affect the ready queue.
 *
 * - Assigning the same priority to a running or ready thread moves it
 * at the end of the ready queue, thus might cause a manual
 * round-robin effect.
 *
 * - If the reniced thread is a user-space shadow, propagate the
 * request to the mated Linux task.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_renice_thread (xnthread_t *thread, int prio) {

    xnpod_renice_thread_inner(thread,prio,1);
}

void xnpod_renice_thread_inner (xnthread_t *thread, int prio, int propagate)

{
    int oldprio;
    spl_t s;

    splhigh(s);

    oldprio = thread->cprio;

    /* Change the thread priority, taking in account an undergoing PIP
       boost. */

    thread->bprio = prio;

    /* Since we don't want to mess with the priority inheritance
       scheme, we must take care of never lowering the target thread's
       priority level if it undergoes a PIP boost. */

    if (!testbits(thread->status,XNBOOST) ||
	xnpod_priocompare(prio,oldprio) > 0)
	{
	thread->cprio = prio;

	if (prio != oldprio &&
	    thread->wchan != NULL &&
	    !testbits(nkpod->status,XNDREORD))
	    /* Renice the pending order of the thread inside its wait
	       queue, unless this behaviour has been explicitely
	       disabled at pod's level (XNDREORD), or the requested
	       priority has not changed, thus preventing spurious
	       round-robin effects. */
	    xnsynch_renice_sleeper(thread);

	if (!testbits(thread->status,XNTHREAD_BLOCK_BITS|XNLOCK))
	    /* Call xnpod_resume_thread() in order to have the XNREADY
	       bit set, *except* if the thread holds the scheduling,
	       which prevents its preemption. */
	    xnpod_resume_thread(thread,0);
	}

    splexit(s);

#ifdef __KERNEL__
    if (propagate && testbits(thread->status,XNSHADOW))
	xnshadow_renice(thread);
#endif /* __KERNEL__ */
}

/*!
 * \fn void xnpod_rotate_readyq(int prio);
 * \brief Rotate a priority level in the ready queue.
 *
 * The thread at the head of the ready queue of the given priority
 * level is moved to the end of this queue. Therefore, the execution
 * of threads having the same priority is switched.  Round-robin
 * scheduling policies may be implemented by periodically issuing this
 * call in a given period of time. It should be noted that the
 * nanokernel already provides a built-in round-robin mode though (see
 * xnpod_activate_rr()).
 *
 * @param prio The priority level to rotate. if XNPOD_RUNPRI is given,
 * the running thread priority is used to rotate the queue.
 *
 * The priority level which is considered is always the base priority
 * of a thread, not the possibly PIP-boosted current priority
 * value. Specifying a priority level with no thread on it is harmless,
 * and will simply lead to a null-effect.
 *
 * Side-effect: This service does not call the rescheduling procedure
 * but affects the ready queue.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_rotate_readyq (int prio)

{
    xnpholder_t *pholder;
    xnsched_t *sched;
    spl_t s;

    sched = xnpod_current_sched();

    if (countpq(&sched->readyq) == 0)
	return; /* Nobody is ready. */

    splhigh(s);

    /* There is _always_ a regular thread, ultimately the root
       one. Use the base priority, not the priority boost. */

    if (prio == XNPOD_RUNPRI ||
	prio == xnthread_base_priority(sched->usrthread))
	xnpod_resume_thread(sched->usrthread,0);
    else
	{
	pholder = findpqh(&sched->readyq,prio);

	if (pholder)
	    /* This call performs the actual rotation. */
	    xnpod_resume_thread(link2thread(pholder,rlink),0);
	}

    splexit(s);
}

/*! 
 * \fn void xnpod_activate_rr(xnticks_t quantum);
 * \brief Globally activate the round-robin scheduling.
 *
 * This service activates the round-robin scheduling for all threads
 * which have the XNRRB flag set in their status mask (see
 * xnpod_set_thread_mode()). Each of them will run for the given time
 * quantum, then preempted and moved at the end of its priority group
 * in the ready queue. This process is repeated until the round-robin
 * scheduling is disabled for those threads.
 *
 * @param quantum The time credit which will be given to each
 * rr-enabled thread (in ticks).
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_activate_rr (xnticks_t quantum)

{
    xnholder_t *holder;
    spl_t s;

    splhigh(s);

    holder = getheadq(&nkpod->threadq);

    while (holder)
	{
	xnthread_t *thread = link2thread(holder,glink);

	if (testbits(thread->status,XNRRB))
	    {
	    thread->rrperiod = quantum;
	    thread->rrcredit = quantum;
	    }

	holder = nextq(&nkpod->threadq,holder);
	}

    splexit(s);
}

/*! 
 * \fn void xnpod_deactivate_rr(void);
 * \brief Globally deactivate the round-robin scheduling.
 *
 * This service deactivates the round-robin scheduling for all threads
 * which have the XNRRB flag set in their status mask (see
 * xnpod_set_thread_mode()).
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_deactivate_rr (void)

{
    xnholder_t *holder;
    spl_t s;

    splhigh(s);

    holder = getheadq(&nkpod->threadq);

    while (holder)
	{
	xnthread_t *thread = link2thread(holder,glink);

	if (testbits(thread->status,XNRRB))
	    thread->rrcredit = XN_INFINITE;

	holder = nextq(&nkpod->threadq,holder);
	}

    splexit(s);
}

/*! 
 * \fn void xnpod_dispatch_signals(void)
 * \brief Deliver pending asynchronous signals to the running thread -
 * INTERNAL.
 *
 * This internal routine checks for the presence of asynchronous
 * signals directed to the running thread, and attempt to start the
 * asynchronous service routine (ASR) if any.
 */

static void xnpod_dispatch_signals (void)

{
    xnthread_t *thread = xnpod_current_thread();
    xnflags_t oldmode;
    xnsigmask_t sigs;
    int asrimask;
    xnasr_t asr;
    spl_t s;

    /* Are signals pending and ASR enabled for this thread ? */

    if (thread->signals == 0 ||
	testbits(thread->status,XNASDI) ||
	thread->asr == XNTHREAD_INVALID_ASR)
	return;

    /* Start the asynchronous service routine */
    oldmode = testbits(thread->status,XNTHREAD_MODE_BITS);
    sigs = thread->signals;
    asrimask = thread->asrimask;
    asr = thread->asr;

    /* Clear pending signals mask since an ASR can be reentrant */
    thread->signals = 0;

    /* Reset ASR mode bits */
    clrbits(thread->status,XNTHREAD_MODE_BITS);
    setbits(thread->status,thread->asrmode);
    thread->asrlevel++;

    /* Setup ASR interrupt mask then fire it. */
    splhigh(s);
    xnarch_setimask(asrimask);
    asr(sigs);
    splexit(s);

    /* Reset the thread mode bits */
    thread->asrlevel--;
    clrbits(thread->status,XNTHREAD_MODE_BITS);
    setbits(thread->status,oldmode);
}

/*! 
 * \fn void xnpod_welcome_thread(xnthread_t *thread);
 * \brief Thread prologue - INTERNAL.
 *
 * This internal routine is called on behalf of a (re)starting
 * thread's prologue before the user entry point is invoked. This call
 * is reserved for internal housekeeping chores and cannot be inlined.
 */

void xnpod_welcome_thread (xnthread_t *thread)

{
    if (thread->signals)
	xnpod_dispatch_signals();

    if (testbits(thread->status,XNLOCK))
	/* Actually grab the scheduler lock. */
	xnpod_lock_sched();

    if (testbits(thread->status,XNFPU))
	xnarch_init_fpu(xnthread_archtcb(thread));

    clrbits(thread->status,XNRESTART);
}

#ifdef CONFIG_RTAI_FPU_SUPPORT

/* xnpod_switch_fpu() -- Switches to the current thread's FPU
   context, saving the previous one as needed. */

void xnpod_switch_fpu (void)

{
    xnsched_t *sched = xnpod_current_sched();
    xnthread_t *runthread = sched->runthread;

    if (testbits(runthread->status,XNFPU) && sched->fpuholder != runthread)
	{
	if (sched->fpuholder == NULL ||
	    xnarch_fpu_ptr(xnthread_archtcb(sched->fpuholder)) !=
	    xnarch_fpu_ptr(xnthread_archtcb(runthread)))
	    {
	    if (sched->fpuholder)
		xnarch_save_fpu(xnthread_archtcb(sched->fpuholder));

	    xnarch_restore_fpu(xnthread_archtcb(runthread));
	    }

	sched->fpuholder = runthread;
	}
}

#endif /* CONFIG_RTAI_FPU_SUPPORT */

/*! 
 * \fn void xnpod_schedule(xnmutex_t *imutex);
 * \brief Rescheduling procedure entry point.
 *
 * This is the central rescheduling routine which should be called to
 * validate and apply changes which have previously been made to the
 * nanokernel scheduling state, such as suspending, resuming or
 * changing the priority of threads.  This call first determines if a
 * thread switch should take place, and performs it as
 * needed. xnpod_schedule() actually switches threads if:
 *
 * - the running thread has been blocked or deleted.
 * - or, the running thread has become less prioritary than the first
 *   ready to run thread.
 * - or, the running thread does not lead no more the ready threads
 * (round-robin).
 * - or, a real-time thread became ready to run, ending the
 *   scheduler idle state (i.e. The root thread was
 *   running so far).
 *
 * @param imutex The address of an interface mutex currently held by
 * the caller which will be subject to a lock-breaking preemption
 * before the current thread is actually switched out. The
 * corresponding kernel mutex will be automatically reacquired by the
 * nanokernel when the thread is eventually switched in again, before
 * xnpod_schedule() returns to its caller. Passing NULL when no
 * lock-breaking preemption is required is valid. See below.
 *
 * The nanokernel implements a lazy rescheduling scheme so that most
 * of the services affecting the threads state MUST be followed by a
 * call to the rescheduling procedure for the new scheduling state to
 * be applied. In other words, multiple changes on the scheduler state
 * can be done in a row, waking threads up, blocking others, without
 * being immediately translated into the corresponding context
 * switches, like it would be necessary would it appear that a more
 * prioritary thread than the current one became runnable for
 * instance. When all changes have been applied, the rescheduling
 * procedure is then called to consider those changes, and possibly
 * replace the current thread by another.
 *
 * As a notable exception to the previous principle however, every
 * action which ends up suspending or deleting the current thread
 * begets an immediate call to the rescheduling procedure on behalf of
 * the service causing the state transition. For instance,
 * self-suspension, self-destruction, or sleeping on a synchronization
 * object automatically leads to a call to the rescheduling procedure,
 * therefore the caller does not need to explicitely issue
 * xnpod_schedule() after such operations.
 *
 * Lock-breaking preemption is a mean by which a thread who holds a
 * nanokernel mutex (i.e. xnmutex_t) can rely on xnpod_schedule() to
 * release such mutex and switch in another thread atomically. The
 * incoming thread can then grab this mutex while the initial holder
 * is suspended. The nanokernel automatically reacquires the mutex on
 * behalf of the initial holder when it eventually resumes execution.
 * This is a desirable feature which provides a simple and safe way
 * for the upper interfaces to deal with scheduling points inside
 * critical sections. Since the aforementioned mutex is usually
 * defined by a client real-time interface to protect from races when
 * concurrent threads access its internal data structures, it is
 * dubbed the "interface mutex" in the Xenomai documentation.
 *
 * The rescheduling procedure always leads to a null-effect if the
 * scheduler is locked (XNLOCK bit set in the status mask of the
 * running thread), or if it is called on behalf of an interrupt
 * service thread.  (ISVC threads have a separate internal
 * rescheduling procedure named xnpod_schedule_runnable()).
 *
 * Calling this procedure with no applicable context switch pending is
 * harmless and simply leads to a null-effect.
 *
 * Side-effects:

 * - If an asynchronous service routine exists, the pending
 * asynchronous signals are delivered to a resuming thread or on
 * behalf of the caller before it returns from the procedure if no
 * context switch has taken place. This behaviour can be disabled by
 * setting the XNASDI flag in the thread's status mask by calling
 * xnpod_set_thread_mode().
 * 
 * - The switch hooks are called on behalf of the resuming thread.
 *
 * - This call may affect the ready queue and switch thread contexts.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_schedule (xnmutex_t *imutex)

{
    xnthread_t *threadout, *threadin, *runthread;
    atomic_counter_t imutexval;
    int doswitch, simutex = 0;
    xnsched_t *sched;
    spl_t s;
#ifdef __KERNEL__
    int shadow;
#endif /* __KERNEL__ */

    /* No immediate rescheduling is possible if an interrupt service
       thread or callout context is active or if the scheduler is
       locked. */

    if (xnpod_callout_p())
	return;

    sched = xnpod_current_sched();

    runthread = sched->runthread;

    /* We have to deal with xnpod_schedule() being called by a regular
       thread that has been resumed through a mutex wakeup operation
       so it can release a mutex claimed by an interrupt service
       thread. In such a case, we just let it release the interface
       mutex as needed on entry; when it later resumes, it will simply
       execute the rescheduling procedure. Interrupt service threads
       can _never_ execute the standard rescheduling code, they always
       use the xnpod_schedule_runnable() routine to switch to the next
       runnable thread. */

    if (xnpod_interrupt_p())
	{
	if (testbits(runthread->status,XNISVC))
	    return;
	}
    else if (testbits(runthread->status,XNLOCK))
	     {
	     /* The running thread has locked the scheduler and is
		still ready to run. Just check for (self-posted)
		pending signals, then exit the procedure. In this
		particular case, we must make sure that the interface
		mutex is free while the ASR is running, since the
		thread might self-deletes from the routine. */
	    
	     if (runthread->signals)
		 {
		 splhigh(s);

		 if (imutex)
		     {
		     simutex = xnmutex_clear_lock(imutex,&imutexval);

		     if (simutex < 0)
			 xnpod_schedule_runnable(runthread,XNPOD_SCHEDLIFO);
		     }

		 xnpod_dispatch_signals();

		 if (simutex)
		     xnmutex_set_lock(imutex,&imutexval);

		 splexit(s);
		 }

	     return;
	     }

    splhigh(s);

    /* The rescheduling proc automagically releases the interface
       mutex (if given) before switching the runthread out then
       reacquires it/them after switching the thread in so that
       callers can save tricky critical section management. The
       atomicity of the operation is kept while releasing the locks by
       xnmutex_clear_lock() which reschedules but does not switch. */

    if (imutex)
	{
	simutex = xnmutex_clear_lock(imutex,&imutexval);

	if (simutex < 0)
	    xnpod_schedule_runnable(runthread,XNPOD_SCHEDLIFO);
	}

    doswitch = 0;

    if (!testbits(runthread->status,XNTHREAD_BLOCK_BITS|XNZOMBIE))
	{
	if (countpq(&sched->readyq) > 0)
	    {
	    xnthread_t *head = link2thread(getheadpq(&sched->readyq),rlink);
	    
	    if (head == runthread)
		doswitch++;
	    else if (xnpod_priocompare(head->cprio,runthread->cprio) > 0)
		{
		if (!testbits(runthread->status,XNREADY))
		    /* Preempt the running thread */
		    xnpod_preempt_current_thread();

		doswitch++;
		}
	    else if (testbits(runthread->status,XNREADY))
		doswitch++;
	    }
	}
    else
	doswitch++;

    /* Clear the rescheduling bit */
    clrbits(nkpod->status,XNSCHED);

    if (!doswitch)
	{
noswitch:
	/* Check for signals (self-posted or posted from an interrupt
	   context) in case the current thread keeps
	   running. Interface mutex must be released while ASR is
	   executed just in case the thread self-deletes from the
	   routine. */

	if (runthread->signals)
	    xnpod_dispatch_signals();

	if (simutex)
	    xnmutex_set_lock(imutex,&imutexval);

	splexit(s);

	return;
	}

    threadout = runthread;
    threadin = link2thread(getpq(&sched->readyq),rlink);

#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    if (!threadin)
	xnpod_fatal("schedule: no thread to schedule?!");
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */

    clrbits(threadin->status,XNREADY);

    if (threadout == threadin &&
	/* Note: the root thread never restarts. */
	!testbits(threadout->status,XNRESTART))
	goto noswitch;

#ifdef __KERNEL__
    shadow = testbits(threadout->status,XNSHADOW);
#endif /* __KERNEL__ */

    if (testbits(threadout->status,XNZOMBIE))
	{
	splexit(s);

	if (countq(&nkpod->tdeleteq) > 0 &&
	    !testbits(threadout->status,XNTHREAD_SYSTEM_BITS))
	    xnpod_fire_callouts(&nkpod->tdeleteq,threadout);

	splhigh(s);

	sched->runthread = threadin;
	sched->usrthread = threadin;

	if (testbits(threadin->status,XNROOT))
	    xnarch_enter_root(xnthread_archtcb(threadin));

	xnthread_cleanup_tcb(threadout);
	
 	xnarch_finalize_and_switch(xnthread_archtcb(threadout),
				   xnthread_archtcb(threadin));
#ifdef __KERNEL__
	if (shadow)
	    /* Reap the user-space mate of a deleted real-time shadow.
	       The Linux task has resumed into the Linux domain at the
	       last code location executed by the shadow. Remember
	       that both sides use the Linux task's stack. */
	    xnshadow_exit();
#endif /* __KERNEL__ */

	xnpod_fatal("zombie thread %s (%p) will not die...",threadout->name,threadout);
	}

    sched->runthread = threadin;
    sched->usrthread = threadin;

    if (testbits(threadout->status,XNROOT))
	xnarch_leave_root(xnthread_archtcb(threadout));
    else if (testbits(threadin->status,XNROOT))
	xnarch_enter_root(xnthread_archtcb(threadin));

    xnarch_switch_to(xnthread_archtcb(threadout),
		     xnthread_archtcb(threadin));

    runthread = sched->runthread;

#ifdef CONFIG_RTAI_FPU_SUPPORT
    xnpod_switch_fpu();
#endif /* CONFIG_RTAI_FPU_SUPPORT */

#ifdef __KERNEL__
    /* Shadow on entry and root without shadow extension on exit? 
       Mmmm... This must be the user-space mate of a deleted real-time
       shadow we've just rescheduled in the Linux domain to have it
       exit properly.  Reap it now. */
    if (shadow &&
	testbits(runthread->status,XNROOT) &&
	xnshadow_ptd(current) == NULL)
	{
	splexit(s);
	xnshadow_exit();
	}
#endif /* __KERNEL__ */

    if (runthread->signals)
	xnpod_dispatch_signals();

    if (simutex)
	xnmutex_set_lock(imutex,&imutexval);

    splexit(s);

    if (nkpod->schedhook)
	nkpod->schedhook(runthread,XNRUNNING);
    
    if (countq(&nkpod->tswitchq) > 0 &&
	!testbits(runthread->status,XNTHREAD_SYSTEM_BITS))
	xnpod_fire_callouts(&nkpod->tswitchq,runthread);
}

/*! 
 * \fn void xnpod_schedule_runnable(xnthread_t *thread,
                                    int flags);
 * \brief Hidden rescheduling procedure - INTERNAL.
 *
 * This internal routine should NEVER be used directly by the upper
 * interfaces. It reinserts the given thread into the ready queue then
 * switches to the most prioritary runnable thread. It must be called
 * interrupts off.
 *
 * @param thread The descriptor address of the thread to reinsert into
 * the ready queue.
 *
 * @param flags A bitmask composed as follows:
 *
 *        - XNPOD_SCHEDLIFO causes the target thread to be inserted at
 *        front of its priority group in the ready queue. Otherwise,
 *        the FIFO ordering is applied.
 *
 *        - XNPOD_NOSWITCH reorders the ready queue without switching
 *        contexts. This feature is used by the nanokernel mutex code
 *        to preserve the atomicity of some operations.
 */

void xnpod_schedule_runnable (xnthread_t *thread, int flags)

{
    xnsched_t *sched = thread->sched;
    xnthread_t *runthread = sched->runthread, *threadin;

    if (thread != runthread)
	{
	removepq(&sched->readyq,&thread->rlink);

	/* The running thread might be in the process of being blocked
	   or reniced but not (un/re)scheduled yet.  Therefore, we
	   have to be careful about not spuriously inserting this
	   thread into the readyq. */

	if (!testbits(runthread->status,XNTHREAD_BLOCK_BITS|XNREADY))
	    {
	    /* Since the runthread is preempted, it must be put at
               _front_ of its priority group so that no spurious
               round-robin effect can occur, unless it holds the
               scheduler lock, in which case it is put at front of the
               readyq, regardless of its priority. */

	    if (testbits(runthread->status,XNLOCK))
		prependpq(&sched->readyq,&runthread->rlink);
	    else
		insertpql(&sched->readyq,&runthread->rlink,runthread->cprio);

	    setbits(runthread->status,XNREADY);
	    }
	}
    else if (testbits(thread->status,XNTHREAD_BLOCK_BITS))
	/* Same remark as before in the case this routine is called
	   with a soon-to-be-blocked running thread as argument. */
	goto maybe_switch;

    if (flags & XNPOD_SCHEDLIFO)
	/* Insert LIFO inside priority group */
	insertpql(&sched->readyq,&thread->rlink,thread->cprio);
    else
	/* Insert FIFO inside priority group */
	insertpqf(&sched->readyq,&thread->rlink,thread->cprio);

    setbits(thread->status,XNREADY);

maybe_switch:

    if (flags & XNPOD_NOSWITCH)
	{
	if (testbits(runthread->status,XNREADY))
	    {
	    removepq(&sched->readyq,&runthread->rlink);
	    clrbits(runthread->status,XNREADY);
	    }

	return;
	}
   
    threadin = link2thread(getpq(&sched->readyq),rlink);

#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    if (!threadin)
	xnpod_fatal("schedule_runnable: no thread to schedule?!");
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */

    clrbits(threadin->status,XNREADY);

    if (threadin == runthread)
	return;	/* No switch. */

    sched->runthread = threadin;

    if (!testbits(threadin->status,XNTHREAD_SYSTEM_BITS))
	sched->usrthread = threadin;

    if (testbits(runthread->status,XNROOT))
	xnarch_leave_root(xnthread_archtcb(runthread));
    else if (testbits(threadin->status,XNROOT))
	xnarch_enter_root(xnthread_archtcb(threadin));

    if (nkpod->schedhook)
	nkpod->schedhook(runthread,XNREADY);

    xnarch_switch_to(xnthread_archtcb(runthread),
		     xnthread_archtcb(threadin));

#ifdef CONFIG_RTAI_FPU_SUPPORT
    xnpod_switch_fpu();
#endif /* CONFIG_RTAI_FPU_SUPPORT */

    if (nkpod->schedhook && runthread == sched->runthread)
	nkpod->schedhook(runthread,XNRUNNING);
}

/*! 
 * \fn void xnpod_set_time(xnticks_t newtime);
 * \brief Set the nanokernel idea of time.
 *
 * The nanokernel tracks the current time as a monotonously increasing
 * count of ticks announced by the timer source since the epoch. The
 * epoch is initially defined by the time the nanokernel has started.
 * This service changes the epoch. Running timers use a different time
 * base thus are not affected by this operation. The nanokernel time
 * is only accounted when the system timer runs in periodic mode.
 *
 * Side-effect:
 *
 * - This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_set_time (xnticks_t newtime)

{
    spl_t s;

    splhigh(s);
    nkpod->wallclock = newtime;
    setbits(nkpod->status,XNTMSET);
    splexit(s);
}

/*! 
 * \fn xnticks_t xnpod_get_time(void);
 * \brief Get the nanokernel idea of time.
 *
 * This service gets the nanokernel (external) clock time.
 *
 * @return The current nanokernel time (in ticks) if the underlying
 * time source runs in periodic mode, or the CPU tick count if the
 * aperiodic mode is in effect, or no timer is running.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of any context.
 */

xnticks_t xnpod_get_time (void)

{
    if (testbits(nkpod->status,XNTMPER))
	return nkpod->wallclock;

    /* In aperiodic mode, our idea of time is the same as the
       CPU's. */

    return xnarch_get_cpu_tsc(); /* CPU ticks. */
}

/*! 
 * \fn int xnpod_add_hook(int type,
                          void (*routine)(xnthread_t *));
 * \brief Install a nanokernel hook.
 *
 * The nanokernel allows to register user-defined routines which get
 * called whenever a specific scheduling event occurs. Multiple hooks
 * can be chained for a single event type, and get called on a FIFO
 * basis.
 *
 * The scheduling is locked while a hook is executing.
 *
 * @param type Defines the kind of hook to install:
 *
 *        - XNHOOK_THREAD_START: The user-defined routine will be
 *        called on behalf of the starter thread whenever a new thread
 *        starts. The descriptor address of the started thread is
 *        passed to the routine.
 *
 *        - XNHOOK_THREAD_DELETE: The user-defined routine will be
 *        called on behalf of the deletor thread whenever a thread is
 *        deleted. The descriptor address of the deleted thread is
 *        passed to the routine.
 *
 *        - XNHOOK_THREAD_SWITCH: The user-defined routine will be
 *        called on behalf of the resuming thread whenever a context
 *        switch takes place. The descriptor address of the thread
 *        which has been switched out is passed to the routine.
 *
 * @param routine The address of the user-supplied routine to call.
 *
 * @return XN_OK is returned on success. Otherwise, one of the
 * following error codes indicates the cause of the failure:
 *
 *         - XNERR_PARAM is returned if type is incorrect.
 *
 *         - XNERR_NOMEM is returned if not enough memory is
 *         available from the system heap to add the new hook.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

int xnpod_add_hook (int type, void (*routine)(xnthread_t *))

{
    xnqueue_t *hookq;
    xnhook_t *hook;
    spl_t s;

    switch (type)
	{
	case XNHOOK_THREAD_START:
	    hookq = &nkpod->tstartq;
	    break;
	case XNHOOK_THREAD_SWITCH:
	    hookq = &nkpod->tswitchq;
	    break;
	case XNHOOK_THREAD_DELETE:
	    hookq = &nkpod->tdeleteq;
	    break;
	default:
	    return XNERR_PARAM;
	}

    hook = xnmalloc(sizeof(*hook));

    if (!hook)
	return XNERR_NOMEM;

    inith(&hook->link);
    hook->routine = routine;
    splhigh(s);
    prependq(hookq,&hook->link);
    splexit(s);

    return XN_OK;
}

/*! 
 * \fn int xnpod_remove_hook(int type,
                             void (*routine)(xnthread_t *));
 * \brief Remove a nanokernel hook.
 *
 * This service removes a nanokernel hook previously registered using
 * xnpod_add_hook().
 *
 * @param type Defines the kind of hook to remove among
 * XNHOOK_THREAD_START, XNHOOK_THREAD_DELETE and XNHOOK_THREAD_SWITCH.
 *
 * @param routine The address of the user-supplied routine to remove.
 *
 * @return XN_OK is returned on success. Otherwise,
 * XNERR_PARAM is returned if type is incorrect or if the routine
 * has never been registered before.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

int xnpod_remove_hook (int type, void (*routine)(xnthread_t *))

{
    xnhook_t *hook = NULL;
    xnholder_t *holder;
    xnqueue_t *hookq;
    spl_t s;

    switch (type)
	{
	case XNHOOK_THREAD_START:
	    hookq = &nkpod->tstartq;
	    break;
	case XNHOOK_THREAD_SWITCH:
	    hookq = &nkpod->tswitchq;
	    break;
	case XNHOOK_THREAD_DELETE:
	    hookq = &nkpod->tdeleteq;
	    break;
	default:
	    return XNERR_PARAM;
	}

    splhigh(s);

    for (holder = getheadq(hookq); holder; holder = nextq(hookq,holder))
	{
	hook = link2hook(holder);

	if (hook->routine == routine)
	    {
	    removeq(hookq,holder);
	    break;
	    }
	}

    splexit(s);

    if (!hook)
	return XNERR_PARAM;

    xnfree(hook);

    return XN_OK;
}

void xnpod_check_context (int mask)

{
    xnsched_t *sched = xnpod_current_sched();

    if ((mask & XNPOD_THREAD_CONTEXT) && !xnpod_asynch_p())
	return;

    if ((mask & XNPOD_INTERRUPT_CONTEXT) && sched->inesting > 0)
	return;

    if ((mask & XNPOD_HOOK_CONTEXT) && xnpod_callout_p())
	return;

    xnpod_fatal("illegal context for call: current=%s, mask=0x%x",
		xnpod_asynch_p() ? "ISR/callout" : xnpod_current_thread()->name,
		mask);
}

/*! 
 * \fn void xnpod_trap_fault(void *fltinfo);
 * \brief Default fault handler.
 *
 * This is the default handler which is called whenever an
 * uncontrolled exception or fault is caught. If the fault is caught
 * on behalf of a real-time thread, the fault handler stored into the
 * service table (svctable.faulthandler) is invoked and the fault is
 * not propagated to the host system. Otherwise, the fault is
 * unhandled by the nanokernel and simply propagated.
 *
 * @param fltinfo An opaque pointer to the arch-specific buffer
 * describing the fault. The actual layout is defined by the
 * xnarch_fltinfo_t type in each arch-dependent layer file.
 *
 */

int xnpod_trap_fault (void *fltinfo)

{
    if (nkpod == NULL || xnpod_idle_p())
	return 0;

    return nkpod->svctable.faulthandler(fltinfo);
}

static void xnpod_clock_irq (void) /* Low-level clock irq handling */

{
    xnarch_memory_barrier();

    nkclock.pending++;

    if (xnpod_priocompare(nkclock.svcthread.cprio,nkclock.priority) < 0)
	xnpod_renice_isvc(&nkclock.svcthread,nkclock.priority);
}

/*! 
 * \fn int xnpod_start_timer(u_long nstick, xnist_t handler)
 * \brief Start the system timer.
 *
 * The nanokernel needs a time source to provide the time-related
 * services to the upper interfaces. xnpod_start_timer() tunes the
 * timer hardware so that a user-defined routine is called according
 * to a given frequency. On architectures that provide a
 * oneshot-programmable time source, the system timer can operate
 * whether in aperiodic or periodic mode. Using the aperiodic mode
 * still allows to run periodic nanokernel timers over it: the
 * underlying hardware will simply be reprogrammed after each tick by
 * the timer manager using the appropriate interval value (see
 * xntimer_start()).
 *
 * The time interval that elapses between two consecutive invocations
 * of the handler is called a tick.
 *
 * @param nstick The timer period in nanoseconds. XNPOD_DEFAULT_TICK
 * can be used to set this value according to the arch-dependent
 * settings. If this parameter is equal to XNPOD_APERIODIC_TICK, the
 * underlying hardware timer is set to operate in oneshot-programmable
 * mode. In this mode, timing accuracy is higher - since it is not
 * rounded to a constant time slice - at the expense of a lesser
 * efficicency when many timers are simultaneously active. The
 * aperiodic mode gives better results in configuration involving a
 * few threads requesting timing services over different time scales
 * that cannot be easily expressed as multiples of a single base tick,
 * or would lead to a waste of high frequency periodical ticks.
 *
 * @param handler The address of the interrupt service thread which
 * will process each incoming tick. XNPOD_DEFAULT_TICKHANDLER can be
 * passed to use the system-defined entry point
 * (i.e. xnpod_announce_tick()). In any case, a user-supplied handler
 * should end up calling xnpod_announce_tick() to inform the
 * nanokernel of the incoming tick.
 *
 * @return XN_OK is returned on success. Otherwise:
 *
 * - XNERR_BUSY is returned if the timer has already been set.
 * xnpod_stop_timer() must be issued before xnpod_start_timer() is
 * called again.
 *
 * - XNERR_PARAM is returned if an invalid null tick handler has been
 * passed, or if the timer precision cannot represent the duration of
 * a single host tick.
 *
 * - XNERR_NOSYS is returned if the underlying architecture does
 * not support the requested aperiodic timing, or if no active pod
 * exists.
 *
 * Side-effect: A host timing service is started in order to relay the
 * canonical periodical tick to the underlying architecture,
 * regardless of the frequency used for Xenomai's system tick. This
 * routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a context
 * allowing immediate memory allocation requests (e.g. an
 * init_module() routine).
 */

int xnpod_start_timer (u_long nstick, xnist_t tickhandler)

{
    u_long rem;
    spl_t s;

    if (!nkpod)
	return XNERR_NOSYS;

    if (tickhandler == NULL)
	return XNERR_PARAM;

#if XNARCH_APERIODIC_PREC == 0
    if (nstick == XNPOD_APERIODIC_TICK)
	return XNERR_NOSYS;	/* No aperiodic support */
#endif /* XNARCH_APERIODIC_PREC == 0 */
	
    splhigh(s);

    if (testbits(nkpod->status,XNTIMED))
	{
	splexit(s);
	return XNERR_BUSY;
	}

#if XNARCH_APERIODIC_PREC != 0
    if (nstick == XNPOD_APERIODIC_TICK) /* Aperiodic mode. */
	{
	clrbits(nkpod->status,XNTMPER);
	nkpod->tickvalue = 1;	/* highest precision: 1ns */
 	nkpod->ticks2sec = 1000000000;
	}
    else /* Periodic setup. */
#endif /* XNARCH_APERIODIC_PREC != 0 */
	{
	setbits(nkpod->status,XNTMPER);
	/* Pre-calculate the number of ticks per second. */
	nkpod->tickvalue = nstick;
	nkpod->ticks2sec = xnarch_ulldiv(1000000000LL,nstick,&rem);
	}

    if (XNARCH_HOST_TICK < nkpod->tickvalue)
	{
	/* Host tick shorter than the timer precision; bad... */
	splexit(s);
	return XNERR_PARAM;
	}

    nkpod->svctable.tickhandler = tickhandler;

    setbits(nkpod->status,XNTIMED);

    /* The clock interrupt does not need to be attached since the
       timer service will handle the arch-dependent setup. */

    xnintr_init(&nkclock,
		0,	/* Not used since never attached. */
		XNINTR_MAX_PRIORITY, /* Highest interrupt priority */
		NULL,
		nkpod->svctable.tickhandler,
		0);

    xnarch_start_timer(nstick,&xnpod_clock_irq);

    /* When no host ticking service is active for the underlying arch,
       the host timer exists but simply never ticks since
       xntimer_start() is passed a null interval value. CAUTION:
       kernel timers over aperiodic mode can be started by
       xntimer_start() only _after_ the hw timer has been set up
       through xnarch_start_timer(). */

    xntimer_start(&nkpod->htimer,
		  XNARCH_HOST_TICK / nkpod->tickvalue,
		  XNARCH_HOST_TICK / nkpod->tickvalue);
    splexit(s);

    return XN_OK;
}

/*! 
 * \fn void xnpod_stop_timer(void)
 * \brief Stop the periodic timer.
 *
 * Stops the periodic timer previously started by a call to
 * xnpod_start_timer().
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine can be called on behalf of a thread or IST
 * context.
 */

void xnpod_stop_timer (void)

{
    spl_t s;

    if (!nkpod)
	return;

    splhigh(s);

    if (testbits(nkpod->status,XNTIMED))
	{
	if (!testbits(nkpod->status,XNFATAL))
	    xntimer_freeze();

	xntimer_stop(&nkpod->htimer);
	clrbits(nkpod->status,XNTIMED|XNTMPER);
	}

    splexit(s);
}

/*! 
 * \fn void xnpod_announce_tick(xnintr_t *intr,
                                int hits)
 * \brief Announce a new clock tick.
 *
 * This is the default service routine for clock ticks which performs
 * the necessary housekeeping chores for time-related services managed
 * by the nanokernel. In a way or another, this routine must be called
 * to announce each incoming clock tick to the nanokernel.
 *
 * @param intr The descriptor address of the interrupt object
 * underlying the timer interrupt.
 *
 * @param hits The number of pending interrupt hits to process in a
 * row.
 *
 * Side-effect: Since this routine manages the round-robin scheduling,
 * the running thread (which has been preempted by the timer
 * interrupt) can be switched out as a result of its time credit being
 * exhausted. The nanokernel always calls the rescheduling procedure
 * after the outer interrupt has been processed.
 *
 * Context: The caller must have a valid nanokernel context (regular
 * or interrupt service thread).
 */

void xnpod_announce_tick (xnintr_t *intr, int hits)

{
    xnthread_t *usrthread;
    spl_t s;

    splhigh(s);

    xntimer_do_timers(hits); /* Fire the timeouts, if any. */

    /* Do the round-robin processing. */

    usrthread = xnpod_current_sched()->usrthread;

    if (testbits(usrthread->status,XNRRB) &&
	usrthread->rrcredit != XN_INFINITE &&
	!testbits(usrthread->status,XNLOCK) &&
	/* Round-robin in aperiodic mode makes no sense. */
	testbits(nkpod->status,XNTMPER))
	{
	/* The thread can be preempted and undergoes a round-robin
	   scheduling. Round-robin time credit is only consumed by a
	   running thread. Thus, if a more prioritary thread outside
	   the priority group which started the time slicing grabs the
	   processor, the current time credit of the preempted thread
	   is kept unchanged, and will not be reset when this thread
	   resumes execution. */

	if (usrthread->rrcredit <= hits)
	    {
	    /* If the time slice is exhausted for the running thread,
	       put it back on the ready queue (in last position) and
	       reset its credit for the next run. */
	    usrthread->rrcredit = usrthread->rrperiod;
	    xnpod_resume_thread(usrthread,0);
	    }
	else
	    usrthread->rrcredit -= hits;
	}

    splexit(s);
}

/*@}*/

EXPORT_SYMBOL(xnpod_activate_rr);
EXPORT_SYMBOL(xnpod_add_hook);
EXPORT_SYMBOL(xnpod_announce_tick);
EXPORT_SYMBOL(xnpod_check_context);
EXPORT_SYMBOL(xnpod_deactivate_rr);
EXPORT_SYMBOL(xnpod_delete_thread);
EXPORT_SYMBOL(xnpod_fatal_helper);
EXPORT_SYMBOL(xnpod_get_time);
EXPORT_SYMBOL(xnpod_init);
EXPORT_SYMBOL(xnpod_init_thread);
EXPORT_SYMBOL(xnpod_remove_hook);
EXPORT_SYMBOL(xnpod_renice_thread);
EXPORT_SYMBOL(xnpod_restart_thread);
EXPORT_SYMBOL(xnpod_resume_thread);
EXPORT_SYMBOL(xnpod_rotate_readyq);
EXPORT_SYMBOL(xnpod_schedule);
EXPORT_SYMBOL(xnpod_schedule_runnable);
EXPORT_SYMBOL(xnpod_set_thread_mode);
EXPORT_SYMBOL(xnpod_set_time);
EXPORT_SYMBOL(xnpod_shutdown);
EXPORT_SYMBOL(xnpod_start_thread);
EXPORT_SYMBOL(xnpod_start_timer);
EXPORT_SYMBOL(xnpod_stop_timer);
EXPORT_SYMBOL(xnpod_suspend_thread);
EXPORT_SYMBOL(xnpod_trap_fault);
EXPORT_SYMBOL(xnpod_unblock_thread);
EXPORT_SYMBOL(xnpod_welcome_thread);

EXPORT_SYMBOL(nkclock);
EXPORT_SYMBOL(nkgkptd);
EXPORT_SYMBOL(nkpod);
