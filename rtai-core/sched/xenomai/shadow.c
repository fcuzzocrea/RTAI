
/*!\file shadow.c
 * \brief Real-time shadow services.
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
 * \ingroup shadow
 */

/*!
 * \ingroup xenomai
 * \defgroup shadow Real-time shadow services.
 *
 * Real-time shadow services.
 *
 *@{*/

#define XENO_SHADOW_MODULE
#include <asm/signal.h>
#include <linux/unistd.h>
#include "rtai_config.h"
#include "xenomai/pod.h"
#include "xenomai/heap.h"
#include "xenomai/mutex.h"
#include "xenomai/synch.h"
#include "xenomai/module.h"
#include "xenomai/shadow.h"
#if CONFIG_TRACE
#include <linux/trace.h>
#endif

#define XENOMAI_MUX_NR 16

int nkgkptd;

static int traceme = 0;

static adomain_t irq_shield;

static struct task_struct *gatekeeper;

static struct semaphore gksync;

static struct semaphore gkreq;

static int gkstop;

static unsigned gkvirq;

static unsigned sigvirq;

static unsigned nicevirq;

static int gk_enter_in,
           gk_enter_out;

static struct {

    xnthread_t *thread;
    xnmutex_t *mutex;

} gk_enter_wheel[XNSHADOW_MAXRQ];

static int gk_leave_in,
           gk_leave_out;

static struct task_struct *gk_leave_wheel[XNSHADOW_MAXRQ];

static int gk_signal_in,
           gk_signal_out;

static struct {

    struct task_struct *task;
    int sig;

} gk_signal_wheel[XNSHADOW_MAXRQ];

static int gk_renice_in,
           gk_renice_out;

static struct {

    struct task_struct *task;
    int prio;

} gk_renice_wheel[XNSHADOW_MAXRQ];

static struct {

    unsigned magic;
    int nrcalls;
    int refcnt;
    xnsysent_t *systab;

} muxtable[XENOMAI_MUX_NR];

static inline struct task_struct *get_calling_task (adevinfo_t *evinfo) {

    return evinfo->domid == adp_current->domid ? current : rtai_get_root_current(0);
}

static inline void set_linux_task_priority (struct task_struct *task, int prio)

{
    spl_t s;

    /* Can be called on behalf of RTAI -- The priority of the
       real-time shadow will be used to determine the Linux
       task priority. */

    if (adp_current == adp_root)
	{
	rtai_set_linux_task_priority(task,SCHED_FIFO,prio);
	return;
	}

    splhigh(s);
    gk_renice_wheel[gk_renice_in].task = task;
    gk_renice_wheel[gk_renice_in].prio = prio;
    gk_renice_in = (gk_renice_in + 1) & (XNSHADOW_MAXRQ - 1);
    splexit(s);

    adeos_propagate_irq(nicevirq);
}

static void engage_irq_shield (void)

{
    adeos_declare_cpuid;
    unsigned long flags;
    unsigned irq;

    /* Since the interrupt shield does not handle the virtual IRQs we
       use for sending commands to Linux (e.g. wakeups, renice
       requests etc.), those will be marked pending in Linux's IRQ log
       when scheduled and get played normally, without any explicit
       propagation needed from the shielding domain, even if the
       latter is stalled. */

    adeos_stall_pipeline_from(&irq_shield);

    adeos_lock_cpu(flags);

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	__adeos_lock_irq(adp_root,cpuid,irq);

    adeos_unlock_cpu(flags);
}

static void disengage_irq_shield (void)

{
    unsigned long flags;
    unsigned irq;

    rtai_hw_lock(flags);

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	__adeos_unlock_irq(adp_root,irq);

    rtai_hw_unlock(flags);

    adeos_unstall_pipeline_from(&irq_shield);
}

static void xnshadow_renice_handler (unsigned virq)

{
    splnone();	/* Unstall the root stage first since the renice
		   operation might sleep.  */
    while (gk_renice_out != gk_renice_in)
	{
	struct task_struct *task = gk_renice_wheel[gk_renice_out].task;
	int prio = gk_renice_wheel[gk_renice_out].prio;
	gk_renice_out = (gk_renice_out + 1) & (XNSHADOW_MAXRQ - 1);
	rtai_set_linux_task_priority(task,SCHED_FIFO,prio);
	}
}

static void xnshadow_wakeup_handler (unsigned virq)

{
    int shield_on = 0;

    while (gk_leave_out != gk_leave_in)
	{
	struct task_struct *task = gk_leave_wheel[gk_leave_out];
	gk_leave_out = (gk_leave_out + 1) & (XNSHADOW_MAXRQ - 1);

	if (xnshadow_thread(task) && !shield_on)
	    {
	    engage_irq_shield();
	    shield_on = 1;
	    }

	wake_up_process(task);
	}
}

static void xnshadow_signal_handler (unsigned virq)

{
    while (gk_signal_out != gk_signal_in)
	{
	struct task_struct *task = gk_signal_wheel[gk_signal_out].task;
	int sig = gk_signal_wheel[gk_signal_out].sig;
	gk_signal_out = (gk_signal_out + 1) & (XNSHADOW_MAXRQ - 1);
	send_sig(sig,task,1);
	}
}

static inline void xnshadow_sched_wakeup (struct task_struct *task)

{
    spl_t s;

    splhigh(s);
    gk_leave_wheel[gk_leave_in] = task;
    gk_leave_in = (gk_leave_in + 1) & (XNSHADOW_MAXRQ - 1);
    splexit(s);

    /* Do _not_ use adeos_propagate_irq() here since we might need to
       schedule a wakeup on behalf of the Linux domain. */

    adeos_schedule_irq(gkvirq);

#if CONFIG_TRACE
    TRACE_PROCESS(TRACE_EV_PROCESS_SIGNAL, -111, task->pid);
#endif
}

static inline void xnshadow_sched_signal (struct task_struct *task, int sig)

{
    spl_t s;

    splhigh(s);
    gk_signal_wheel[gk_signal_in].task = task;
    gk_signal_wheel[gk_signal_in].sig = sig;
    gk_signal_in = (gk_signal_in + 1) & (XNSHADOW_MAXRQ - 1);
    splexit(s);

    adeos_propagate_irq(sigvirq);
}

static void xnshadow_itimer_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;
    xnshadow_sched_signal(xnthread_archtcb(thread)->user_task,SIGALRM);
}

static void gatekeeper_thread (void)

{
    atomic_counter_t imutexval;

    gatekeeper = current;

    self_daemonize("gatekeeper");

    sigfillset(&current->blocked);

    up(&gksync);

    for (;;)
	{
	set_linux_task_priority(current,1);

	down_interruptible(&gkreq);

	if (gkstop)
	    break;

	while (gk_enter_out != gk_enter_in)
	    {
	    xnthread_t *thread = gk_enter_wheel[gk_enter_out].thread;
	    xnmutex_t *imutex = gk_enter_wheel[gk_enter_out].mutex;
	    gk_enter_out = (gk_enter_out + 1) & (XNSHADOW_MAXRQ - 1);
#if 1
	    if (traceme)
		printk("__GK__ %s (ipipe=%lu)\n",
		       thread->name,
		       adeos_test_pipeline_from(&rtai_domain));
#endif
	    xnpod_resume_thread(thread,XNRELAX);

	    if (imutex)
		xnmutex_clear_lock(imutex,&imutexval);
	    }

	xnpod_renice_root(XNPOD_ROOT_PRIO_BASE);

	/* Reschedule on behalf of the RTAI domain reflecting all
	   changes in a row. */
	xnshadow_schedule();
	}

    up(&gksync);
}

/* timespec/timeval <-> ticks conversion routines -- Lifted and
  adapted from include/linux/time.h. */

unsigned long long xnshadow_ts2ticks (const struct timespec *v)

{
    u_long hz = (u_long)xnpod_get_ticks2sec();
    unsigned long long nsec = v->tv_nsec;
    u_long sec = v->tv_sec;

    nsec += xnarch_ulldiv(1000000000LL,hz,NULL) - 1;
    nsec = xnarch_ulldiv(nsec,1000000000L / hz,NULL);

    return hz * sec + nsec;
}

void xnshadow_ticks2ts (unsigned long long ticks, struct timespec *v)

{
    u_long hz = (u_long)xnpod_get_ticks2sec(), mod;
    v->tv_nsec = xnarch_ullmod(ticks,hz,&mod) * (1000000000L / hz);
    v->tv_sec = xnarch_ulldiv(ticks,hz,NULL);
}

unsigned long long xnshadow_tv2ticks (const struct timeval *v)

{
    unsigned long long nsec = v->tv_usec * 1000LL;
    u_long hz = (u_long)xnpod_get_ticks2sec();
    u_long sec = v->tv_sec;

    nsec += xnarch_ulldiv(1000000000LL,hz,NULL) - 1;
    nsec = xnarch_ulldiv(nsec,1000000000L / hz,NULL);

    return hz * sec + nsec;
}

void xnshadow_ticks2tv (unsigned long long ticks, struct timeval *v)

{
    u_long hz = (u_long)xnpod_get_ticks2sec(), mod;
    v->tv_usec = xnarch_ullmod(ticks,hz,&mod) * (1000000L / hz);
    v->tv_sec = xnarch_ulldiv(ticks,hz,NULL);
}

/*! 
 * \fn void xnshadow_harden(xnmutex_t *imutex);
 * \brief Migrate a Linux task to the RTAI domain -- INTERNAL.
 *
 * This service causes the transition of "current" from the Linux
 * domain to RTAI. This is obtained by asking the gatekeeper to resume
 * the shadow mated with "current" then triggering the rescheduling
 * procedure in the RTAI domain. The shadow will resume in the RTAI
 * domain as returning from schedule().
 *
 * @param imutex The address of an interface mutex currently held by
 * the caller which will be subject to a lock-breaking preemption
 * before the rescheduling takes place in the __xn_sys_sched
 * service. The ability to pass a mutex through this service is
 * indirectly used by skins when creating a new shadow thread (see
 * xnshadow_map()), thus preventing any deletion while the thread
 * descriptor is accessed by the internal bootstrap code. Passing NULL
 * when no lock-breaking preemption is required is valid. See
 * xnpod_schedule() for more on lock-breaking preemption points.
 *
 * Side-effect: This routine indirectly triggers the rescheduling
 * procedure (see __xn_sys_sched service).
 *
 * Context: This routine must be called on behalf of a user-space task
 * from the Linux domain.
 */

void xnshadow_harden (xnmutex_t *imutex)

{
    spl_t s;

#if 1
    if (traceme)
	printk("_!HARDENING!_ %s, status 0x%lx, pid=%d (ipipe=%lu, domain=%s)\n",
	       xnshadow_thread(current)->name,
	       xnshadow_thread(current)->status,
	       current->pid,
	       adeos_test_pipeline_from(&rtai_domain),
	       adp_current->name);
#endif
    /* Enqueue the request to move "current" from the Linux domain to
       the RTAI domain. This will cause the shadow thread to resume
       using the register state of the Linux task. */

    splhigh(s);

    gk_enter_wheel[gk_enter_in].thread = xnshadow_thread(current);
    gk_enter_wheel[gk_enter_in].mutex = imutex;
    gk_enter_in = (gk_enter_in + 1) & (XNSHADOW_MAXRQ - 1);

    engage_irq_shield();

    splexit(s);

    if (xnshadow_thread(current)->cprio > gatekeeper->rt_priority)
	set_linux_task_priority(gatekeeper,xnshadow_thread(current)->cprio);

    set_current_state(TASK_INTERRUPTIBLE);

    /* Wake up the gatekeeper which will perform the transition. */
    up(&gkreq);

    schedule();

#ifdef CONFIG_RTAI_FPU_SUPPORT
    xnpod_switch_fpu();
#endif /* CONFIG_RTAI_FPU_SUPPORT */

    splnone();

    /* "current" is now running into the RTAI domain. */

#if 1
    if (traceme)
	printk("__RT__ %s, pid=%d (ipipe=%lu)\n",
	       xnpod_current_thread()->name,
	       current->pid,
	       adeos_test_pipeline_from(&rtai_domain));
#endif
}

/*! 
 * \fn void xnshadow_relax(void);
 * \brief Switch a shadow thread back to the Linux domain -- INTERNAL.
 *
 * This service yields the control of the running shadow back to
 * Linux. This is obtained by suspending the shadow and scheduling a
 * wake up call for the mated user task inside the Linux domain. The
 * Linux task will resume on return from xnpod_suspend_thread() on
 * behalf of the root thread.
 *
 * Side-effect: This routine indirectly calls the rescheduling
 * procedure.
 *
 * Context: This routine must be called on behalf of a real-time
 * shadow inside the RTAI domain.
 */

void xnshadow_relax (void)

{
    xnthread_t *thread = xnpod_current_thread();
    spl_t s;

    /* Enqueue the request to move the running shadow from the RTAI
       domain to the Linux domain.  This will cause the Linux task
       to resume using the register state of the shadow thread. */

#if 1
    if (traceme)
	printk("_!RELAXING!_ %s, status 0x%lx, pid=%d (ipipe=%lu, domain=%s)\n",
	       thread->name,
	       thread->status,
	       xnthread_archtcb(thread)->user_task->pid,
	       adeos_test_pipeline_from(&rtai_domain),
	       adp_current->name);
#endif

    xnshadow_sched_wakeup(xnthread_archtcb(thread)->user_task);
    xnpod_renice_root(thread->cprio);
    splhigh(s);
    xnpod_suspend_thread(thread,XNRELAX,XN_INFINITE,NULL,NULL);
    __adeos_schedule_back_root(current);
    splexit(s);

    /* "current" is now running into the Linux domain on behalf of the
       root thread. */
#if 1
    if (traceme)
	printk("__RELAX__ %s (on %s, status 0x%lx), pid=%d (ipipe=%lu, domain=%s)\n",
	       thread->name,
	       xnpod_current_sched()->runthread->name,
	       xnpod_current_sched()->runthread->status,
	       xnthread_archtcb(thread)->user_task->pid,
	       adeos_test_pipeline_from(&rtai_domain),
	       adp_current->name);
#endif
}

void xnshadow_unmap (xnthread_t *thread) /* Must be called by the task deletion hook. */

{
    struct task_struct *task = xnthread_archtcb(thread)->user_task;

    if (!task)
	return;

#if 1
    if (traceme)
	printk("__UNMAP__: %s, pid=%d, task=%s (ipipe=%lu, domain=%s, taskstate=%ld)\n",
	       thread->name,
	       task->pid,
	       task->comm,
	       adeos_test_pipeline_from(&rtai_domain),
	       adp_current->name,
	       task->state);
#endif

    xnshadow_ptd(task) = NULL;

    /* The zombie side returning to user-space will be trapped and
       exited inside the pod's rescheduling routines. */
    xnshadow_sched_wakeup(task);
}

static void xnshadow_sync_post (pid_t syncpid, int *u_syncp, int err)

{
    struct task_struct *synctask;

    /* FIXME: this won't be SMP safe. */

    read_lock(&tasklist_lock);
    synctask = find_task_by_pid(syncpid);
    read_unlock(&tasklist_lock);

    if (synctask)
	{
	__xn_put_user(synctask,err ?: 0x7fffffff,u_syncp); /* Poor man's semaphore V. */
	wake_up_process(synctask);
	}
}

static int xnshadow_sync_wait (int *u_syncp)

{
    int s, syncflag;

    for (;;)	/* Poor man's semaphore P. */
	{
	splhigh(s);

	__xn_get_user(current,syncflag,u_syncp);

	if (syncflag)
	    break;

	set_current_state(TASK_INTERRUPTIBLE);

	splexit(s);

	schedule();
	}

    splexit(s);

    return syncflag == 0x7fffffff ? 0 : syncflag;
}

void xnshadow_exit (void)

{
    __adeos_schedule_back_root(current);
    do_exit(0);
}

static void xnshadow_asr (xnsigmask_t sigs)

{
    if (sigs & XNTHREAD_SHADOW_SIGKILL)
	xnpod_delete_self(NULL);
}

/*! 
 * \fn xnthread_t *xnshadow_map(xnthread_t *thread,
                                const char *name,
                                int prio,
				    pid_t syncpid,
				    int *u_syncp,
				    unsigned magic,
				    xnmutex_t *imutex);
 * \brief Create a shadow thread context -- INTERNAL.
 *
 * @param thread The descriptor address of the new shadow thread to be
 * mapped to "current".
 *
 * @param name An ASCII string standing for the symbolic name of the
 * shadow thread.
 *
 * @param prio The base priority of the new thread. This value must
 * range from [minpri .. maxpri] (inclusive) as specified when calling
 * the xnpod_init() service from the skin initialization routine.
 *
 * @param syncpid If non-zero, this must be the pid of a Linux task to
 * wake up when the shadow has been initialized. In this case, u_syncp
 * must be valid to, and the new shadow thread is left in a dormant
 * state (XNDORMANT) after its creation, leading to the suspension of
 * "current" in the RTAI domain. Otherwise, the shadow thread is
 * immediately started and "current" exits from this service without
 * being suspended.
 *
 * @param u_syncp If non-zero, this must be a pointer to an integer
 * variable into the caller's address space in user-space which will
 * be used as a semaphore. This semaphore will be posted to wakeup the
 * task identified by pid before "current" is suspended in dormant
 * state by this service. The awaken Linux task is expected to invoke
 * a syscall hat ends up calling xnshadow_start() to finally start the
 * newly created shadow. Passing a null pointer here has the same
 * effect as passing a zero pid argument, and there will be no attempt
 * to wake up any task.
 *
 * @param magic A magic value aimed at identifying unambiguously the
 * skin to which the shadow belongs to. This value must be non-zero.
 *
 * @param imutex The address of an interface mutex currently held by
 * the caller which will be subject to a lock-breaking preemption
 * before the rescheduling takes place in the __xn_sys_sched
 * service. Passing NULL when no lock-breaking preemption is required
 * is valid. See xnpod_schedule() for more on lock-breaking preemption
 * points.
 *
 * Side-effect: This routine indirectly calls the rescheduling
 * procedure.
 *
 * Context: This routine must be called on behalf of the Linux
 * user-space task which is being shadowed.
 */

void xnshadow_map (xnthread_t *thread,
		   const char *name,
		   int prio,
		   pid_t syncpid,
		   int *u_syncp, /* user-space pointer */
		   unsigned magic,
		   xnmutex_t *imutex)
{
    int autostart = !(syncpid && u_syncp);
    spl_t s;

    current->cap_effective |= CAP_TO_MASK(CAP_IPC_LOCK)|CAP_TO_MASK(CAP_SYS_RAWIO)|CAP_TO_MASK(CAP_SYS_NICE);

    xnthread_init(thread,
		  name,
		  prio,
		  XNSHADOW
#ifdef CONFIG_RTAI_FPU_SUPPORT
		  |XNFPU
#endif /* CONFIG_RTAI_FPU_SUPPORT */
		  ,
		  0,
		  NULL,
		  magic);

    thread->asr = &xnshadow_asr;
    thread->asrmode = 0;
    thread->asrimask = 0;

    xnarch_init_shadow_tcb(xnthread_archtcb(thread),thread,name);

    splhigh(s);
    appendq(&nkpod->threadq,&thread->glink);
    xnpod_suspend_thread(thread,
			 autostart ? XNRELAX : (XNRELAX|XNDORMANT),
			 XN_INFINITE,
			 NULL,
			 NULL);
    splexit(s);

    xnshadow_ptd(current) = thread;

    set_linux_task_priority(current,prio);

    if (!autostart)
	/* Wake up the initiating Linux task. */
	xnshadow_sync_post(syncpid,u_syncp,0);
    
#if 1
    if (traceme)
	printk("__MAP__ %s from %s, prio=%d, pid=%d, domain=%s\n",
	       name ? name : "<anonymous>",
	       xnpod_current_sched()->runthread->name,
	       xnthread_base_priority(thread),
	       xnthread_archtcb(thread)->user_task->pid,
	       adp_current->name);
#endif

    /* If not autostarting, the shadow will be left suspended in
       dormant state. */
    xnshadow_harden(imutex);

    if (autostart)
	/* We are immediately joining the RTAI realm on behalf of the
	   current Linux task. */
	xnshadow_start(thread,0,NULL,NULL,1);
    else if (xnshadow_ptd(current) == NULL)
	    /* Woops, this shadow was unmapped while in dormant state
	       (i.e. before xnshadow_start() has been called on
	       it). Ask Linux to reap it. */
	xnshadow_exit();
}

void xnshadow_start (xnthread_t *thread,
		     u_long mode,
		     void (*u_entry)(void *cookie),
		     void *u_cookie,
		     int resched)
{
    spl_t s;

    splhigh(s);

    setbits(thread->status,(mode & (XNTHREAD_MODE_BITS|XNSUSP))|XNSTARTED);
    thread->imask = 0;
    thread->imode = (mode & XNTHREAD_MODE_BITS);
    thread->entry = u_entry;	/* user-space pointer -- do not deref. */
    thread->cookie = u_cookie;	/* ditto. */
    thread->stime = xnarch_get_cpu_time();

    if (testbits(thread->status,XNRRB))
	thread->rrcredit = thread->rrperiod;

    xntimer_init(&thread->atimer,&xnshadow_itimer_handler,thread);

    xnpod_resume_thread(thread,XNDORMANT);

#if 1
    if (traceme)
	printk("__START__ %s (status=0x%lx), prio=%d, pid=%d, domain=%s (sched? %d)\n",
	       thread->name,
	       thread->status,
	       xnthread_base_priority(thread),
	       xnthread_archtcb(thread)->user_task->pid,
	       adp_current->name,
	       resched);
#endif

    splexit(s);

    if (resched)
	/* Reschedule on behalf of the RTAI domain. */
	xnshadow_schedule();
}

void xnshadow_renice (xnthread_t *thread)

{
    struct task_struct *task = xnthread_archtcb(thread)->user_task;
    spl_t s;

    splhigh(s);

    set_linux_task_priority(task,thread->cprio);

    if (xnpod_root_p() &&
	testbits(thread->status,XNRELAX) &&
	xnpod_priocompare(thread->cprio,xnpod_current_root()->cprio) > 0)
	xnpod_renice_root(thread->cprio);

    splexit(s);
}

static int xnshadow_attach_skin (struct task_struct *curr,
				 unsigned magic,
				 u_long infarg)
{
    xnsysinfo_t info;
    int muxid;
    spl_t s;

    if (infarg)
	{
	info.cpufreq = xnarch_get_cpu_freq();
	info.tickval = xnpod_get_tickval();
	__xn_copy_to_user(curr,(void *)infarg,&info,sizeof(info));
	}

    splhigh(s);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	{
	if (muxtable[muxid].magic == magic)
	    {
	    muxtable[muxid].refcnt++;
	    splexit(s);
	    return ++muxid;
	    }
	}

    splexit(s);

    return -ENOENT;
}

static int xnshadow_detach_skin (struct task_struct *curr, int muxid)

{
    xnholder_t *holder, *nholder;
    xnthread_t *thread;
    spl_t s;

    if (--muxid < 0 || muxid >= XENOMAI_MUX_NR)
	return -EINVAL;

    splhigh(s);

    muxtable[muxid].refcnt--;

    /* Find all active shadow threads belonging to the detached skin
       and delete them. Sidenote: there can only be one active primary
       interface (i.e. skin) declaring a real-time pod at a time, but
       additionally, there might be native nucleus threads
       (e.g. debugger) and/or threads belonging to secondary/helper
       interfaces which do not declare any pod, so we need to check
       their magic before attempting to delete them. */

    nholder = getheadq(&nkpod->threadq);

    while ((holder = nholder) != NULL)
	{
	nholder = nextq(&nkpod->threadq,holder);
	thread = link2thread(holder,glink);

	if (xnthread_magic(thread) == muxtable[muxid].magic &&
	    testbits(thread->status,XNSHADOW))
	    xnpod_delete_thread(thread,NULL);
	}

    splexit(s);

    return 0;
}

static int xnshadow_substitute_syscall (struct task_struct *curr,
					struct pt_regs *regs,
					int migrate)
{
    xnthread_t *thread = xnshadow_thread(curr);

    switch (__xn_reg_mux(regs))
	{
	case __NR_nanosleep:
	    
	    {
	    xnticks_t now, expire, delay;
	    struct timespec t;

	    if (!__xn_access_ok(curr,VERIFY_READ,(void *)__xn_reg_arg1(regs),sizeof(t)))
		{
		__xn_reg_rval(regs) = -EFAULT;
		return 1;
		}

	    __xn_copy_from_user(curr,&t,(void *)__xn_reg_arg1(regs),sizeof(t));

	    if (t.tv_nsec >= 1000000000L || t.tv_nsec < 0 || t.tv_sec < 0)
		{
		__xn_reg_rval(regs) = -EINVAL;
		return 1;
		}

	    if (migrate) /* Shall we migrate to RTAI first? */
		xnshadow_harden(NULL);

	    if (testbits(nkpod->status,XNTMPER))
		expire = nkpod->jiffies;
	    else
		expire = xnpod_get_cpu_time();

	    delay = xnshadow_ts2ticks(&t);
	    expire += delay;

	    if (delay > 0)
		xnpod_delay(delay);

	    if (testbits(nkpod->status,XNTMPER))
		now = nkpod->jiffies;
	    else
		now = xnpod_get_cpu_time();

	    if (now >= expire)
		__xn_reg_rval(regs) = 0;
	    else
		{
		if (__xn_reg_arg2(regs))
		    {
		    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg2(regs),sizeof(t)))
			{
			__xn_reg_rval(regs) = -EFAULT;
			return 1;
			}

		    xnshadow_ticks2ts(now - expire,&t);
		    __xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&t,sizeof(t));
		    }

		__xn_reg_rval(regs) = -1;
		}

	    return 1;
	    }

	case __NR_setitimer:

	    {
	    xnticks_t delay, interval, expire;
	    struct itimerval itv;

	    if (__xn_reg_arg1(regs) != ITIMER_REAL)
		return 0;

	    if (__xn_reg_arg2(regs))
		{
		if (!__xn_access_ok(curr,VERIFY_READ,(void *)__xn_reg_arg2(regs),sizeof(itv)))
		    {
		    __xn_reg_rval(regs) = -EFAULT;
		    return 1;
		    }

		__xn_copy_from_user(curr,&itv,(void *)__xn_reg_arg2(regs),sizeof(itv));
		}
	    else
		memset(&itv,0,sizeof(itv));

	    xntimer_stop(&thread->atimer);

	    delay = xnshadow_tv2ticks(&itv.it_value);
	    interval = xnshadow_tv2ticks(&itv.it_interval);

	    if (testbits(nkpod->status,XNTMPER))
		expire = nkpod->jiffies;
	    else
		expire = xnpod_get_cpu_time();

	    expire += delay;

	    if (delay > 0)
		xntimer_start(&thread->atimer,delay,interval);

	    if (__xn_reg_arg3(regs))
		{
		if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg3(regs),sizeof(itv)))
		    {
		    __xn_reg_rval(regs) = -EFAULT;
		    return 1;
		    }

		interval = xntimer_interval(&thread->atimer);

		if (xntimer_active_p(&thread->atimer))
		    {
		    delay = xntimer_get_timeout(&thread->atimer);

		    if (delay == 0)
			delay = 1;
		    }
		else
		    delay = 0;

		xnshadow_ticks2tv(delay,&itv.it_value);
		xnshadow_ticks2tv(interval,&itv.it_interval);
		__xn_copy_to_user(curr,(void *)__xn_reg_arg3(regs),&itv,sizeof(itv));
		}

	    __xn_reg_rval(regs) = 0;

	    return 1;
	    }

	case __NR_getitimer:

	    {
	    xnticks_t delay, interval;
	    struct itimerval itv;

	    if (__xn_reg_arg1(regs) != ITIMER_REAL)
		return 0;

	    if (!__xn_reg_arg2(regs) ||
		!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg2(regs),sizeof(itv)))
		{
		__xn_reg_rval(regs) = -EFAULT;
		return 1;
		}

	    interval = xntimer_interval(&thread->atimer);

	    if (xntimer_active_p(&thread->atimer))
		{
		delay = xntimer_get_timeout(&thread->atimer);

		if (delay == 0) /* Cannot be negative in this context. */
		    delay = 1;
		}
	    else
		delay = 0;

	    xnshadow_ticks2tv(delay,&itv.it_value);
	    xnshadow_ticks2tv(interval,&itv.it_interval);
	    __xn_copy_to_user(curr,(void *)__xn_reg_arg3(regs),&itv,sizeof(itv));
	    __xn_reg_rval(regs) = 0;

	    return 1;
	    }

	default:

	    /* No real-time replacement -- let Linux handle this call. */
	    return 0;
	}
}

static void xnshadow_realtime_sysentry (adevinfo_t *evinfo)

{
    struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
    struct task_struct *task;
    xnthread_t *thread;
    int muxid, muxop;

    if (!nkpod)
	{
	if (__xn_reg_mux_p(regs))
	    {
	    if (__xn_reg_mux(regs) != __xn_mux_code(0,__xn_sys_attach))
		printk("Xenomai: bad syscall %ld/%ld -- no skin loaded\n",
		       __xn_mux_id(regs),
		       __xn_mux_op(regs));

	    __xn_reg_rval(regs) = -ENOSYS;
	    }
	else
	    /* Regular Linux syscall with no skin loaded -- propagate
	       it to the Linux kernel. */
	    adeos_propagate_event(evinfo);

	return;
	}

    task = get_calling_task(evinfo);
    thread = xnshadow_thread(task);

    if (!__xn_reg_mux_p(regs))
	{
	if (xnpod_root_p())
	    {
	    /* The call originates from the Linux domain, just
	       propagate the event so that we will fall back to
	       xnshadow_linux_sysentry(). */
	    adeos_propagate_event(evinfo);
	    return;
	    }
#if 1
	if (traceme)
	    printk("__SHADOW__ %s (sched=%s, linux=%s), call=%ld, pid=%d, ilock=%ld, task %p, origdomain 0x%x\n",
		   xnpod_current_thread()->name,
		   thread->name,
		   task->comm,
		   __xn_reg_mux(regs),
		   task->pid,
		   adeos_test_pipeline_from(&irq_shield),
		   task,
		   evinfo->domid);
#endif

	if (!testbits(thread->status,XNRELAX) &&
	    xnshadow_substitute_syscall(task,regs,0))
	    /* This is a Linux syscall issued on behalf of a shadow
	       thread running inside the RTAI domain. This call has
	       just been intercepted by Xenomai and a RTAI replacement
	       has been substituted for it. */
	    return;

	/* This syscall has not been substituted, let Linux handle
	   it. This will eventually fall back to the Linux syscall
	   handler if our Linux domain handler does not intercept
	   it. Before we let it go, ensure that our running thread has
	   properly entered the Linux domain. */

	adeos_propagate_event(evinfo);

#if 1
	if (traceme)
	    printk("__SYSIN__ %s, call=%ld, pid=%d\n",
		   xnpod_current_thread()->name,
		   __xn_reg_mux(regs),
		   task->pid);
#endif

	if (!testbits(thread->status,XNRELAX))
	    {
	    if (testbits(thread->status,XNAUTOSW))
		setbits(thread->status,XNSYSSW);

	    xnshadow_relax();
	    }

	return;
	}

    muxid = __xn_mux_id(regs);
    muxop = __xn_mux_op(regs);

#if 1
    if (traceme)
	printk("REQ {skin=%d, op=%d} on behalf of thread %s, pid=%d in domain %s\n",
	       muxid,
	       muxop,
	       xnpod_current_thread()->name,
	       task->pid,
	       adp_current->name);
#endif

    if (muxid == 0)
	{
	/* Internal nucleus call for shadow support. */

	switch (muxop)
	    {
	    case __xn_sys_sched:

		xnpod_schedule(NULL);
		return;

	    case __xn_sys_attach:

		__xn_reg_rval(regs) = xnshadow_attach_skin(task,
							   __xn_reg_arg1(regs),
							   __xn_reg_arg2(regs));
		return;

	    case __xn_sys_detach:

		__xn_reg_rval(regs) = xnshadow_detach_skin(task,
							   __xn_reg_arg1(regs));
		return;

	    case __xn_sys_sync:	/* Do this from the Linux domain. */

		adeos_propagate_event(evinfo);
		return;

	    case __xn_sys_migrate:

		if (!thread)	/* Not a shadow anyway. */
		    __xn_reg_rval(regs) = 0;
		else if (__xn_reg_arg1(regs)) /* Linux => RTAI */
		    {
		    if (!xnthread_test_flags(thread,XNRELAX))
			__xn_reg_rval(regs) = 0;
		    else
			adeos_propagate_event(evinfo);
		    }
		else	/* RTAI => Linux */
		    {
		    if (xnthread_test_flags(thread,XNRELAX))
			__xn_reg_rval(regs) = 0;
		    else
			{
			__xn_reg_rval(regs) = 1;
			xnshadow_relax();
			}
		    }

		return;

#if CONFIG_TRACE
	    case 20:

		TRACE_PROCESS(TRACE_EV_PROCESS_SIGNAL, -888, adp_root->cpudata[0].irq_pending_lo[0]);
		__xn_reg_rval(regs) = 0;
		return;

	    case 21:

		TRACE_PROCESS(TRACE_EV_PROCESS_SIGNAL, -999, 0);
		__xn_reg_rval(regs) = 0;
		return;
#endif

	    default:

		__xn_reg_rval(regs) = -ENOSYS;
		return;
	    }
	}

    /* Skin call: check validity. */

    if (muxid < 0 || muxid > XENOMAI_MUX_NR ||
	muxop < 0 || muxop >= muxtable[muxid - 1].nrcalls ||
	((muxtable[muxid - 1].systab[muxop].flags & __xn_flag_anycontext) == 0 &&
	 xnshadow_thread(task) == NULL))
	{
	__xn_reg_rval(regs) = -ENOSYS;
	return;
	}

    if ((muxtable[muxid - 1].systab[muxop].flags & __xn_flag_suspensive) != 0 &&
	evinfo->domid != RTAI_DOMAIN_ID)
	/* This one must be handled in the Linux domain. */
	adeos_propagate_event(evinfo);
    else
	__xn_reg_rval(regs) = muxtable[muxid - 1].systab[muxop].svc(task,regs);
}

static void xnshadow_linux_sysentry (adevinfo_t *evinfo)

{
    struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
    xnthread_t *thread = xnshadow_thread(current);
    int muxid, muxop;

    if (__xn_reg_mux_p(regs))
	{
	/* muxid and muxop have already been checked in the RTAI
	   domain handler. */

	muxid = __xn_mux_id(regs);
	muxop = __xn_mux_op(regs);
#if 1
	if (traceme)
	    printk("REQ {skin=%d, op=%d} on behalf of thread %s, pid=%d in domain %s\n",
		   muxid,
		   muxop,
		   xnpod_current_thread()->name,
		   current->pid,
		   adp_current->name);
#endif

	if (muxid == 0)
	    {
	    /* These are special built-in services which must be run
	       on behalf of the Linux domain. */

	    switch (muxop)
		{
		case __xn_sys_sync:

		    __xn_reg_rval(regs) = xnshadow_sync_wait((int *)__xn_reg_arg1(regs));
		    break;

		case __xn_sys_migrate:

		    __xn_reg_rval(regs) = 1;
		    xnshadow_harden(NULL);
		    return;
		}
	    }
	else
	    __xn_reg_rval(regs) = muxtable[muxid - 1].systab[muxop].svc(current,regs);

	if (thread && testbits(thread->status,XNAUTOSW|XNRELAX) == (XNAUTOSW|XNRELAX))
	    {
	    xnshadow_harden(NULL);
	    return;
	    }
	}
    else
	{
	if (thread && xnshadow_substitute_syscall(current,regs,1))
	    /* This is a Linux syscall issued on behalf of a shadow
	       thread running inside the Linux domain. If the call has
	       been substituted with a RTAI replacement, do not let
	       Linux know about it. */
	    return;

	/* Fall back to Linux syscall handling. */
	adeos_propagate_event(evinfo);
	}
}

static void xnshadow_linux_sysexit (adevinfo_t *evinfo)

{
    xnthread_t *thread = xnshadow_thread(current);

    if (thread && !signal_pending(current))
	{
#if 1
	if (traceme)
	    printk("__SYSOUT__ %s, pid=%d, autoswitch=%d\n",
		   xnpod_current_thread()->name,
		   current->pid,
		   testbits(thread->status,XNAUTOSW|XNSYSSW) == (XNAUTOSW|XNSYSSW));
#endif
	if (testbits(thread->status,XNAUTOSW|XNSYSSW) == (XNAUTOSW|XNSYSSW))
	    {
	    clrbits(thread->status,XNSYSSW);
	    xnshadow_harden(NULL);
	    return;
	    }
	}

    adeos_propagate_event(evinfo);
}

static void xnshadow_linux_taskexit (adevinfo_t *evinfo)

{
    xnthread_t *thread = xnshadow_thread(current);

    if (thread)
	{
#if 1
	if (traceme)
	    printk("LINUX EXIT on behalf of thread %s, pid=%d, relaxed? %d\n",
		   thread->name,
		   current->pid,
		   !!testbits(thread->status,XNRELAX));
#endif

	if (xnpod_shadow_p())
	    xnshadow_relax();

	/* So that we won't attempt to further wakeup the exiting task
	   in xnshadow_unmap(). */

	xnthread_archtcb(thread)->user_task = NULL;
    	xnshadow_ptd(current) = NULL;

	xnpod_delete_thread(thread,NULL);

#if 1
	if (traceme)
	    printk("Cleaned up %s\n",thread->name);
#endif
	}

    adeos_propagate_event(evinfo);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
static struct mmreq {
    int in, out, count;
#define MAX_MM 32  /* Should be more than enough (must be a power of 2). */
#define bump_mmreq(x) do { x = (x + 1) & (MAX_MM - 1); } while(0)
    struct mm_struct *mm[MAX_MM];
} mmrqtab[NR_CPUS];
#endif  /* KERNEL_VERSION < 2.6.0 */

static void xnshadow_schedule_head (adevinfo_t *evinfo)

{
    struct { struct task_struct *prev, *next; } *evdata = (__typeof(evdata))evinfo->evdata;
    struct task_struct *next = evdata->next;
    int rootprio;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    struct task_struct *prev = evdata->prev;

    /* The SCHEDULE_HEAD event is sent by the (Adeosized) Linux kernel
       each time it's about to switch a process out. This hook is
       aimed at preventing the last active MM from being dropped
       during the real-time operations since it is costly.  See
       kernel/sched.c (schedule()) for more. The MM dropping is simply
       postponed until the SCHEDULE_TAIL event is received, right
       after the incoming task has been switched in. */

    if (!prev->mm)
	{
	struct mmreq *p = mmrqtab + task_cpu(prev);
	struct mm_struct *oldmm = prev->active_mm;
	BUG_ON(p->count >= MAX_MM);
	/* Prevent the MM from being dropped in schedule(), then pend
	   a request to drop it later in xnshadow_schedule_tail(). */
	atomic_inc(&oldmm->mm_count);
	p->mm[p->in] = oldmm;
	bump_mmreq(p->in);
	p->count++;
	}
#endif  /* KERNEL_VERSION < 2.6.0 */

    adeos_propagate_event(evinfo);

    if (!nkpod)
	return;

    if (xnshadow_thread(next))
	{
	rootprio = xnshadow_thread(next)->cprio;
	engage_irq_shield();
	}
    else if (next != gatekeeper)
	{
	rootprio = XNPOD_ROOT_PRIO_BASE;
	disengage_irq_shield();
	}
    else
	return;

    /* Current Xenomai thread must be the root one in this context, so
       we can safely renice Xenomai's runthread (i.e. as returned by
       xnpod_current_thread()). */

    if (xnpod_current_thread()->cprio != rootprio)
	xnpod_renice_root(rootprio);
}

static void xnshadow_schedule_tail (adevinfo_t *evinfo)

{
    if (evinfo->domid == RTAI_DOMAIN_ID)
	/* About to resume in xnshadow_harden() after the gatekeeper
	   switched us back. Do _not_ propagate this event so that
	   Linux's tail scheduling won't be performed. */
	return;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    {
    struct mmreq *p;

#ifdef CONFIG_PREEMPT
    preempt_disable();
#endif /* CONFIG_PREEMPT */

    p = mmrqtab + smp_processor_id();

    while (p->out != p->in)
	{
	struct mm_struct *oldmm = p->mm[p->out];
	mmdrop(oldmm);
	bump_mmreq(p->out);
	p->count--;
	}

#ifdef CONFIG_PREEMPT
    preempt_enable();
#endif /* CONFIG_PREEMPT */
    }
#endif  /* KERNEL_VERSION < 2.6.0 */

    adeos_propagate_event(evinfo);
}

static void xnshadow_signal_process (adevinfo_t *evinfo)

{
    struct { struct task_struct *task; int sig; } *evdata = (__typeof(evdata))evinfo->evdata;
    xnthread_t *thread = xnshadow_thread(evdata->task);

    if (thread && !testbits(thread->status,XNRELAX|XNROOT))
	{
	switch (evdata->sig)
	    {
	    case SIGTERM:
	    case SIGKILL:
	    case SIGQUIT:
	    case SIGINT:

		/* Let the kick handler process those signals, and let
		   them propagate. */

		break;

	    default:

		/* Instead of having the shadow threads being marked
		   as uninterruptible when running into the RTAI
		   domain, which somewhat breaks Linux's activity
		   counters, we leave them in an interruptible state,
		   but block the signal propagation here when such
		   threads remain under the control of the Xenomai
		   scheduler. */

		return;
	    }
	}

    adeos_propagate_event(evinfo);
}

static void xnshadow_kick_process (adevinfo_t *evinfo)

{
    struct { struct task_struct *task; } *evdata = (__typeof(evdata))evinfo->evdata;
    struct task_struct *task = evdata->task;
    xnthread_t *thread = xnshadow_thread(task);
    spl_t s;

    if (thread && !testbits(thread->status,XNRELAX|XNROOT))
	{
	/* Some kernel-originated signals do not raise an
	   ADEOS_SIGNAL_PROCESS event and cannot be blocked in any way
	   (e.g. group exit signal). So we must interpose on the
	   ADEOS_KICK_PROCESS event in order to be given a chance to
	   see those at least, and unblock their real-time counterpart
	   if they happen to target a real-time shadow. This event is
	   always propagated and cannot be dismissed, but again, at
	   least we have been warned. */

	if (sigismember(&task->pending.signal,SIGTERM) ||
	    sigismember(&task->pending.signal,SIGKILL) ||
	    sigismember(&task->pending.signal,SIGQUIT) ||
	    sigismember(&task->pending.signal,SIGINT))
	    {
	    splhigh(s);

	    thread->signals |= XNTHREAD_SHADOW_SIGKILL;

	    if (!testbits(thread->status,XNSTARTED))
		xnshadow_start(thread,0,NULL,NULL,0);
	    else
		xnpod_unblock_thread(thread);

	    if (testbits(thread->status,XNSUSP))
		xnpod_resume_thread(thread,XNSUSP);
	    
	    xnshadow_schedule(); /* Schedule in the RTAI space. */

	    splexit(s);
	    }
	}
}

static void xnshadow_renice_process (adevinfo_t *evinfo)

{
    struct { struct task_struct *task; int policy; struct sched_param *param; } *evdata;
    xnthread_t *thread;

    evdata = (__typeof(evdata))evinfo->evdata;
    thread = xnshadow_thread(evdata->task);

    if (!thread)
	{
	adeos_propagate_event(evinfo);
	return;	/* Not a shadow -- Let Linux handle this one. */
	}

    if (evdata->policy != SCHED_FIFO)
	/* Bad policy -- Make Linux to ignore the change. */
	return;

    adeos_propagate_event(evinfo);

    xnpod_renice_thread_inner(thread,evdata->param->sched_priority,0);

    xnpod_schedule(NULL); /* We are already running into the RTAI domain. */
}

int xnshadow_register_skin (unsigned magic,
			    int nrcalls,
			    xnsysent_t *systab)
{
    int muxid;
    spl_t s;

    /* We can only handle up to 256 syscalls per skin, check for over-
       and underflow (MKL) */

    if (XNARCH_MAX_SYSENT < nrcalls || 0 > nrcalls)
	return -EINVAL;

    splhigh(s);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	{
	if (muxtable[muxid].systab == NULL)
	    {
	    muxtable[muxid].systab = systab;
	    muxtable[muxid].nrcalls = nrcalls;
	    muxtable[muxid].magic = magic;
	    muxtable[muxid].refcnt = 0;

	    splexit(s);
	    return muxid + 1;
	    }
	}

    splexit(s);
    
    return -ENOSPC;
}

int xnshadow_unregister_skin (int muxid)

{
    spl_t s;

    if (--muxid < 0 || muxid >= XENOMAI_MUX_NR)
	return -EINVAL;

    if (muxtable[muxid].refcnt > 0)
	return -EBUSY;

    splhigh(s);

    muxtable[muxid].systab = NULL;
    muxtable[muxid].nrcalls = 0;
    muxtable[muxid].magic = 0;

    splexit(s);

    return 0;
}

static void xnshadow_irq_trampoline (unsigned irq) {

    adeos_propagate_irq(irq);
}

static void xnshadow_shield (int iflag)

{
    unsigned irq;

    if (iflag)
	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	    adeos_virtualize_irq(irq,
				 &xnshadow_irq_trampoline,
				 NULL,
				 IPIPE_DYNAMIC_MASK);
    for (;;)
	adeos_suspend_domain();
}

int xnshadow_init (void)

{
    adattr_t attr;

    adeos_init_attr(&attr);
    attr.name = "Xenomai";
    attr.domid = 0x59454e4f;
    attr.entry = &xnshadow_shield;
    attr.priority = ADEOS_ROOT_PRI + 50;

    if (adeos_register_domain(&irq_shield,&attr))
	return -EBUSY;

    nkgkptd = adeos_alloc_ptdkey();
    gkvirq = adeos_alloc_irq();
    adeos_virtualize_irq(gkvirq,&xnshadow_wakeup_handler,NULL,IPIPE_HANDLE_MASK);
    sigvirq = adeos_alloc_irq();
    adeos_virtualize_irq(sigvirq,&xnshadow_signal_handler,NULL,IPIPE_HANDLE_MASK);
    nicevirq = adeos_alloc_irq();
    adeos_virtualize_irq(nicevirq,&xnshadow_renice_handler,NULL,IPIPE_HANDLE_MASK);

    init_MUTEX_LOCKED(&gksync);
    init_MUTEX_LOCKED(&gkreq);

    kernel_thread((void *)&gatekeeper_thread,NULL,0);
    down(&gksync);

    adeos_catch_event(ADEOS_SYSCALL_PROLOGUE,&xnshadow_linux_sysentry);
    adeos_catch_event(ADEOS_SYSCALL_EPILOGUE,&xnshadow_linux_sysexit);
    adeos_catch_event(ADEOS_EXIT_PROCESS,&xnshadow_linux_taskexit);
    adeos_catch_event(ADEOS_SIGNAL_PROCESS,&xnshadow_signal_process);
    adeos_catch_event(ADEOS_KICK_PROCESS,&xnshadow_kick_process);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,&xnshadow_schedule_head);
    adeos_catch_event_from(&rtai_domain,ADEOS_SCHEDULE_TAIL,&xnshadow_schedule_tail);
    adeos_catch_event_from(&rtai_domain,ADEOS_SYSCALL_PROLOGUE,&xnshadow_realtime_sysentry);
    adeos_catch_event_from(&rtai_domain,ADEOS_RENICE_PROCESS,&xnshadow_renice_process);

    return 0;
}

void xnshadow_cleanup (void)

{
    gkstop = 1;
    up(&gkreq);
    down(&gksync);

    adeos_free_irq(gkvirq);
    adeos_free_irq(sigvirq);
    adeos_free_irq(nicevirq);
    adeos_free_ptdkey(nkgkptd);

    adeos_catch_event(ADEOS_SYSCALL_PROLOGUE,NULL);
    adeos_catch_event(ADEOS_SYSCALL_EPILOGUE,NULL);
    adeos_catch_event(ADEOS_EXIT_PROCESS,NULL);
    adeos_catch_event(ADEOS_SIGNAL_PROCESS,NULL);
    adeos_catch_event(ADEOS_KICK_PROCESS,NULL);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,NULL);
    adeos_catch_event_from(&rtai_domain,ADEOS_SCHEDULE_TAIL,NULL);
    adeos_catch_event_from(&rtai_domain,ADEOS_SYSCALL_PROLOGUE,NULL);
    adeos_catch_event_from(&rtai_domain,ADEOS_RENICE_PROCESS,NULL);

    adeos_unregister_domain(&irq_shield);
}

/*@}*/

EXPORT_SYMBOL(xnshadow_harden);
EXPORT_SYMBOL(xnshadow_map);
EXPORT_SYMBOL(xnshadow_register_skin);
EXPORT_SYMBOL(xnshadow_relax);
EXPORT_SYMBOL(xnshadow_start);
EXPORT_SYMBOL(xnshadow_ticks2ts);
EXPORT_SYMBOL(xnshadow_ticks2tv);
EXPORT_SYMBOL(xnshadow_ts2ticks);
EXPORT_SYMBOL(xnshadow_tv2ticks);
EXPORT_SYMBOL(xnshadow_unmap);
EXPORT_SYMBOL(xnshadow_unregister_skin);
