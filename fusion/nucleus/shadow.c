/*!\file shadow.c
 * \brief Real-time shadow services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The RTAI project <http://www.rtai.org>
 * Copyright (C) 2004 The HYADES project <http://www.hyades-itea.org>
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
#include <stdarg.h>
#include <asm/signal.h>
#include <linux/unistd.h>
#include <linux/wait.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/synch.h>
#include <nucleus/module.h>
#include <nucleus/shadow.h>
#include <nucleus/fusion.h>

int nkgkptd;

struct xnskentry muxtable[XENOMAI_MUX_NR];

static int traceme = 0;

static struct __gatekeeper {

    struct task_struct *server;
    wait_queue_head_t waitq;
    struct semaphore sync;
    xnthread_t *thread;

} gatekeeper[XNARCH_NR_CPUS];
 
static int gkstop;

static unsigned sbvirq;

static struct __schedback {

    int in, out;

    struct {
#define SB_WAKEUP_REQ 1
#define SB_SIGNAL_REQ 2
	int type;
	struct task_struct *task;
	int arg;
#define SB_MAX_REQUESTS 32 /* Must be a ^2 */
    } req[SB_MAX_REQUESTS];

} schedback[XNARCH_NR_CPUS];

static adomain_t irq_shield;

static cpumask_t shielded_cpus,
                 unshielded_cpus;

static rwlock_t shield_lock = RW_LOCK_UNLOCKED;

#define get_switch_lock_owner() \
switch_lock_owner[task_cpu(current)]

#define set_switch_lock_owner(t) \
do { \
   switch_lock_owner[task_cpu(t)] = t; \
} while(0)

static struct task_struct *switch_lock_owner[XNARCH_NR_CPUS];

void xnpod_declare_iface_proc(struct xnskentry *iface);

void xnpod_discard_iface_proc(struct xnskentry *iface);

static inline struct task_struct *get_calling_task (adevinfo_t *evinfo) {

    return (xnpod_shadow_p()
            ? current
            : rthal_get_root_current(xnarch_current_cpu()));
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

static inline void engage_irq_shield (void)

{
    unsigned long flags;
    adeos_declare_cpuid;

    adeos_lock_cpu(flags);

    if (cpu_test_and_set(cpuid,shielded_cpus))
	goto unmask_and_exit;

    adeos_read_lock(&shield_lock);

    cpu_clear(cpuid,unshielded_cpus);

    xnarch_lock_xirqs(&irq_shield,cpuid);

    adeos_read_unlock(&shield_lock);

 unmask_and_exit:
    
    adeos_unlock_cpu(flags);
}

static void disengage_irq_shield (void)
     
{
    unsigned long flags;
    adeos_declare_cpuid;

    adeos_lock_cpu(flags);

    if (cpu_test_and_set(cpuid,unshielded_cpus))
	goto unmask_and_exit;

    adeos_write_lock(&shield_lock);

    cpu_clear(cpuid,shielded_cpus);

    /* We want the shield to be either engaged on all CPUs (i.e. if at
       least one CPU asked for shielding), or disengaged on all
       (i.e. if no CPU asked for shielding). */

    if (!cpus_empty(shielded_cpus))
	{
	adeos_write_unlock(&shield_lock);
	goto unmask_and_exit;
	}

    /* At this point we know that we are the last CPU to disengage the
       shield, so we just unlock the external IRQs for all CPUs, and
       trigger an IPI on everyone but self to make sure that the
       remote interrupt logs will be played. We also forcibly unstall
       the shield stage on the local CPU in order to flush it the same
       way. */

    xnarch_unlock_xirqs(&irq_shield,cpuid);

#ifdef CONFIG_SMP
    {
    cpumask_t other_cpus = xnarch_cpu_online_map;
    cpu_clear(cpuid,other_cpus);
    adeos_send_ipi(ADEOS_SERVICE_IPI1,other_cpus);
    }
#endif /* CONFIG_SMP */

    adeos_write_unlock(&shield_lock);

    adeos_unstall_pipeline_from(&irq_shield);

 unmask_and_exit:

    adeos_unlock_cpu(flags);
}

static void shield_handler (unsigned irq)

{
#ifdef CONFIG_SMP
    if (irq != ADEOS_SERVICE_IPI1)
#endif /* CONFIG_SMP */
    adeos_propagate_irq(irq);
}

static void shield_entry (int iflag)

{
    if (iflag)
	xnarch_grab_xirqs(&shield_handler);

#if !defined(CONFIG_ADEOS_NOTHREADS)
    for (;;)
	adeos_suspend_domain();
#endif /* !CONFIG_ADEOS_NOTHREADS */
}

static void schedback_handler (unsigned virq)

{
    int cpuid = smp_processor_id(), reqnum;
    struct __schedback *sb = &schedback[cpuid];

    while ((reqnum = sb->out) != sb->in)
	{
	struct task_struct *task = sb->req[reqnum].task;
	sb->out = (reqnum + 1) & (SB_MAX_REQUESTS - 1);

	switch (sb->req[reqnum].type)
	    {
	    case SB_WAKEUP_REQ:

		if (xnshadow_thread(task) &&
		    testbits(xnshadow_thread(task)->status,XNSHIELD))
		    engage_irq_shield();

#ifdef CONFIG_SMP
		/* If the fusion task migrated while hardened, "migrate" its Linux
		   counter-part (migrating a suspended task is cheap) */
		if (!cpu_isset(cpuid, task->cpus_allowed))
		    set_cpus_allowed(task, cpumask_of_cpu(cpuid));
#endif /* CONFIG_SMP */

		wake_up_process(task);
		break;

	    case SB_SIGNAL_REQ:

		send_sig(sb->req[reqnum].arg,task,1);
		break;
	    }
	}
}

static void schedule_linux_call (int type,
				 struct task_struct *task,
				 int arg)
{
    int cpuid = smp_processor_id(), reqnum;
    struct __schedback *sb = &schedback[cpuid];
    spl_t s;

    splhigh(s);
    reqnum = sb->in;
    sb->req[reqnum].type = type;
    sb->req[reqnum].task = task;
    sb->req[reqnum].arg = arg;
    sb->in = (reqnum + 1) & (SB_MAX_REQUESTS - 1);
    splexit(s);

    /* Do _not_ use adeos_propagate_irq() here since we might need to
       schedule a command on behalf of the Linux domain. */

    adeos_schedule_irq(sbvirq);
}

static void itimer_handler (void *cookie)

{
    xnthread_t *thread = (xnthread_t *)cookie;
    struct task_struct *task = xnthread_archtcb(thread)->user_task;
    schedule_linux_call(SB_SIGNAL_REQ,task,SIGALRM);
}

static void gatekeeper_thread (void *data)

{
    unsigned cpu = (unsigned)(unsigned long)data;
    struct __gatekeeper *gk = &gatekeeper[cpu];
    struct task_struct *this_task = current;
    DECLARE_WAITQUEUE(wait,this_task);
    char name[32] = "gatekeeper";
    
    gk->server = this_task;

    sigfillset(&this_task->blocked);
    set_cpus_allowed(this_task, cpumask_of_cpu(cpu));
    rthal_set_linux_task_priority(this_task,SCHED_FIFO,MAX_USER_RT_PRIO - 1);

    init_waitqueue_head(&gk->waitq);
    add_wait_queue_exclusive(&gk->waitq,&wait);

#ifdef CONFIG_SMP
    sprintf(name,"gatekeeper/%u",cpu);
#endif /* CONFIG_SMP */
    daemonize(name);

    up(&gk->sync);	/* Sync with xnshadow_mount(). */

    for (;;)
        {
	set_current_state(TASK_INTERRUPTIBLE);
	up(&gk->sync); /* Make the request token available. */
	schedule();
	splnone();

	if (gkstop)
	    break;

#if 1
	if (traceme)
	    printk("__GK__[%u] %s (ipipe=%lu)\n",
		   cpu,gk->thread->name,
		   adeos_test_pipeline_from(&rthal_domain));
#endif

#ifdef CONFIG_SMP
	{
	spl_t s;
	/* If the fusion task migrated while run by Linux, migrate
	   the shadow too. */
	xnlock_get_irqsave(&nklock, s);
	gk->thread->sched = xnpod_sched_slot(cpu);
	xnpod_resume_thread(gk->thread,XNRELAX);
	xnlock_put_irqrestore(&nklock, s);
	}
#else /* !CONFIG_SMP */
	xnpod_resume_thread(gk->thread,XNRELAX);
#endif /* CONFIG_SMP */

	xnpod_renice_root(XNPOD_ROOT_PRIO_BASE);
	xnpod_schedule();
	}

    up(&gk->sync);	/* Sync whith xnshadow_cleanup(). */
}

/* timespec/timeval <-> ticks conversion routines -- Lifted and
  adapted from include/linux/time.h. */
 
unsigned long long xnshadow_ts2ticks (const struct timespec *v)
  
{
    u_long tickval = xnpod_get_tickval(), ticks;
    u_long hz = xnpod_get_ticks2sec(); /* hz == 1000000000/tickval */
    unsigned long long nsec;

    if (tickval != 1)
        {
        /* save a division: we add to nsec the worst remainder of the division
           of sec * 1e9 by tickval. nsec may not fit on 32 bits if v is not
           normalized. */
        nsec = v->tv_nsec + tickval - 1;
        /* the result is expected to fit on 32 bits, we can hence use uldiv
           instead of ulldiv. */
        ticks = xnarch_uldiv(nsec, tickval);
        }
    else
        ticks = v->tv_nsec;

    return xnarch_ullmul(hz, v->tv_sec) + ticks;
}

void xnshadow_ticks2ts (unsigned long long ticks, struct timespec *v)

{
    u_long rem_ticks, tickval = xnpod_get_tickval();
    u_long hz = xnpod_get_ticks2sec();
    v->tv_sec = xnarch_uldivrem(ticks, hz, &rem_ticks);
    v->tv_nsec = rem_ticks * tickval;
}

unsigned long long xnshadow_tv2ticks (const struct timeval *v)

{
    u_long tickval = xnpod_get_tickval();
    u_long hz = xnpod_get_ticks2sec(); /* hz == 1000000000/tickval */
    unsigned long long nsec, ticks;

    if (tickval != 1)
        {
        nsec = xnarch_ullmul(v->tv_usec,1000UL) + tickval - 1;
        /* If tickval is not 1, tickval is greater than 1000, so that 'ticks'
           fit on 32 bits and we can again use uldiv instead of ulldiv. */
        ticks = xnarch_uldiv(nsec, tickval);
        }
    else
        /* ticks may need 64 bits if v is not normalized. */
        ticks = xnarch_ullmul(v->tv_usec, 1000UL);

    return xnarch_ullmul(hz, v->tv_sec) + ticks;
}

void xnshadow_ticks2tv (unsigned long long ticks, struct timeval *v)

{
    u_long rem_ticks, tickval = xnpod_get_tickval();
    u_long hz = xnpod_get_ticks2sec();
    v->tv_sec = xnarch_uldivrem(ticks, hz, &rem_ticks);
    v->tv_usec = rem_ticks * tickval / 1000UL;
}

/*! 
 * @internal
 * \fn static int xnshadow_harden(void);
 * \brief Migrate a Linux task to the RTAI domain.
 *
 * This service causes the transition of "current" from the Linux
 * domain to RTAI. This is obtained by asking the gatekeeper to resume
 * the shadow mated with "current" then triggering the rescheduling
 * procedure in the RTAI domain. The shadow will resume in the RTAI
 * domain as returning from schedule().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space thread operating in secondary (i.e. relaxed) mode.
 *
 * Rescheduling: always.
 */

static int xnshadow_harden (void)

{
    struct task_struct *this_task = current;
    /* Linux is not allowed to migrate shadow mates on its own, and
       shadows cannot be migrated by anyone but themselves, so the cpu
       number is constant in this context, despite the potential for
       preemption. */
    struct __gatekeeper *gk = &gatekeeper[task_cpu(this_task)];

    if (signal_pending(this_task) ||
	down_interruptible(&gk->sync)) /* Grab the request token. */
	return -EINTR;

#if 1
    if (traceme)
	printk("_!HARDENING!_ %s, status 0x%lx, pid=%d (ipipe=%lu, domain=%s)\n",
	       xnshadow_thread(current)->name,
	       xnshadow_thread(current)->status,
	       current->pid,
	       adeos_test_pipeline_from(&rthal_domain),
	       adp_current->name);
#endif

    /* Set up the request to move "current" from the Linux domain to
       the RTAI domain. This will cause the shadow thread to resume
       using the register state of the current Linux task. For this to
       happen, we set up the migration data, prepare to suspend then
       wake up the gatekeeper which will perform the actual
       transition. */

    gk->thread = xnshadow_thread(this_task);
    wake_up_interruptible_sync(&gk->waitq);
    set_current_state(TASK_INTERRUPTIBLE);
    schedule();

#ifdef CONFIG_RTAI_OPT_DEBUG
    if (adp_current == adp_root)
	xnpod_fatal("wake_up_interruptible_sync() not so synchronous?!");
#endif /* CONFIG_RTAI_OPT_DEBUG */

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

    return 0;
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
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space thread operating in primary (i.e. harden) mode.
 *
 * Rescheduling: always.
 *
 * @note "current" is valid here since the shadow runs with the
 * properties of the Linux task.
 */

void xnshadow_relax (void)

{
    xnthread_t *thread = xnpod_current_thread();

    /* Enqueue the request to move the running shadow from the RTAI
       domain to the Linux domain.  This will cause the Linux task
       to resume using the register state of the shadow thread. */

#if 1
    if (traceme)
	printk("_!RELAXING!_ %s, status 0x%lx, pid=%d (ipipe=%lu, domain=%s)\n",
	       thread->name,
	       thread->status,
	       current->pid,
	       adeos_test_pipeline_from(&rthal_domain),
	       adp_current->name);
#endif

    if (current->state & TASK_UNINTERRUPTIBLE)
	/* Just to avoid wrecking Linux's accounting of non-
	   interruptible tasks, move back kicked tasks to
	   interruptible state, like schedule() saw them initially. */
	set_current_state((current->state&~TASK_UNINTERRUPTIBLE)|TASK_INTERRUPTIBLE);

    schedule_linux_call(SB_WAKEUP_REQ,current,0);

    xnpod_renice_root(thread->cprio);
    xnpod_suspend_thread(thread,XNRELAX,XN_INFINITE,NULL);

    __adeos_schedule_back_root(get_switch_lock_owner());

    if (current->rt_priority != thread->cprio)
      rthal_set_linux_task_priority(current,SCHED_FIFO,thread->cprio);

    /* "current" is now running into the Linux domain on behalf of the
       root thread. */
#if 1
    if (traceme)
	printk("__RELAX__ %s (on %s, status 0x%lx), pid=%d (ipipe=%lu, domain=%s)\n",
	       thread->name,
	       xnpod_current_sched()->runthread->name,
	       xnpod_current_sched()->runthread->status,
	       current->pid,
	       adeos_test_pipeline_from(&rthal_domain),
	       adp_current->name);
#endif
}

void xnshadow_sync_post (pid_t syncpid, int __user *u_syncp, int err)

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

static int xnshadow_sync_wait (int __user *u_syncp)

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
			 int __user *u_syncp);
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
 * Environments:
 *
 * This service can be called from:
 *
 * - Regular user-space process. 
 *
 * Rescheduling: always.
 *
 */

void xnshadow_map (xnthread_t *thread,
                   pid_t syncpid,
                   int __user *u_syncp)
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
    rthal_set_linux_task_priority(current,SCHED_FIFO,xnthread_base_priority(thread));
    xnshadow_ptd(current) = thread;
    xnpod_suspend_thread(thread,XNRELAX,XN_INFINITE,NULL);

#if 1
    if (traceme)
        printk("__MAP__ %s from %s, prio=%d, pid=%d, domain=%s, autostart=%d, pid=%d\n",
               xnthread_name(thread),
               xnpod_current_sched()->runthread->name,
               xnthread_base_priority(thread),
               current->pid,
               adp_current->name,
               autostart,
               current->pid);
#endif

   if (autostart)
       {
       xnshadow_start(thread,NULL,NULL);
       xnshadow_harden();
       }
   else
       xnshadow_sync_post(syncpid,u_syncp,0);
}

void xnshadow_unmap (xnthread_t *thread)

{
    struct task_struct *task;
    unsigned muxid, magic;

#ifdef CONFIG_RTAI_OPT_DEBUG
    if (!testbits(xnpod_current_sched()->status,XNKCOUT))
	xnpod_fatal("xnshadow_unmap() called from invalid context");
#endif /* CONFIG_RTAI_OPT_DEBUG */

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

    if (task->state != TASK_RUNNING)
	/* If the shadow is being unmapped in primary mode, the
	   associated Linux task should also die. The zombie Linux
	   side returning to user-space will be trapped and exited
	   inside the pod's rescheduling routines. */
	schedule_linux_call(SB_WAKEUP_REQ,task,0);
    else
	/* Otherwise, if the shadow is being unmapped in secondary
	   mode, we only detach the shadow thread from its Linux mate,
	   and renice the root thread appropriately. We do not
	   reschedule since xnshadow_unmap() must be called from a
	   thread deletion hook. */
	xnpod_renice_root(XNPOD_ROOT_PRIO_BASE);
}

int xnshadow_wait_barrier (struct pt_regs *regs)

{
    xnthread_t *thread = xnshadow_thread(current);
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (testbits(thread->status,XNSTARTED))
	{
	/* Already done -- no op. */
	xnlock_put_irqrestore(&nklock,s);
	goto release_task;
	}

    /* We must enter this call on behalf of the Linux domain. */
    set_current_state(TASK_INTERRUPTIBLE);
    xnlock_put_irqrestore(&nklock,s);

    schedule();

    if (!testbits(thread->status,XNSTARTED))
	return -EINTR;

 release_task:

    __xn_copy_to_user(task,
		      (void __user *)__xn_reg_arg1(regs),
		      &thread->entry,
		      sizeof(thread->entry));

    __xn_copy_to_user(task,
		      (void __user *)__xn_reg_arg2(regs),
		      &thread->cookie,
		      sizeof(thread->cookie));

    return xnshadow_harden();
}

void xnshadow_start (xnthread_t *thread,
                     void (*u_entry)(void *cookie),
                     void *u_cookie)
{
    struct task_struct *task;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    __setbits(thread->status,XNSTARTED);
    thread->imask = 0;
    thread->imode = 0;
    thread->entry = u_entry;    /* user-space pointer -- do not deref. */
    thread->cookie = u_cookie;  /* ditto. */
    thread->stime = xnarch_get_cpu_time();
    thread->affinity = xnarch_cpumask_of_cpu(smp_processor_id());

    if (testbits(thread->status,XNRRB))
        thread->rrcredit = thread->rrperiod;

    xntimer_init(&thread->atimer,&itimer_handler,thread);

    xnpod_resume_thread(thread,XNDORMANT);

    task = xnthread_archtcb(thread)->user_task;

#if 1
    if (traceme)
        printk("__START__ %s (status=0x%lx), prio=%d, pid=%d, domain=%s, entry=%p\n",
               thread->name,
               thread->status,
               xnthread_base_priority(thread),
               task->pid,
               adp_current->name,
               u_entry);
#endif

   xnlock_put_irqrestore(&nklock,s);

   if (task->state == TASK_INTERRUPTIBLE)
       /* Wakeup the Linux mate waiting on the barrier. */
       schedule_linux_call(SB_WAKEUP_REQ,task,0);
}

void xnshadow_renice (xnthread_t *thread)

{
  /* Called with nklock locked, RTAI interrupts off. */

    if (xnpod_root_p()) {
	struct task_struct *task = xnthread_archtcb(thread)->user_task;
	int prio = thread->cprio;
	spl_t s;
	rthal_local_irq_sync(s);
	rthal_set_linux_task_priority(task,SCHED_FIFO,prio);
	rthal_local_irq_restore(s);
    }
}

static int attach_to_interface (struct task_struct *curr,
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
	    /* Increment the reference count now (actually, only the
	       first call to attach_to_interface() really increments
	       the counter), so that the interface cannot be removed
	       under our feet. */

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

    return -ESRCH;
}

static int detach_from_interface (struct task_struct *curr, int muxid)

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
       and delete them. */

    nholder = getheadq(&nkpod->threadq);

    while ((holder = nholder) != NULL)
	{
	nholder = nextq(&nkpod->threadq,holder);
	thread = link2thread(holder,glink);

	if (xnthread_get_magic(thread) == muxtable[muxid].magic)
	    xnpod_delete_thread(thread);
	}

    xnlock_put_irqrestore(&nklock,s);

    return 0;
}

static int substitute_linux_syscall (struct task_struct *curr,
				     struct pt_regs *regs)
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

#if CONFIG_RTAI_HW_APERIODIC_TIMER
	    if (!testbits(nkpod->status,XNTMPER))
		expire = xnpod_get_cpu_time();
	    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
		expire = nkpod->jiffies;

	    delay = xnshadow_ts2ticks(&t);
	    expire += delay;

	    if (!xnpod_shadow_p() && xnshadow_harden() == -EINTR)
		{
		__xn_error_return(regs,-ERESTARTSYS);
		goto intr;
		}

	    __xn_success_return(regs,-1);

	    if (delay > 0)
		{
		xnpod_delay(delay);

		if (signal_pending(curr))
		    {
		    __xn_error_return(regs,-EINTR);
		    request_syscall_restart(thread,regs);
		    }
		}
intr:

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

		    xnshadow_ticks2ts(expire - now,&t);
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
		return 0;

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

static void exec_nucleus_syscall (int muxop, struct pt_regs *regs)

{
    /* Called on behalf of the root thread. */

    switch (muxop)
	{
	case __xn_sys_sync:

	    __xn_status_return(regs,xnshadow_sync_wait((int *)__xn_reg_arg1(regs)));
	    break;

	case __xn_sys_migrate:

	    if (xnshadow_harden() == -EINTR)
		__xn_error_return(regs,-ERESTARTSYS);
	    else
		__xn_success_return(regs,1);

	    break;

	case __xn_sys_barrier:

	    __xn_status_return(regs,xnshadow_wait_barrier(regs));
	    break;

	case __xn_sys_attach:

	    __xn_status_return(regs,
                               attach_to_interface(current,
						   __xn_reg_arg1(regs),
						   __xn_reg_arg2(regs)));
	    break;
		
	case __xn_sys_detach:
	    
	    __xn_status_return(regs,
                               detach_from_interface(current,
						     __xn_reg_arg1(regs)));
	    break;

	default:

	    printk(KERN_WARNING "RTAI[nucleus]: Unknown nucleus syscall #%d\n",muxop);
	}
}

static void rtai_sysentry (adevinfo_t *evinfo)

{
    struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
    struct task_struct *task;
    xnthread_t *thread;
    int muxid, muxop;
    u_long sysflags;

    if (nkpod && !testbits(nkpod->status,XNPIDLE))
	goto nucleus_loaded;

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

 nucleus_loaded:

    task = get_calling_task(evinfo);
    thread = xnshadow_thread(task);

    if (__xn_reg_mux_p(regs))
	goto xenomai_syscall;

    if (xnpod_root_p())
	/* The call originates from the Linux domain, either from a
	   relaxed shadow or from a regular Linux task; just propagate
	   the event so that we will fall back to linux_sysentry(). */
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

    if (substitute_linux_syscall(task,regs))
	/* This is a Linux syscall issued on behalf of a shadow thread
	   running inside the RTAI domain. This call has just been
	   intercepted by the nucleus and a RTAI replacement has been
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
	case __xn_sys_migrate:

	    if (!thread)	/* Not a shadow anyway. */
		__xn_success_return(regs,-EPERM);
	    else if (__xn_reg_arg1(regs) == FUSION_RTAI_DOMAIN) /* Linux => RTAI */
		{
		if (!xnthread_test_flags(thread,XNRELAX))
		    __xn_success_return(regs,0);
		else
		    /* Migration to RTAI from the Linux domain must be
		       done from the latter: propagate the request to
		       the Linux-level handler. */
		    adeos_propagate_event(evinfo);
		}
	    else if (__xn_reg_arg1(regs) == FUSION_LINUX_DOMAIN) /* RTAI => Linux */
		{
		if (xnthread_test_flags(thread,XNRELAX))
		    __xn_success_return(regs,0);
		else
		    {
		    __xn_success_return(regs,1);
		    xnshadow_relax();
		    }
		}
	    else
		__xn_error_return(regs,-EINVAL);

	    return;

	case __xn_sys_attach:
	case __xn_sys_detach:
	case __xn_sys_sync:
	case __xn_sys_barrier:

	    /* If called from RTAI, switch to secondary mode then run
	     * the internal syscall afterwards. If called from Linux,
	     * propagate the event so that linux_sysentry() will catch
	     * it and run the syscall from there. We need to play this
	     * trick here and at a few other locations because Adeos
	     * will propagate events down the pipeline up to (and
	     * including) the calling domain itself, so if RTAI is the
	     * original caller, there is no way Linux can receive the
	     * syscall from propagation because Adeos won't cross the
	     * boundary delimited by the calling RTAI stage for this
	     * particular syscall instance. If the latter is still
	     * unclear in your mind, have a look at the
	     * adeos_catch_event() documentation and get back to this
	     * later. */

	    if (evinfo->domid == RTHAL_DOMAIN_ID)
	        {
		xnshadow_relax();
		exec_nucleus_syscall(muxop,regs);
		return;
		}

	    /* Falldown wanted. */

 propagate_syscall:

	    /* Delegate the syscall handling to the Linux domain. */
	    adeos_propagate_event(evinfo);
	    return;

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

    if ((sysflags & __xn_exec_shadow) != 0 && !thread)
	{
	__xn_error_return(regs,-EPERM);
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

    if ((sysflags & __xn_exec_lostage) != 0)
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
    else if ((sysflags & __xn_exec_histage) != 0)
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

static void linux_sysentry (adevinfo_t *evinfo)

{
    struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
    xnthread_t *thread = xnshadow_thread(current);
    int muxid, muxop;

    if (__xn_reg_mux_p(regs))
	goto xenomai_syscall;

    if (thread && substitute_linux_syscall(current,regs))
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
       the Linux domain (over which we are currently running). */

    exec_nucleus_syscall(muxop,regs);

    return;

 skin_syscall:

    if ((muxtable[muxid - 1].systab[muxop].flags & __xn_exec_histage) != 0)
	{
	/* This request originates from the Linux domain and must be
	   run into the RTAI domain: harden the caller and execute the
	   syscall. */
	if (xnshadow_harden() == -EINTR)
	    {
	    __xn_error_return(regs,-ERESTARTSYS);
	    return;
	    }
	}

    __xn_status_return(regs,muxtable[muxid - 1].systab[muxop].svc(current,regs));

    if (xnpod_shadow_p() && signal_pending(current))
	request_syscall_restart(xnshadow_thread(current),regs);
}

static void linux_task_exit (adevinfo_t *evinfo)

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

    xnshadow_ptd(current) = NULL;
    xnthread_archtcb(thread)->user_task = NULL;
    xnpod_delete_thread(thread);

#if 1
    if (traceme)
	printk("Cleaned up %s\n",thread->name);
#endif
}

static inline void __xnshadow_reset_shield (xnthread_t *thread)

{
    if (testbits(thread->status,XNSHIELD))
	engage_irq_shield();
    else
	disengage_irq_shield();
}

void xnshadow_reset_shield (void)

{
    xnthread_t *thread = xnshadow_thread(current);

    if (!thread)
	return; /* uh?! */

    __xnshadow_reset_shield(thread);
}

static void linux_schedule_head (adevinfo_t *evinfo)

{
    struct { struct task_struct *prev, *next; } *evdata = (__typeof(evdata))evinfo->evdata;
    struct task_struct *next = evdata->next;
    xnthread_t *thread = xnshadow_thread(next);
    int oldrprio, newrprio;
    adeos_declare_cpuid;

    adeos_propagate_event(evinfo);

    if (!nkpod || testbits(nkpod->status,XNPIDLE))
	return;

    adeos_load_cpuid();	/* Linux is running in a migration-safe
			   portion of code. */

    set_switch_lock_owner(current);

    if (thread)
	{
	newrprio = thread->cprio;

#ifdef CONFIG_RTAI_OPT_DEBUG
        if (testbits(thread->status,XNTHREAD_BLOCK_BITS & ~XNRELAX))
            xnpod_fatal("blocked thread %s rescheduled?!",thread->name);
#endif /* CONFIG_RTAI_OPT_DEBUG */

	__xnshadow_reset_shield(thread);
	}
    else if (next != gatekeeper[cpuid].server)
	    {
	    newrprio = XNPOD_ROOT_PRIO_BASE;
	    disengage_irq_shield();
	    }
        else
	    return;

    /* Current nucleus thread must be the root one in this context, so
       we can safely renice the nucleus's runthread (i.e. as returned
       by xnpod_current_thread()). */

    oldrprio = xnpod_current_thread()->cprio;

    if (oldrprio != newrprio)
        {
#if 1
        if (traceme)
            printk("RESET ROOT PRIO (old prio %d) TO %s's (prio %d), cpu %d\n",
                   oldrprio,
                   thread->name,
                   newrprio,
                   adeos_processor_id());
#endif
        xnpod_renice_root(newrprio);

	if (xnpod_priocompare(newrprio,oldrprio) < 0)
	    /* Subtle: by downgrading the root thread priority, some
	       higher priority thread might well become eligible for
	       execution instead of us. Since xnpod_renice_root() does
	       not reschedule (and must _not_ in most of other cases),
	       let's call the rescheduling procedure ourselves. */
            xnpod_schedule();
        }
}

static void linux_schedule_tail (adevinfo_t *evinfo)

{
    if (evinfo->domid == RTHAL_DOMAIN_ID)
	/* About to resume in xnshadow_harden() after the gatekeeper
	   switched us back. Do _not_ propagate this event so that
	   Linux's tail scheduling won't be performed. */
	return;

    adeos_propagate_event(evinfo);
}

static void linux_kick_process (adevinfo_t *evinfo)

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

    if (xnpod_unblock_thread(thread))
	setbits(thread->status,XNKICKED);

    if (testbits(thread->status,XNSUSP))
	{
	xnpod_resume_thread(thread,XNSUSP);
	setbits(thread->status,XNKICKED);
	}

    /* If we are kicking a shadow thread, make sure Linux won't
       schedule in its mate under our feet as a result of running
       signal_wake_up(). The RTAI scheduler must remain in control for
       now, until we explicitely relax the shadow thread to allow for
       processing the pending signals. Make sure we keep the
       additional state flags unmodified so that we don't break any
       undergoing ptrace. */

    if (task->state & TASK_INTERRUPTIBLE)
	set_task_state(task,(task->state&~TASK_INTERRUPTIBLE)|TASK_UNINTERRUPTIBLE);

    xnpod_schedule();

    xnlock_put_irqrestore(&nklock,s);
}

static void linux_renice_process (adevinfo_t *evinfo)

{
    struct {
	struct task_struct *task;
	int policy;
	struct sched_param *param;
    } *evdata;
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

    if (thread->cprio != evdata->param->sched_priority)
	xnpod_renice_thread_inner(thread,evdata->param->sched_priority,0);

    if (current == evdata->task && thread->cprio != xnpod_current_root()->cprio)
	xnpod_renice_root(thread->cprio);

    if (xnsched_resched_p())
	xnpod_schedule();
}

/*
 * xnshadow_register_interface() -- Register a new skin/interface.
 * NOTE: an interface can be registered without its pod being
 * necessarily active. In such a case, a lazy initialization scheme
 * can be implemented through the event callback fired upon the first
 * client attachment.
 */

int xnshadow_register_interface (const char *name,
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

#ifdef CONFIG_PROC_FS
	    xnpod_declare_iface_proc(muxtable + muxid);
#endif /* CONFIG_PROC_FS */

	    return muxid + 1;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);
    
    return -ENOSPC;
}

/*
 * xnshadow_unregister_interface() -- Unregister a new skin/interface.
 * NOTE: an interface can be unregistered without its pod being
 * necessarily active.
 */

int xnshadow_unregister_interface (int muxid)

{
    int err = 0;
    spl_t s;

    if (--muxid < 0 || muxid >= XENOMAI_MUX_NR)
	return -EINVAL;

    xnlock_get_irqsave(&nklock,s);

    if (xnarch_atomic_get(&muxtable[muxid].refcnt) <= 0)
	{
	muxtable[muxid].systab = NULL;
	muxtable[muxid].nrcalls = 0;
	muxtable[muxid].magic = 0;
	xnarch_atomic_set(&muxtable[muxid].refcnt,-1);
#ifdef CONFIG_PROC_FS
	xnpod_discard_iface_proc(muxtable + muxid);
#endif /* CONFIG_PROC_FS */
	}
    else
	err = -EBUSY;

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

void xnshadow_grab_events (void)

{
    adeos_catch_event(ADEOS_EXIT_PROCESS,&linux_task_exit);
    adeos_catch_event(ADEOS_KICK_PROCESS,&linux_kick_process);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,&linux_schedule_head);
    adeos_catch_event_from(&rthal_domain,ADEOS_SCHEDULE_TAIL,&linux_schedule_tail);
    adeos_catch_event_from(&rthal_domain,ADEOS_RENICE_PROCESS,&linux_renice_process);
}

void xnshadow_release_events (void)

{
    adeos_catch_event(ADEOS_EXIT_PROCESS,NULL);
    adeos_catch_event(ADEOS_KICK_PROCESS,NULL);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,NULL);
    adeos_catch_event_from(&rthal_domain,ADEOS_SCHEDULE_TAIL,NULL);
    adeos_catch_event_from(&rthal_domain,ADEOS_RENICE_PROCESS,NULL);
}

int xnshadow_mount (void)

{
    adattr_t attr;
    unsigned cpu;

    adeos_init_attr(&attr);
    attr.name = "IShield";
    attr.domid = 0x53484c44;
    attr.entry = &shield_entry;
    attr.priority = ADEOS_ROOT_PRI + 50;

    if (adeos_register_domain(&irq_shield,&attr))
	return -EBUSY;

    shielded_cpus = CPU_MASK_NONE;
    unshielded_cpus = xnarch_cpu_online_map;

    nkgkptd = adeos_alloc_ptdkey();
    sbvirq = adeos_alloc_irq();
    adeos_virtualize_irq(sbvirq,&schedback_handler,NULL,IPIPE_HANDLE_MASK);

    for (cpu = 0; cpu < num_online_cpus(); ++cpu)
	{
	struct __gatekeeper *gk = &gatekeeper[cpu];
	init_MUTEX_LOCKED(&gk->sync);
	xnarch_memory_barrier();
        kernel_thread((void *)&gatekeeper_thread, (void *)(unsigned long) cpu,0);
        down(&gk->sync);
	}

    /* We need to grab these ones right now. */
    adeos_catch_event(ADEOS_SYSCALL_PROLOGUE,&linux_sysentry);
    adeos_catch_event_from(&rthal_domain,ADEOS_SYSCALL_PROLOGUE,&rtai_sysentry);

    return 0;
}

void xnshadow_cleanup (void)

{
    unsigned cpu;

    gkstop = 1;

    adeos_catch_event(ADEOS_SYSCALL_PROLOGUE,NULL);
    adeos_catch_event_from(&rthal_domain,ADEOS_SYSCALL_PROLOGUE,NULL);

    for (cpu = 0; cpu < num_online_cpus(); ++cpu)
	{
	struct __gatekeeper *gk = &gatekeeper[cpu];
        down(&gk->sync);
	wake_up_interruptible_sync(&gk->waitq);
        down(&gk->sync);
	}

    adeos_free_irq(sbvirq);
    adeos_free_ptdkey(nkgkptd);

    adeos_unregister_domain(&irq_shield);
}

/*@}*/

EXPORT_SYMBOL(xnshadow_map);
EXPORT_SYMBOL(xnshadow_register_interface);
EXPORT_SYMBOL(xnshadow_relax);
EXPORT_SYMBOL(xnshadow_start);
EXPORT_SYMBOL(xnshadow_sync_post);
EXPORT_SYMBOL(xnshadow_ticks2ts);
EXPORT_SYMBOL(xnshadow_ticks2tv);
EXPORT_SYMBOL(xnshadow_ts2ticks);
EXPORT_SYMBOL(xnshadow_tv2ticks);
EXPORT_SYMBOL(xnshadow_unmap);
EXPORT_SYMBOL(xnshadow_unregister_interface);
EXPORT_SYMBOL(xnshadow_wait_barrier);
EXPORT_SYMBOL(nkgkptd);
