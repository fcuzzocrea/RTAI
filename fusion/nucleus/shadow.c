/*!\file shadow.c
 * \brief Real-time shadow services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The RTAI project <http://www.rtai.org>
 * Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
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
 * \ingroup nucleus
 * \defgroup shadow Real-time shadow services.
 *
 * Real-time shadow services.
 *
 *@{*/

#define XENO_SHADOW_MODULE
#include <asm/signal.h>
#include <linux/unistd.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/synch.h>
#include <nucleus/module.h>
#include <nucleus/shadow.h>
#if CONFIG_TRACE
#include <linux/trace.h>
#endif

int nkgkptd;

struct xnskentry muxtable[XENOMAI_MUX_NR];

static int traceme = 0;

static adomain_t irq_shield;

static struct task_struct *gatekeeper[XNARCH_NR_CPUS];

static struct semaphore gksync;

static struct semaphore gkreq[XNARCH_NR_CPUS];

static int gkstop;

static unsigned gkvirq, sigvirq, nicevirq, shieldvirq;

static int gk_enter_in[XNARCH_NR_CPUS],
           gk_enter_out[XNARCH_NR_CPUS];

static xnthread_t *gk_enter_wheel[XNARCH_NR_CPUS][XNSHADOW_MAXRQ];

static int gk_leave_in[XNARCH_NR_CPUS],
           gk_leave_out[XNARCH_NR_CPUS];

static struct task_struct *gk_leave_wheel[XNARCH_NR_CPUS][XNSHADOW_MAXRQ];

static int gk_signal_in[XNARCH_NR_CPUS],
           gk_signal_out[XNARCH_NR_CPUS];

static struct {

    struct task_struct *task;
    int sig;

} gk_signal_wheel[XNARCH_NR_CPUS][XNSHADOW_MAXRQ];

static int gk_renice_in[XNARCH_NR_CPUS],
           gk_renice_out[XNARCH_NR_CPUS];

static struct {

    struct task_struct *task;
    int prio;

} gk_renice_wheel[XNARCH_NR_CPUS][XNSHADOW_MAXRQ];

static cpumask_t shielded_cpus;

#define get_switch_lock_owner() \
switch_lock_owner[task_cpu(current)]

#define set_switch_lock_owner(t) \
do { \
   switch_lock_owner[task_cpu(t)] = t; \
} while(0)

static struct task_struct *switch_lock_owner[XNARCH_NR_CPUS];

static inline struct task_struct *get_calling_task (adevinfo_t *evinfo) {

    return (xnpod_shadow_p()
            ? current
            : rthal_get_root_current(xnarch_current_cpu()));
}

static inline void set_linux_task_priority (struct task_struct *task, int prio)

{
    adeos_declare_cpuid;
    spl_t s;

    /* Can be called on behalf of RTAI -- The priority of the
       real-time shadow will be used to determine the Linux
       task priority. */

    if (adp_current == adp_root)
	{
	rthal_set_linux_task_priority(task,SCHED_FIFO,prio);
	return;
	}

    splhigh(s);
    adeos_load_cpuid();
    gk_renice_wheel[cpuid][gk_renice_in[cpuid]].task = task;
    gk_renice_wheel[cpuid][gk_renice_in[cpuid]].prio = prio;
    gk_renice_in[cpuid] = (gk_renice_in[cpuid] + 1) & (XNSHADOW_MAXRQ - 1);
    splexit(s);

    adeos_propagate_irq(nicevirq);
}

static inline void request_syscall_restart (xnthread_t *thread, struct pt_regs *regs)

{
    if (testbits(thread->status,XNKICKED))
	{
	if (__xn_interrupted_p(regs))
	    __xn_error_return(regs,-ERESTARTSYS);

	clrbits(thread->status,XNKICKED);
	}

    xnshadow_relax();
}

static inline void engage_irq_shield (int this_cpu) {

    cpu_set(this_cpu,shielded_cpus);
}

static void disengage_irq_shield (int this_cpu)
     
{
    cpumask_t other_cpus;

    cpu_clear(this_cpu,shielded_cpus);

    other_cpus = shielded_cpus;

    if (cpus_empty(other_cpus))
	{
#ifdef CONFIG_SMP
        adeos_send_ipi(ADEOS_SERVICE_IPI1,other_cpus);
#endif /* CONFIG_SMP */
	adeos_trigger_irq(shieldvirq);
	}
}

#ifndef __adeos_unlock_local_irq /* TEMPORARY */
#define __adeos_unlock_local_irq(adp,cpuid,irq) \
do { \
    if (test_and_clear_bit(IPIPE_LOCK_FLAG,&(adp)->irqs[irq].control)) \
         if ((adp)->cpudata[cpuid].irq_hits[irq] > 0) { \
           __set_bit(irq & IPIPE_IRQ_IMASK,&(adp)->cpudata[cpuid].irq_pending_lo[irq >> IPIPE_IRQ_ISHIFT]); \
           __set_bit(irq >> IPIPE_IRQ_ISHIFT,&(adp)->cpudata[cpuid].irq_pending_hi); \
         } \
} while(0)
#endif

static void xnshadow_shield_body (unsigned irq)

{
    static unsigned long irq_lo[XNARCH_NR_CPUS][IPIPE_IRQ_IWORDS];
    static unsigned long irq_hi[XNARCH_NR_CPUS];
    adeos_declare_cpuid;
    unsigned hi, lo;

    /* Adeos guarantees that hw interrupts are always off on entry of
       IRQ handlers for non-root domains, so we can just load the
       current cpuid with no special protection. */

    adeos_load_cpuid();

    if (cpus_empty(shielded_cpus))
	{
	while (irq_hi[cpuid] != 0)
	    {
	    hi = ffnz(irq_hi[cpuid]);
	    __clear_bit(hi,&irq_hi[cpuid]);

	    while (irq_lo[cpuid][hi] != 0)
		{
		unsigned _irq;
		lo = ffnz(irq_lo[cpuid][hi]);
		__clear_bit(lo,&irq_lo[cpuid][hi]);
		_irq = (hi << IPIPE_IRQ_ISHIFT) + lo;
		__adeos_unlock_local_irq(adp_root,cpuid,_irq);
		}
	    }

	goto propagate;
	}

    switch (irq)
	{
#ifdef CONFIG_SMP
	case ADEOS_CRITICAL_IPI:
	case ADEOS_SERVICE_IPI1:
	XNARCH_PASSTHROUGH_IRQS

	    /* Never lock out these ones. */
	    break;
#endif /* CONFIG_SMP */

	default:

	    {
	    hi = irq >> IPIPE_IRQ_ISHIFT;
	    lo = irq & IPIPE_IRQ_IMASK;

	    /* By not testing the Adeos lock bit (IPIPE_LOCK_FLAG) but
	       rather our local one, we accept that some Linux driver
	       forcibly unlocks the interrupt path for the root domain
	       by calling the ->enable() or ->startup() IRQ descriptor
	       handlers. In such a case, our marker would tell us that
	       the IRQ is already locked whilst it is actually not at
	       the Adeos-level. We do this since eager IRQ enabling by
	       Linux is usually not part of the usual operations, but
	       rather part of a recovery mode where synchronous
	       replies from the hardware are expected; i.e.
	       interrupts on such channel _must_ flow then. The cost
	       for us is real-time operations in the Linux domain
	       being preemptable by non real-time interrupt handling
	       for this source until the next time the shield gets
	       reasserted, but the gain is box stability. */

	    if (!__test_and_set_bit(hi,&irq_hi[cpuid]))
		if (!__test_and_set_bit(lo,&irq_lo[cpuid][hi]))
		    __adeos_lock_irq(adp_root,cpuid,irq);
	    }
	}

 propagate:

    adeos_propagate_irq(irq);
}

static void xnshadow_shield (int iflag)

{
    unsigned irq;

    if (iflag)
	for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	    adeos_virtualize_irq(irq,
				 &xnshadow_shield_body,
				 NULL,
				 IPIPE_DYNAMIC_MASK);
    for (;;)
	adeos_suspend_domain();
}

static void xnshadow_shield_handler (unsigned virq)

{
    /* nop */
}

static void xnshadow_renice_handler (unsigned virq)

{
    adeos_declare_cpuid;

    /* Unstall the root stage first since the renice operation might
       sleep.  */
    __adeos_unstall_root();

    adeos_load_cpuid();

    while (gk_renice_out[cpuid] != gk_renice_in[cpuid])
	{
	struct task_struct *task = gk_renice_wheel[cpuid][gk_renice_out[cpuid]].task;
	int prio = gk_renice_wheel[cpuid][gk_renice_out[cpuid]].prio;
	gk_renice_out[cpuid] = (gk_renice_out[cpuid] + 1) & (XNSHADOW_MAXRQ - 1);

	rthal_set_linux_task_priority(task,SCHED_FIFO,prio);
	}
}

static void xnshadow_wakeup_handler (unsigned virq)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    while (gk_leave_out[cpuid] != gk_leave_in[cpuid])
	{
	struct task_struct *task = gk_leave_wheel[cpuid][gk_leave_out[cpuid]];
	gk_leave_out[cpuid] = (gk_leave_out[cpuid] + 1) & (XNSHADOW_MAXRQ - 1);

	if (xnshadow_thread(task))
	    engage_irq_shield(cpuid);

#ifdef CONFIG_SMP
        /* If the fusion task migrated while hardened, "migrate" its Linux
           counter-part (migrating a suspended task is cheap) */
        if (!cpu_isset(cpuid, task->cpus_allowed))
            set_cpus_allowed(task, cpumask_of_cpu(cpuid));
#endif
	wake_up_process(task);
	}
}

static void xnshadow_signal_handler (unsigned virq)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    while (gk_signal_out[cpuid] != gk_signal_in[cpuid])
	{
	struct task_struct *task = gk_signal_wheel[cpuid][gk_signal_out[cpuid]].task;
	int sig = gk_signal_wheel[cpuid][gk_signal_out[cpuid]].sig;
	gk_signal_out[cpuid] = (gk_signal_out[cpuid] + 1) & (XNSHADOW_MAXRQ - 1);
	send_sig(sig,task,1);
	}
}

static inline void xnshadow_sched_wakeup (struct task_struct *task)

{
    adeos_declare_cpuid;
    spl_t s;

    splhigh(s);
    adeos_load_cpuid();
    gk_leave_wheel[cpuid][gk_leave_in[cpuid]] = task;
    gk_leave_in[cpuid] = (gk_leave_in[cpuid] + 1) & (XNSHADOW_MAXRQ - 1);
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
    adeos_declare_cpuid;
    spl_t s;

    splhigh(s);
    adeos_load_cpuid();
    gk_signal_wheel[cpuid][gk_signal_in[cpuid]].task = task;
    gk_signal_wheel[cpuid][gk_signal_in[cpuid]].sig = sig;
    gk_signal_in[cpuid] = (gk_signal_in[cpuid] + 1) & (XNSHADOW_MAXRQ - 1);
    splexit(s);

    adeos_propagate_irq(sigvirq);
}

static void xnshadow_itimer_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;
    xnshadow_sched_signal(xnthread_archtcb(thread)->user_task,SIGALRM);
}

static void gatekeeper_thread (void *data)

{
    unsigned cpu = (unsigned)(unsigned long)data;
    char name[16] = "gatekeeper";
#ifdef CONFIG_SMP
    spl_t s;
#endif /* CONFIG_SMP */
    
    gatekeeper[cpu] = current;

    set_cpus_allowed(current, cpumask_of_cpu(cpu));

#ifdef CONFIG_SMP
    sprintf(name,"gatekeeper/%u",cpu);
#endif /* CONFIG_SMP */
    daemonize(name);

    sigfillset(&current->blocked);

    up(&gksync);

    for (;;)
        {
	set_linux_task_priority(current,1);

	down_interruptible(&gkreq[cpu]);

	if (gkstop)
	    break;

	while (gk_enter_out[cpu] != gk_enter_in[cpu])
	    {
	    xnthread_t *thread = gk_enter_wheel[cpu][gk_enter_out[cpu]];
	    gk_enter_out[cpu] = (gk_enter_out[cpu] + 1) & (XNSHADOW_MAXRQ - 1);
#if 1
	    if (traceme)
		printk("__GK__[%u] %s (ipipe=%lu)\n",
		       cpu,thread->name,adeos_test_pipeline_from(&rthal_domain));
#endif
#ifdef CONFIG_SMP
            /* If the fusion task migrated while run by Linux, migrate
               the shadow too. */
            xnlock_get_irqsave(&nklock, s);
            thread->sched = xnpod_sched_slot(cpu);
	    xnpod_resume_thread(thread,XNRELAX);
            xnlock_put_irqrestore(&nklock, s);
#else /* !CONFIG_SMP */
	    xnpod_resume_thread(thread,XNRELAX);
#endif /* CONFIG_SMP */
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
 * @internal
 * \fn void xnshadow_harden(void);
 * \brief Migrate a Linux task to the RTAI domain.
 *
 * This service causes the transition of "current" from the Linux
 * domain to RTAI. This is obtained by asking the gatekeeper to resume
 * the shadow mated with "current" then triggering the rescheduling
 * procedure in the RTAI domain. The shadow will resume in the RTAI
 * domain as returning from schedule().
 *
 * Side-effect: This routine indirectly triggers the rescheduling
 * procedure (see __xn_sys_sched service).
 *
 * Context: This routine must be called on behalf of a user-space task
 * from the Linux domain.
 */

void xnshadow_harden (void)

{
    adeos_declare_cpuid;
    spl_t s;

#if 1
    if (traceme)
	printk("_!HARDENING!_ %s, status 0x%lx, pid=%d (ipipe=%lu, domain=%s)\n",
	       xnshadow_thread(current)->name,
	       xnshadow_thread(current)->status,
	       current->pid,
	       adeos_test_pipeline_from(&rthal_domain),
	       adp_current->name);
#endif
    /* Enqueue the request to move "current" from the Linux domain to
       the RTAI domain. This will cause the shadow thread to resume
       using the register state of the Linux task. */

    splhigh(s);
    adeos_load_cpuid();

    gk_enter_wheel[cpuid][gk_enter_in[cpuid]] = xnshadow_thread(current);
    gk_enter_in[cpuid] = (gk_enter_in[cpuid] + 1) & (XNSHADOW_MAXRQ - 1);

    engage_irq_shield(cpuid);

    splexit(s);

    if (xnshadow_thread(current)->cprio > gatekeeper[cpuid]->rt_priority)
	set_linux_task_priority(gatekeeper[cpuid],xnshadow_thread(current)->cprio);

    set_current_state(TASK_INTERRUPTIBLE);

    /* Wake up the gatekeeper which will perform the transition. */
    up(&gkreq[cpuid]);

    schedule();

#ifdef CONFIG_RTAI_HW_FPU
    xnpod_switch_fpu(xnpod_current_sched());
#endif /* CONFIG_RTAI_HW_FPU */

    xnlock_clear_irqon(&nklock);

    /* "current" is now running into the RTAI domain. */

#if 1
    if (traceme)
	printk("__RT__ %s, pid=%d (ipipe=%lu)\n",
	       xnpod_current_thread()->name,
	       current->pid,
	       adeos_test_pipeline_from(&rthal_domain));
#endif
}

/*! 
 * @internal
 * \fn void xnshadow_relax(void);
 * \brief Switch a shadow thread back to the Linux domain.
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

 * Note: "current" is valid here since the shadow runs with the
 * properties of the Linux task.
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
	       adeos_test_pipeline_from(&rthal_domain),
	       adp_current->name);
#endif

    xnshadow_sched_wakeup(xnthread_archtcb(thread)->user_task);
    xnpod_renice_root(thread->cprio);
    splhigh(s);
    xnpod_suspend_thread(thread,XNRELAX,XN_INFINITE,NULL);
    __adeos_schedule_back_root(get_switch_lock_owner());
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
	       adeos_test_pipeline_from(&rthal_domain),
	       adp_current->name);
#endif
}

void xnshadow_unmap (xnthread_t *thread) /* Must be called by the task deletion hook. */

{
    struct task_struct *task;
    unsigned muxid, magic;

    task = xnthread_archtcb(thread)->user_task;

#if 1
    if (traceme)
	printk("__UNMAP__: %s, pid=%d, task=%s (ipipe=%lu, domain=%s, taskstate=%ld)\n",
	       thread->name,
	       task ? task->pid : -1,
	       task ? task->comm : "<null>",
	       adeos_test_pipeline_from(&rthal_domain),
	       adp_current->name,
	       task ? task->state : -1);
#endif

    magic = xnthread_get_magic(thread);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	{
	if (muxtable[muxid].magic == magic)
            {
            if (xnarch_atomic_dec_and_test(&muxtable[muxid].refcnt))
                /* We were the last thread, decrement the counter,
		   since it was incremented by the first "attach"
		   operation. */
                xnarch_atomic_dec(&muxtable[muxid].refcnt);

            break;
            }
        }

    if (!task)
	return;

    xnshadow_ptd(task) = NULL;

    /* The zombie side returning to user-space will be trapped and
       exited inside the pod's rescheduling routines. */
    xnshadow_sched_wakeup(task);
}

void xnshadow_sync_post (pid_t syncpid, int *u_syncp, int err)

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
    int syncflag;
    spl_t s;

    for (;;)	/* Poor man's semaphore P. */
	{
	xnlock_get_irqsave(&nklock,s);

	__xn_get_user(current,syncflag,u_syncp);

	if (syncflag)
	    break;

	set_current_state(TASK_INTERRUPTIBLE);

	xnlock_put_irqrestore(&nklock,s);

	schedule();
	}

    xnlock_put_irqrestore(&nklock,s);

    return syncflag == 0x7fffffff ? 0 : syncflag;
}

void xnshadow_exit (void)

{
    __adeos_schedule_back_root(get_switch_lock_owner());
    do_exit(0);
}

/*! 
 * \fn void xnshadow_map(xnthread_t *thread,
			    pid_t syncpid,
			    int *u_syncp);
 * @internal
 * \brief Create a shadow thread context.
 *
 * This call maps a nucleus thread to the "current" Linux task.
 *
 * @param thread The descriptor address of the new shadow thread to be
 * mapped to "current". This descriptor must have been previously
 * initialized by a call to xnpod_init_thread().
 *
 * @warning The thread must have been set the same magic number
 * (i.e. xnthread_set_magic()) than the one used to register the skin
 * it belongs to. Failing to do so leads to unexpected results.
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
 * Side-effect: This routine indirectly calls the rescheduling
 * procedure.
 *
 * Context: This routine must be called on behalf of the Linux
 * user-space task which is being shadowed.
 */

void xnshadow_map (xnthread_t *thread,
		   pid_t syncpid,
		   int *u_syncp) /* user-space pointer */
{
    int autostart = !(syncpid && u_syncp);
    unsigned muxid, magic;

    /* Prevent Linux from migrating us from now on. */
    set_cpus_allowed(current, cpumask_of_cpu(smp_processor_id()));

    /* Increment the interface reference count. */
    magic = xnthread_get_magic(thread);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	{
	if (muxtable[muxid].magic == magic)
            {
            xnarch_atomic_inc(&muxtable[muxid].refcnt);
            break;
            }
        }

    current->cap_effective |= CAP_TO_MASK(CAP_IPC_LOCK)|CAP_TO_MASK(CAP_SYS_RAWIO)|CAP_TO_MASK(CAP_SYS_NICE);

    xnarch_init_shadow_tcb(xnthread_archtcb(thread),thread,xnthread_name(thread));

    xnpod_suspend_thread(thread,XNRELAX,XN_INFINITE,NULL);

    set_linux_task_priority(current,xnthread_base_priority(thread));

    if (autostart)
        xnpod_resume_thread(thread,XNDORMANT);
    else
	/* Wake up the initiating Linux task. */
	xnshadow_sync_post(syncpid,u_syncp,0);
    
    xnshadow_ptd(current) = thread;

#if 1
    if (traceme)
	printk("__MAP__ %s from %s, prio=%d, pid=%d, domain=%s\n",
	       xnthread_name(thread),
	       xnpod_current_sched()->runthread->name,
	       xnthread_base_priority(thread),
	       xnthread_archtcb(thread)->user_task->pid,
	       adp_current->name);
#endif

    /* If not autostarting, the shadow will be left suspended in
       dormant state. */
    xnshadow_harden();

    if (autostart)
	/* We are immediately joining the RTAI realm on behalf of the
	   current Linux task. */
	xnshadow_start(thread,0,NULL,NULL,1);
    else if (xnshadow_ptd(current) == NULL)
	    /* Whoops, this shadow was unmapped while in dormant state
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

    xnlock_get_irqsave(&nklock,s);

    __setbits(thread->status,(mode & (XNTHREAD_MODE_BITS|XNSUSP))|XNSTARTED);
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

    xnlock_put_irqrestore(&nklock,s);

    if (resched)
	/* Reschedule on behalf of the RTAI domain. */
	xnshadow_schedule();
}

void xnshadow_renice (xnthread_t *thread)

{
    struct task_struct *task = xnthread_archtcb(thread)->user_task;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    set_linux_task_priority(task,thread->cprio);

    if (xnpod_root_p() &&
	testbits(thread->status,XNRELAX) &&
	xnpod_priocompare(thread->cprio,xnpod_current_root()->cprio) > 0)
	xnpod_renice_root(thread->cprio);

    xnlock_put_irqrestore(&nklock,s);
}

static int xnshadow_attach_skin (struct task_struct *curr,
				 unsigned magic,
				 u_long infarg)
{
    xnsysinfo_t info;
    int muxid;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	{
	if (muxtable[muxid].magic == magic)
	    {
	    /* Increment the reference count now (actually, only the first call
	       to xnshadow_attach_skin really increments the counter), so that
	       the interface cannot be removed under our feet. */

            if (!xnarch_atomic_inc_and_test(&muxtable[muxid].refcnt))
                xnarch_atomic_dec(&muxtable[muxid].refcnt);

	    xnlock_put_irqrestore(&nklock,s);

	    /* Since the pod might be created by the event callback
	       and not earlier than that, do not refer to nkpod until
	       the latter had a chance to call xnpod_init(). */

	    if (muxtable[muxid].eventcb)
		{
		int err = muxtable[muxid].eventcb(XNSHADOW_CLIENT_ATTACH);

		if (err)
		    {
		    xnarch_atomic_dec(&muxtable[muxid].refcnt);
		    return err;
		    }
		}

	    if (!nkpod || testbits(nkpod->status,XNPIDLE))
		/* Ok mate, but you really ought to create some pod in
		   a way or another if you want me to be of some
		   help here... */
		return -ENOSYS;

	    if (infarg)
		{
		info.cpufreq = xnarch_get_cpu_freq();
		info.tickval = xnpod_get_tickval();
		__xn_copy_to_user(curr,(void *)infarg,&info,sizeof(info));
		}

	    return ++muxid;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    return -ENOENT;
}

static int xnshadow_detach_skin (struct task_struct *curr, int muxid)

{
    xnholder_t *holder, *nholder;
    xnthread_t *thread;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (--muxid < 0 || muxid >= XENOMAI_MUX_NR || muxtable[muxid].magic == 0)
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EINVAL;
	}

    if (muxtable[muxid].eventcb)
	/* The reference count is our safety belt against concurrent
	   removal of the skin under our feet, so we do not decrement
	   it until the event callback had a chance to run, possibly
	   releasing the current critical section internally. */
	muxtable[muxid].eventcb(XNSHADOW_CLIENT_DETACH);

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

	if (xnthread_get_magic(thread) == muxtable[muxid].magic &&
	    testbits(thread->status,XNSHADOW))
	    xnpod_delete_thread(thread);
	}

    xnlock_put_irqrestore(&nklock,s);

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

	    if (!testbits(nkpod->status,XNTIMED))
		return 0; /* No RT timer started -- Let Linux handle this. */

	    if (!__xn_access_ok(curr,VERIFY_READ,(void *)__xn_reg_arg1(regs),sizeof(t)))
		{
		__xn_error_return(regs,-EFAULT);
		return 1;
		}

	    __xn_copy_from_user(curr,&t,(void *)__xn_reg_arg1(regs),sizeof(t));

	    if (t.tv_nsec >= 1000000000L || t.tv_nsec < 0 || t.tv_sec < 0)
		{
		__xn_error_return(regs,-EINVAL);
		return 1;
		}

	    if (migrate) /* Shall we migrate to RTAI first? */
		xnshadow_harden();

#if CONFIG_RTAI_HW_APERIODIC_TIMER
	    if (!testbits(nkpod->status,XNTMPER))
		expire = xnpod_get_cpu_time();
	    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
		expire = nkpod->jiffies;

	    delay = xnshadow_ts2ticks(&t);
	    expire += delay;
	    __xn_success_return(regs,-1);

	    if (delay > 0)
		{
		xnpod_delay(delay);

		if (signal_pending(curr))
		    request_syscall_restart(thread,regs);
		}

#if CONFIG_RTAI_HW_APERIODIC_TIMER
	    if (!testbits(nkpod->status,XNTMPER))
		now = xnpod_get_cpu_time();
	    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
		now = nkpod->jiffies;

	    if (now >= expire)
		__xn_success_return(regs,0);
	    else
		{
		if (__xn_reg_arg2(regs))
		    {
		    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg2(regs),sizeof(t)))
			{
			__xn_error_return(regs,-EFAULT);
			return 1;
			}

		    xnshadow_ticks2ts(now - expire,&t);
		    __xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&t,sizeof(t));
		    }
		}

	    return 1;
	    }

	case __NR_setitimer:

	    {
	    xnticks_t delay, interval;
	    struct itimerval itv;

	    if (!testbits(nkpod->status,XNTIMED) ||
		__xn_reg_arg1(regs) != ITIMER_REAL)
		return 0;

	    if (__xn_reg_arg2(regs))
		{
		if (!__xn_access_ok(curr,VERIFY_READ,(void *)__xn_reg_arg2(regs),sizeof(itv)))
		    {
		    __xn_error_return(regs,-EFAULT);
		    return 1;
		    }

		__xn_copy_from_user(curr,&itv,(void *)__xn_reg_arg2(regs),sizeof(itv));
		}
	    else
		memset(&itv,0,sizeof(itv));

	    xntimer_stop(&thread->atimer);

	    delay = xnshadow_tv2ticks(&itv.it_value);
	    interval = xnshadow_tv2ticks(&itv.it_interval);

	    if (delay > 0 && xntimer_start(&thread->atimer,delay,interval) < 0)
		{
		__xn_error_return(regs,-ETIMEDOUT);
		return 1;
		}

	    if (__xn_reg_arg3(regs))
		{
		if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg3(regs),sizeof(itv)))
		    {
		    __xn_error_return(regs,-EFAULT);
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

	    __xn_success_return(regs,0);

	    return 1;
	    }

	case __NR_getitimer:

	    {
	    xnticks_t delay, interval;
	    struct itimerval itv;

	    if (!testbits(nkpod->status,XNTIMED) ||
		__xn_reg_arg1(regs) != ITIMER_REAL)

	    if (!__xn_reg_arg2(regs) ||
		!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg2(regs),sizeof(itv)))
		{
		__xn_error_return(regs,-EFAULT);
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
	    __xn_success_return(regs,0);

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
    u_long sysflags;

    if (nkpod && !testbits(nkpod->status,XNPIDLE))
	goto skin_loaded;

    if (__xn_reg_mux_p(regs))
	{
	if (__xn_reg_mux(regs) == __xn_mux_code(0,__xn_sys_attach))
	    /* Valid exception case: we may be called to attach a
	       skin which will create its own pod through its
	       callback routine before returning to user-space. */
	    goto propagate_syscall;

	xnlogwarn("Bad syscall %ld/%ld -- no skin loaded.\n",
		  __xn_mux_id(regs),
		  __xn_mux_op(regs));

	__xn_error_return(regs,-ENOSYS);
	}
    else
	/* Regular Linux syscall with no skin loaded -- propagate it
	   to the Linux kernel. */
	goto propagate_syscall;

    return;

 skin_loaded:

    task = get_calling_task(evinfo);
    thread = xnshadow_thread(task);

    if (__xn_reg_mux_p(regs))
	goto xenomai_syscall;

    if (xnpod_root_p())
	/* The call originates from the Linux domain, either from a
	   relaxed shadow or from a regular Linux task; just propagate
	   the event so that we will fall back to
	   xnshadow_linux_sysentry(). */
	   goto propagate_syscall;

    /* From now on, we know that we have a valid shadow thread
       pointer. */

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

    if (xnshadow_substitute_syscall(task,regs,0))
	/* This is a Linux syscall issued on behalf of a shadow thread
	   running inside the RTAI domain. This call has just been
	   intercepted by Xenomai and a RTAI replacement has been
	   substituted for it. */
	return;

    /* This syscall has not been substituted, let Linux handle
       it. This will eventually fall back to the Linux syscall handler
       if our Linux domain handler does not intercept it. Before we
       let it go, ensure that our running thread has properly entered
       the Linux domain. */

#if 1
    if (traceme)
	printk("__SYSIN__ %s, call=%ld, pid=%d, origin=%x\n",
	       xnpod_current_thread()->name,
	       __xn_reg_mux(regs),
	       task->pid,
	       evinfo->domid);
#endif

    xnshadow_relax();

    goto propagate_syscall;

 xenomai_syscall:

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

    if (muxid != 0)
	goto skin_syscall;

    /* Nucleus internal syscall. */

    switch (muxop)
	{
	case __xn_sys_sched:

	    xnpod_schedule();
	    return;

	case __xn_sys_migrate:

	    if (!thread)	/* Not a shadow anyway. */
		__xn_success_return(regs,0);
	    else if (__xn_reg_arg1(regs)) /* Linux => RTAI */
		{
		if (!xnthread_test_flags(thread,XNRELAX))
		    __xn_success_return(regs,0);
		else
		    /* Migration to RTAI from the Linux domain must be
		       done from the latter: propagate the request to
		       the Linux-level handler. */
		    adeos_propagate_event(evinfo);
		}
	    else	/* RTAI => Linux */
		{
		if (xnthread_test_flags(thread,XNRELAX))
		    __xn_success_return(regs,0);
		else
		    {
		    __xn_success_return(regs,1);
		    xnshadow_relax();
		    }
		}

	    return;

	case __xn_sys_attach:
	case __xn_sys_detach:
	case __xn_sys_sync:

 propagate_syscall:

	    /* Delegate the syscall handling to the Linux domain. */
	    adeos_propagate_event(evinfo);
	    return;

#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
	case __xn_sys_timestamps:

	    /* Send back the execution timestamps. */

	    if (__xn_access_ok(task,
			       VERIFY_WRITE,
			       (void *)__xn_reg_arg1(regs),
			       sizeof(xntimes_t)))
		{
		xntimes_t ts;
		xnpod_get_timestamps(&ts);
		__xn_copy_to_user(task,(void *)__xn_reg_arg1(regs),&ts,sizeof(ts));
		__xn_success_return(regs,sizeof(ts));
		}
	    else
		__xn_error_return(regs,-EFAULT);

	    return;
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */

	default:

 bad_syscall:
	    __xn_error_return(regs,-ENOSYS);
	    return;
	}

 skin_syscall:

    if (muxid < 0 || muxid > XENOMAI_MUX_NR ||
	muxop < 0 || muxop >= muxtable[muxid - 1].nrcalls)
	goto bad_syscall;

    sysflags = muxtable[muxid - 1].systab[muxop].flags;

    if ((sysflags & __xn_flag_shadow) != 0 && !thread)
	{
	__xn_error_return(regs,-EACCES);
	return;
	}

    /*
     * Here we have to dispatch the syscall execution properly,
     * depending on:
     *
     * o Whether the syscall must be run into the Linux or RTAI
     * domain, or indifferently in the current RTAI domain.
     *
     * o Whether the caller currently runs in the Linux or RTAI
     * domain.
     */

    if ((sysflags & __xn_flag_lostage) != 0)
	{
	/* Syscall must run into the Linux domain. */

	if (evinfo->domid == RTHAL_DOMAIN_ID)
	    /* Request originates from the RTAI domain: just relax the
	       caller and execute the syscall immediately after. */
	    xnshadow_relax();
	else
	    /* Request originates from the Linux domain: propagate the
	       event to our Linux-based handler, so that the syscall
	       is executed from there. */
	    goto propagate_syscall;
	}
    else if ((sysflags & __xn_flag_histage) != 0)
	{
	/* Syscall must run into the RTAI domain. */

	if (evinfo->domid != RTHAL_DOMAIN_ID)
	    /* Request originates from the Linux domain: propagate the
	       event to our Linux-based handler, so that the caller is
	       hardened and the syscall is eventually executed from
	       there. */
	    goto propagate_syscall;
	
	/* Request originates from the RTAI domain: run the syscall
	   immediately. */
	}

    __xn_status_return(regs,muxtable[muxid - 1].systab[muxop].svc(task,regs));

    if (xnpod_shadow_p() && signal_pending(task))
	request_syscall_restart(thread,regs);
}

static void xnshadow_linux_sysentry (adevinfo_t *evinfo)

{
    struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
    xnthread_t *thread = xnshadow_thread(current);
    int muxid, muxop;

    if (__xn_reg_mux_p(regs))
	goto xenomai_syscall;

    if (thread && xnshadow_substitute_syscall(current,regs,1))
	/* This is a Linux syscall issued on behalf of a shadow thread
	   running inside the Linux domain. If the call has been
	   substituted with a RTAI replacement, do not let Linux know
	   about it. */
	return;

    /* Fall back to Linux syscall handling. */
    adeos_propagate_event(evinfo);
    return;

 xenomai_syscall:

    /* muxid and muxop have already been checked in the RTAI domain
       handler. */

    muxid = __xn_mux_id(regs);
    muxop = __xn_mux_op(regs);

#if 1
    if (traceme)
	printk("REQ {skin=%d, op=%d} on behalf of thread %s, pid=%d in domain %s\n",
	       muxid,
	       muxop,
	       nkpod ? xnpod_current_thread()->name : "<system>",
	       current->pid,
	       adp_current->name);
#endif

    if (muxid != 0)
	goto skin_syscall;

    /* These are special built-in services which must run on behalf of
       the Linux domain. */

    switch (muxop)
	{
	case __xn_sys_sync:

	    __xn_status_return(regs,xnshadow_sync_wait((int *)__xn_reg_arg1(regs)));
	    return;

	case __xn_sys_migrate:

	    __xn_success_return(regs,1);
	    xnshadow_harden();
	    return;

	case __xn_sys_attach:

	    __xn_reg_rval(regs) = xnshadow_attach_skin(current,
						       __xn_reg_arg1(regs),
						       __xn_reg_arg2(regs));
	    return;
		
	case __xn_sys_detach:
	    
	    __xn_reg_rval(regs) = xnshadow_detach_skin(current,
						       __xn_reg_arg1(regs));
	    return;
	}

 skin_syscall:

    if ((muxtable[muxid - 1].systab[muxop].flags & __xn_flag_histage) != 0)
	/* This request originates from the Linux domain and must be
	   run into the RTAI domain: harden the caller and execute the
	   syscall. */
	xnshadow_harden();

    __xn_status_return(regs,muxtable[muxid - 1].systab[muxop].svc(current,regs));

    if (xnpod_shadow_p() && signal_pending(current))
	request_syscall_restart(xnshadow_thread(current),regs);
}

static void xnshadow_linux_taskexit (adevinfo_t *evinfo)

{
    xnthread_t *thread = xnshadow_thread(current);

    if (!thread)
	{
	adeos_propagate_event(evinfo);
	return;
	}

#if 1
    if (traceme)
	printk("LINUX EXIT [%s] on behalf of thread %s, pid=%d/%s, relaxed? %d\n",
		xnpod_current_thread()->name,
	       thread->name,
	       current->pid,
	       current->comm,
	       !!testbits(thread->status,XNRELAX));
#endif

    if (xnpod_shadow_p())
	xnshadow_relax();

    /* So that we won't attempt to further wakeup the exiting task in
       xnshadow_unmap(). */

    xnthread_archtcb(thread)->user_task = NULL;
    xnshadow_ptd(current) = NULL;

    xnpod_delete_thread(thread);

#if 1
    if (traceme)
	printk("Cleaned up %s\n",thread->name);
#endif
}

static void xnshadow_schedule_head (adevinfo_t *evinfo)

{
    struct { struct task_struct *prev, *next; } *evdata = (__typeof(evdata))evinfo->evdata;
    struct task_struct *next = evdata->next;
    adeos_declare_cpuid;
    int rootprio;

    adeos_propagate_event(evinfo);

    if (!nkpod || testbits(nkpod->status,XNPIDLE))
	return;

    adeos_load_cpuid();

    set_switch_lock_owner(current);

    if (xnshadow_thread(next))
	{
	rootprio = xnshadow_thread(next)->cprio;

#ifdef CONFIG_RTAI_OPT_DEBUG
        if (testbits(xnshadow_thread(next)->status,
		     XNTHREAD_BLOCK_BITS & ~XNRELAX))
            xnpod_fatal("blocked thread %s rescheduled?!",
			xnshadow_thread(next)->name);
#endif /* CONFIG_RTAI_OPT_DEBUG */

	engage_irq_shield(cpuid);
	}
    else if (next != gatekeeper[cpuid])
	    {
	    rootprio = XNPOD_ROOT_PRIO_BASE;
	    disengage_irq_shield(cpuid);
	    }
        else
	    return;

    /* Current Xenomai thread must be the root one in this context, so
       we can safely renice Xenomai's runthread (i.e. as returned by
       xnpod_current_thread()). */

    if (xnpod_current_thread()->cprio != rootprio)
        {
#if 1
        if (traceme)
            printk("RESET ROOT PRIO (old prio %d) TO %s's (prio %d), cpu %d\n",
                   xnpod_current_thread()->cprio,
                   xnshadow_thread(next)->name,
                   rootprio,
                   adeos_processor_id());
#endif
        xnpod_renice_root(rootprio);
        }
}

static void xnshadow_schedule_tail (adevinfo_t *evinfo)

{
    if (evinfo->domid == RTHAL_DOMAIN_ID)
        {
	/* About to resume in xnshadow_harden() after the gatekeeper
	   switched us back. Do _not_ propagate this event so that
	   Linux's tail scheduling won't be performed. */
	return;
	}

    adeos_propagate_event(evinfo);
}

static void xnshadow_kick_process (adevinfo_t *evinfo)

{
    struct { struct task_struct *task; } *evdata = (__typeof(evdata))evinfo->evdata;
    struct task_struct *task = evdata->task;
    xnthread_t *thread = xnshadow_thread(task);
    spl_t s;

    if (!thread || testbits(thread->status,XNRELAX|XNROOT))
	return;

    xnlock_get_irqsave(&nklock,s);

    if (thread == thread->sched->runthread)
	xnsched_set_resched(thread->sched);

    if (!testbits(thread->status,XNSTARTED))
	xnshadow_start(thread,0,NULL,NULL,0);
    else
	{
	if (xnpod_unblock_thread(thread))
	    __setbits(thread->status,XNKICKED);

	if (testbits(thread->status,XNSUSP))
	    {
	    xnpod_resume_thread(thread,XNSUSP);
	    __setbits(thread->status,XNKICKED);
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    xnshadow_schedule(); /* Schedule in the RTAI space. */
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

    xnpod_schedule(); /* We are already running into the RTAI domain. */
}

/*
 * xnshadow_register_skin() -- Register a new skin/interface.  NOTE: a
 * skin can be registered without its pod being necessarily active. In
 * such a case, a lazy initialization scheme can be implemented
 * through the event callback fired upon client attachment.
 */

int xnshadow_register_skin (const char *name,
			    unsigned magic,
			    int nrcalls,
			    xnsysent_t *systab,
			    int (*eventcb)(int))
{
    int muxid;
    spl_t s;

    /* We can only handle up to 256 syscalls per skin, check for over-
       and underflow (MKL). */

    if (XNARCH_MAX_SYSENT < nrcalls || 0 > nrcalls)
	return -EINVAL;

    xnlock_get_irqsave(&nklock,s);

    for (muxid = 0; muxid < XENOMAI_MUX_NR; muxid++)
	{
	if (muxtable[muxid].systab == NULL)
	    {
	    muxtable[muxid].name = name;
	    muxtable[muxid].systab = systab;
	    muxtable[muxid].nrcalls = nrcalls;
	    muxtable[muxid].magic = magic;
	    xnarch_atomic_set(&muxtable[muxid].refcnt,-1);
	    muxtable[muxid].eventcb = eventcb;

	    xnlock_put_irqrestore(&nklock,s);

	    return muxid + 1;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);
    
    return -ENOSPC;
}

/*
 * xnshadow_unregister_skin() -- Unregister a new skin/interface.
 * NOTE: a skin can be unregistered without its pod being necessarily
 * active.
 */

int xnshadow_unregister_skin (int muxid)

{
    int err = 0;
    spl_t s;

    if (--muxid < 0 || muxid >= XENOMAI_MUX_NR)
	return -EINVAL;

    xnlock_get_irqsave(&nklock,s);

    if (xnarch_atomic_get(&muxtable[muxid].refcnt) == -1)
	{
	muxtable[muxid].systab = NULL;
	muxtable[muxid].nrcalls = 0;
	muxtable[muxid].magic = 0;
	}
    else
	err = -EBUSY;

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

void xnshadow_grab_events (void)

{
    adeos_catch_event(ADEOS_EXIT_PROCESS,&xnshadow_linux_taskexit);
    adeos_catch_event(ADEOS_KICK_PROCESS,&xnshadow_kick_process);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,&xnshadow_schedule_head);
    adeos_catch_event_from(&rthal_domain,ADEOS_SCHEDULE_TAIL,&xnshadow_schedule_tail);
    adeos_catch_event_from(&rthal_domain,ADEOS_RENICE_PROCESS,&xnshadow_renice_process);
}

void xnshadow_release_events (void)

{
    adeos_catch_event(ADEOS_EXIT_PROCESS,NULL);
    adeos_catch_event(ADEOS_KICK_PROCESS,NULL);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,NULL);
    adeos_catch_event_from(&rthal_domain,ADEOS_SCHEDULE_TAIL,NULL);
    adeos_catch_event_from(&rthal_domain,ADEOS_RENICE_PROCESS,NULL);
}

int xnshadow_init (void)

{
    adattr_t attr;
    unsigned cpu;

    adeos_init_attr(&attr);
    attr.name = "IShield";
    attr.domid = 0x53484c44;
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
    shieldvirq = adeos_alloc_irq();
    adeos_virtualize_irq(shieldvirq,&xnshadow_shield_handler,NULL,IPIPE_HANDLE_MASK);

    init_MUTEX_LOCKED(&gksync);

    for (cpu = 0; cpu < num_online_cpus(); ++cpu)
        {
        init_MUTEX_LOCKED(&gkreq[cpu]);
        kernel_thread((void *)&gatekeeper_thread, (void *)(unsigned long) cpu,0);
        }

    for (cpu = 0; cpu < num_online_cpus(); ++cpu)
        down(&gksync);

    /* We need to grab these ones right now. */
    adeos_catch_event(ADEOS_SYSCALL_PROLOGUE,&xnshadow_linux_sysentry);
    adeos_catch_event_from(&rthal_domain,ADEOS_SYSCALL_PROLOGUE,&xnshadow_realtime_sysentry);

#ifdef CONFIG_SMP
    adeos_virtualize_irq(ADEOS_SERVICE_IPI1,
			 (void (*)(unsigned))&xnshadow_shield_handler,
			 NULL,
			 IPIPE_HANDLE_MASK);
#endif /* CONFIG_SMP */

    return 0;
}

void xnshadow_cleanup (void)

{
    unsigned cpu;

    gkstop = 1;

#ifdef CONFIG_SMP
    adeos_virtualize_irq(ADEOS_SERVICE_IPI1,
			 NULL,
			 NULL,
			 IPIPE_PASS_MASK);
#endif /* CONFIG_SMP */

    adeos_catch_event(ADEOS_SYSCALL_PROLOGUE,NULL);
    adeos_catch_event_from(&rthal_domain,ADEOS_SYSCALL_PROLOGUE,NULL);

    for (cpu = 0; cpu < num_online_cpus(); ++cpu)
        up(&gkreq[cpu]);
    
    for (cpu = 0; cpu < num_online_cpus(); ++cpu)
        down(&gksync);

    adeos_free_irq(gkvirq);
    adeos_free_irq(sigvirq);
    adeos_free_irq(nicevirq);
    adeos_free_irq(shieldvirq);
    adeos_free_ptdkey(nkgkptd);

    adeos_unregister_domain(&irq_shield);
}

/*@}*/

EXPORT_SYMBOL(xnshadow_harden);
EXPORT_SYMBOL(xnshadow_map);
EXPORT_SYMBOL(xnshadow_register_skin);
EXPORT_SYMBOL(xnshadow_relax);
EXPORT_SYMBOL(xnshadow_start);
EXPORT_SYMBOL(xnshadow_sync_post);
EXPORT_SYMBOL(xnshadow_ticks2ts);
EXPORT_SYMBOL(xnshadow_ticks2tv);
EXPORT_SYMBOL(xnshadow_ts2ticks);
EXPORT_SYMBOL(xnshadow_tv2ticks);
EXPORT_SYMBOL(xnshadow_unmap);
EXPORT_SYMBOL(xnshadow_unregister_skin);
