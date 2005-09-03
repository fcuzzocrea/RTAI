/*!\file pod.c
 * \brief Real-time pod services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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
 * \ingroup pod
 */

/*!
 * \ingroup nucleus
 * \defgroup pod Real-time pod services.
 *
 * Real-time pod services.
 *@{*/

#define XENO_POD_MODULE 1

#include <stdarg.h>
#include <nucleus/pod.h>
#include <nucleus/timer.h>
#include <nucleus/synch.h>
#include <nucleus/heap.h>
#include <nucleus/intr.h>
#include <nucleus/module.h>
#include <nucleus/ltt.h>

/* NOTE: We need to initialize the globals: remember that this code
   also runs over user-space VMs... */

xnpod_t *nkpod = NULL;

#ifdef CONFIG_SMP
xnlock_t nklock = XNARCH_LOCK_UNLOCKED;
#endif /* CONFIG_SMP */

u_long nkschedlat = 0;

u_long nktimerlat = 0;

char *nkmsgbuf = NULL;

const char *xnpod_fatal_helper (const char *format, ...)

{
    const unsigned nr_cpus = xnarch_num_online_cpus();
    xnholder_t *holder;
    char *p = nkmsgbuf;
    xnticks_t now;
    unsigned cpu;
    va_list ap;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    va_start(ap,format);
    p += vsnprintf(p,XNPOD_FATAL_BUFSZ,format,ap);
    va_end(ap);

    if (!nkpod || testbits(nkpod->status,XNFATAL|XNPIDLE))
        goto out;

    setbits(nkpod->status,XNFATAL);
    now = nktimer->get_jiffies();

    p += snprintf(p,XNPOD_FATAL_BUFSZ - (p - nkmsgbuf),
	          "\n %-3s  %-6s %-4s %-8s %-8s  %s\n",
		  "CPU","PID","PRI","TIMEOUT","STAT","NAME");

    for (cpu = 0; cpu < nr_cpus; ++cpu)
        {
        xnsched_t *sched = xnpod_sched_slot(cpu);

        holder = getheadq(&nkpod->threadq);

        while (holder)
	    {
	    xnthread_t *thread = link2thread(holder,glink);
            holder = nextq(&nkpod->threadq,holder);

            if (thread->sched != sched)
                continue;

	    p += snprintf(p,XNPOD_FATAL_BUFSZ - (p - nkmsgbuf),
			  "%c%3u  %-6d %-4d %-8Lu %.8lx  %s\n",
			  thread == sched->runthread ? '>' : ' ',
			  cpu,
			  xnthread_user_pid(thread),
			  thread->cprio,
			  xnthread_get_timeout(thread, now),
			  thread->status,
			  thread->name);
            }
        }

    if (testbits(nkpod->status,XNTIMED))
	p += snprintf(p,XNPOD_FATAL_BUFSZ - (p - nkmsgbuf),
		      "Timer: %s [tickval=%lu ns, elapsed=%Lu]\n",
		      nktimer->get_type(),
		      xnpod_get_tickval(),
		      nktimer->get_jiffies());
    else
        p += snprintf(p,XNPOD_FATAL_BUFSZ - (p - nkmsgbuf),
		      "Timer: none\n");
 out:

    xnlock_put_irqrestore(&nklock,s);

    return nkmsgbuf;
}

/*
 * xnpod_fault_handler -- The default fault handler.
 */

static int xnpod_fault_handler (xnarch_fltinfo_t *fltinfo)

{
    xnthread_t *thread = xnpod_current_thread();

    xnltt_log_event(rtai_ev_fault,
                    thread->name,
                    xnarch_fault_pc(fltinfo),
                    xnarch_fault_trap(fltinfo));

#ifdef __KERNEL__
    if (xnarch_fault_fpu_p(fltinfo))
        {
#if defined(CONFIG_RTAI_OPT_FUSION) && defined(CONFIG_RTAI_HW_FPU)
        xnarchtcb_t *tcb = xnthread_archtcb(thread);
        
        if (xnpod_shadow_p() && !xnarch_fpu_init_p(tcb->user_task))
            {
            /* The faulting task is a shadow using the FPU for the first time,
               initialize its FPU. Of course if RTAI is not compiled with
               support for FPU, such use of the FPU is an error. */
            xnarch_init_fpu(tcb);
            return 1;
            }
#endif /* OPT_FUSION && HW_FPU */

        print_symbol("RTAI: Invalid use of FPU in RTAI context at %s\n",
                     xnarch_fault_pc(fltinfo));
        }
#endif /* __KERNEL__ */

    if (!xnpod_userspace_p())
        {
        xnprintf("RTAI: suspending kernel thread %p ('%s') at 0x%lx after exception #%u\n",
                 thread,
                 thread->name,
                 xnarch_fault_pc(fltinfo),
                 xnarch_fault_trap(fltinfo));

        xnpod_suspend_thread(thread,XNSUSP,XN_INFINITE,NULL);

        return 1;
        }

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* If we experienced a trap on behalf of a shadow thread, just
       move the second to the Linux domain, so that the host O/S
       (e.g. Linux) can attempt to process the exception. This is
       especially useful in order to handle user-space errors or debug
       stepping properly. */

    if (xnpod_shadow_p())
        {
#ifdef CONFIG_RTAI_OPT_DEBUG
        if (xnarch_fault_notify(fltinfo)) /* Don't report debug traps */
            xnprintf("Switching %s to secondary mode after exception #%u from "
                     "user-space at 0x%lx (pid %d)\n",
                     thread->name,
                     xnarch_fault_trap(fltinfo),
                     xnarch_fault_pc(fltinfo),
		     xnthread_user_pid(thread));
#endif /* CONFIG_RTAI_OPT_DEBUG */
	if (xnarch_fault_pf_p(fltinfo))
	    /* The page fault counter is not SMP-safe, but it's a
	       simple indicator that something went wrong wrt memory
	       locking anyway. */
	    xnthread_inc_pf(thread);

        xnshadow_relax(xnarch_fault_notify(fltinfo));
        }
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    return 0;
}

void xnpod_schedule_handler (void)

{
    xnltt_log_event(rtai_ev_smpsched);
    xnsched_set_resched(xnpod_current_sched());
    xnpod_schedule();
}

/*! 
 * \fn int xnpod_init(xnpod_t *pod,int minpri,int maxpri,xnflags_t flags)
 * \brief Initialize a new pod.
 *
 * Initializes a new pod which can subsequently be used to start
 * real-time activities. Once a pod is active, real-time APIs can be
 * stacked over. There can only be a single pod active in the host
 * environment. Such environment can be confined to a process though
 * (e.g. simulator or VM layers), or expand to the whole system
 * (e.g. Adeos or RTAI).
 *
 * @param pod The address of a pod descriptor the nucleus will use to
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
 * nucleus that the (xnsynch_t) pend queue should not be reordered
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
 * @return 0 is returned on success. Otherwise:
 *
 * - -EBUSY is returned if a pod already exists. As a special
 * exception, if the fusion pod is currently loaded with no active
 * attachment onto it, it is forcibly unloaded and replaced by the new
 * pod.
 *
 * - -ENOMEM is returned if the memory manager fails to initialize.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization code
 *
 * @note No initialization code called by this routine may refer to
 * the global "nkpod" pointer.
 */

int xnpod_init (xnpod_t *pod, int minpri, int maxpri, xnflags_t flags)

{
    unsigned cpu, nr_cpus = xnarch_num_online_cpus();
    char root_name[16];
    xnsched_t *sched;
    void *heapaddr;
    int rc, n;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (nkpod != NULL)
        {
        /* Avoid trying to shutdown the pod when it is not valid. */
        if (!testbits(pod->status, XNPIDLE) ||

            /* In case a pod is already active, ask for removal via a call
               to the unload hook if any. Otherwise, the operation has
               failed. */
            !nkpod->svctable.unload || nkpod->svctable.unload() <= 0)
            {
            xnlock_put_irqrestore(&nklock, s);
            return -EBUSY;
            }
        }

    if (minpri > maxpri)
        /* The lower the value, the higher the priority */
        flags |= XNRPRIO;

    /* Flags must be set before xnpod_get_qdir() is called */
    pod->status = (flags & (XNRPRIO|XNDREORD))|XNPIDLE;

    initq(&xnmod_glink_queue);
    initq(&pod->threadq);
    initq(&pod->tstartq);
    initq(&pod->tswitchq);
    initq(&pod->tdeleteq);

    for (cpu = 0; cpu < xnarch_num_online_cpus(); cpu++)
        for (n = 0; n < XNTIMER_WHEELSIZE; n++)
            initq(&pod->sched[cpu].timerwheel[n]);

    xnarch_atomic_set(&pod->schedlck,0);
    pod->minpri = minpri;
    pod->maxpri = maxpri;
    pod->jiffies = 0;
    pod->wallclock_offset = 0;
    pod->tickvalue = XNARCH_DEFAULT_TICK;
    pod->ticks2sec = 1000000000/ XNARCH_DEFAULT_TICK;
    pod->refcnt = 0;
    xnarch_atomic_set(&pod->timerlck,0);

    pod->svctable.settime = &xnpod_set_time;
    pod->svctable.faulthandler = &xnpod_fault_handler;
    pod->svctable.unload = NULL;
#ifdef __RTAI_SIM__
    pod->schedhook = NULL;
#endif /* __RTAI_SIM__ */

    initq(&pod->suspendq);

    for (cpu=0; cpu < nr_cpus; ++cpu)
        {
        sched = &pod->sched[cpu];
        sched_initpq(&sched->readyq,xnpod_get_qdir(pod),xnpod_get_maxprio(pod,0));
        sched->status = 0;
        sched->inesting = 0;
        sched->runthread = NULL;
        }

    pod->root_prio_base = xnpod_get_minprio(pod,1);

    /* The global "nkpod" pointer must be valid in order to perform
       the remaining operations. */

    nkpod = pod;

    /* No direct handler here since the host timer processing is
       postponed to xnintr_irq_handler(), as part of the interrupt
       exit code. */
    xntimer_init(&pod->htimer,NULL,NULL);
    xntimer_set_priority(&pod->htimer,XNTIMER_LOPRIO);

    xnlock_put_irqrestore(&nklock,s);

    heapaddr = xnarch_sysalloc(XNPOD_HEAPSIZE);

    if (!heapaddr ||
        xnheap_init(&kheap,heapaddr,XNPOD_HEAPSIZE,XNPOD_PAGESIZE) != 0)
        {
        rc = -ENOMEM;
        goto fail;
        }

    for (cpu = 0; cpu < nr_cpus; ++cpu)
        {
        sched = xnpod_sched_slot(cpu);
#ifdef CONFIG_SMP
        sprintf(root_name,"ROOT/%u",cpu);
#else /* !CONFIG_SMP */
        sprintf(root_name,"ROOT");
#endif /* CONFIG_SMP */

        xnsched_clr_mask(sched);

        /* Create the root thread -- it might be a placeholder for the
           current context or a real thread, it depends on the real-time
           layer. If the root thread needs to allocate stack memory, it
           must not rely on the validity of "nkpod" when doing so. */

        rc = xnthread_init(&sched->rootcb,
                           root_name,
                           XNPOD_ROOT_PRIO_BASE,
                           XNROOT|XNSTARTED
#ifdef CONFIG_RTAI_HW_FPU
                           /* If the host environment has a FPU, the root
                              thread must care for the FPU context. */
                           |XNFPU
#endif /* CONFIG_RTAI_HW_FPU */
                           ,
                           XNARCH_ROOT_STACKSZ);

        if (rc)
            {
fail:
            nkpod = NULL;
            return rc;
            }

        appendq(&pod->threadq,&sched->rootcb.glink);

        sched->runthread = &sched->rootcb;
#ifdef CONFIG_RTAI_HW_FPU
        sched->fpuholder = &sched->rootcb;
#endif /* CONFIG_RTAI_HW_FPU */

        /* Initialize per-cpu rootcb */
        xnarch_init_root_tcb(xnthread_archtcb(&sched->rootcb),
                             &sched->rootcb,
                             xnthread_name(&sched->rootcb));

        sched->rootcb.sched = sched;

        sched->rootcb.affinity = xnarch_cpumask_of_cpu(cpu);
        }

    xnarch_hook_ipi(&xnpod_schedule_handler);

    xnarch_memory_barrier();
    
    clrbits(pod->status,XNPIDLE);

    xnarch_notify_ready();

    return 0;
}

static void xnpod_flush_heap (xnheap_t *heap,
                              void *extaddr,
                              u_long extsize,
                              void *cookie)
{
    xnarch_sysfree(extaddr,extsize);
}

/*! 
 * \fn void xnpod_shutdown(int xtype)
 * \brief Shutdown the current pod.
 *
 * Forcibly shutdowns the active pod. All existing nucleus threads
 * (but the root one) are terminated, and the system heap is freed.
 *
 * @param xtype An exit code passed to the host environment who
 * started the nucleus. Zero is always interpreted as a successful
 * return.
 *
 * The nucleus never calls this routine directly. Skins should provide
 * their own shutdown handlers which end up calling xnpod_shutdown()
 * after their own housekeeping chores have been carried out.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 *
 * Rescheduling: never.
 */

void xnpod_shutdown (int xtype)

{
    xnholder_t *holder, *nholder;
    xnthread_t *thread;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (!nkpod || testbits(nkpod->status,XNPIDLE) || nkpod->refcnt > 0)
        goto unlock_and_exit; /* No-op */

    /* FIXME: We must release the lock before stopping the timer, so
       we accept a potential race due to another skin being pushed
       while we remove the current pod, which is clearly not a common
       situation anyway. */

    xnlock_put_irqrestore(&nklock,s);

    xnpod_stop_timer();

    xnarch_notify_shutdown();

    xnlock_get_irqsave(&nklock,s);

    xntimer_destroy(&nkpod->htimer);

    nholder = getheadq(&nkpod->threadq);

    while ((holder = nholder) != NULL)
        {
        nholder = nextq(&nkpod->threadq,holder);

        thread = link2thread(holder,glink);

        if (!testbits(thread->status,XNROOT))
            xnpod_delete_thread(thread);
        }

    xnpod_schedule();

    setbits(nkpod->status,XNPIDLE);

    xnlock_put_irqrestore(&nklock,s);

    xnarch_notify_halt();

    xnlock_get_irqsave(&nklock,s);

    xnheap_destroy(&kheap,&xnpod_flush_heap,NULL);

    nkpod = NULL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

static inline void xnpod_fire_callouts (xnqueue_t *hookq, xnthread_t *thread)

{
    /* Must be called with nklock locked, interrupts off. */
    xnsched_t *sched = xnpod_current_sched();
    xnholder_t *holder, *nholder;

    __setbits(sched->status,XNKCOUT);

    /* The callee is allowed to alter the hook queue when running */

    nholder = getheadq(hookq);

    while ((holder = nholder) != NULL)
        {
        xnhook_t *hook = link2hook(holder);
        nholder = nextq(hookq,holder);
        hook->routine(thread);
        }

    __clrbits(sched->status,XNKCOUT);
}

static inline void xnpod_switch_zombie (xnthread_t *threadout,
                                        xnthread_t *threadin)
{
    /* Must be called with nklock locked, interrupts off. */
    xnsched_t *sched = xnpod_current_sched();
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    int shadow = testbits(threadout->status,XNSHADOW);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    xnltt_log_event(rtai_ev_finalize,threadout->name,threadin->name);

    if (countq(&nkpod->tdeleteq) > 0 &&
        !testbits(threadout->status,XNTHREAD_SYSTEM_BITS))
        {
        xnltt_log_event(rtai_ev_callout,"SELF-DELETE",threadout->name);
        xnpod_fire_callouts(&nkpod->tdeleteq,threadout);
        }

    sched->runthread = threadin;

    if (testbits(threadin->status,XNROOT)) {
        xnpod_reset_watchdog();
        xnfreesync();
        xnarch_enter_root(xnthread_archtcb(threadin));
    }

     /* FIXME: Catch 22 here, whether we choose to run on an invalid
        stack (cleanup then hooks), or to access the TCB space shortly
        after it has been freed while non-preemptible (hooks then
        cleanup)... Option #2 is current. */

    xnthread_cleanup_tcb(threadout);

    xnarch_finalize_and_switch(xnthread_archtcb(threadout),
                               xnthread_archtcb(threadin));

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    if (shadow)
        /* Reap the user-space mate of a deleted real-time shadow.
           The Linux task has resumed into the Linux domain at the
           last code location executed by the shadow. Remember
           that both sides use the Linux task's stack. */
        xnshadow_exit();
#endif /* __KERNEL__  && CONFIG_RTAI_OPT_FUSION */
    
    xnpod_fatal("zombie thread %s (%p) would not die...",threadout->name,threadout);
}

/*! 
 * \fn void xnpod_init_thread(xnthread_t *thread,const char *name,int prio,xnflags_t flags,unsigned stacksize)
 * \brief Initialize a new thread.
 *
 * Initializes a new thread attached to the active pod. The thread is
 * left in an innocuous state until it is actually started by
 * xnpod_start_thread().
 *
 * @param thread The address of a thread descriptor the nucleus will
 * use to store the thread-specific data.  This descriptor must always
 * be valid while the thread is active therefore it must be allocated
 * in permanent memory. @warning Some architectures may require the
 * descriptor to be properly aligned in memory; this is an additional
 * reason for descriptors not to be laid in the program stack where
 * alignement constraints might not always be satisfied.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * thread. This name is copied to a safe place into the thread
 * descriptor. This name might be used in various situations by the
 * nucleus for issuing human-readable diagnostic messages, so it is
 * usually a good idea to provide a sensible value here. The simulator
 * even uses this name intensively to identify threads in the
 * debugging GUI it provides. However, passing NULL here is always
 * legal and means "anonymous".
 *
 * @param prio The base priority of the new thread. This value must
 * range from [minpri .. maxpri] (inclusive) as specified when calling
 * the xnpod_init() service.
 *
 * @param flags A set of creation flags affecting the operation. The
 * following flags can be part of this bitmask, each of them affecting
 * the nucleus behaviour regarding the created thread:
 *
 * - XNSUSP creates the thread in a suspended state. In such a case,
 * the thread will have to be explicitely resumed using the
 * xnpod_resume_thread() service for its execution to actually begin,
 * additionally to issuing xnpod_start_thread() for it. This flag can
 * also be specified when invoking xnpod_start_thread() as a starting
 * mode.

 * - XNFPU (enable FPU) tells the nucleus that the new thread will use
 * the floating-point unit. In such a case, the nucleus will handle
 * the FPU context save/restore ops upon thread switches at the
 * expense of a few additional cycles per context switch. By default,
 * a thread is not expected to use the FPU. This flag is simply
 * ignored when the nucleus runs on behalf of a userspace-based
 * real-time control layer since the FPU management is always active
 * if present.
 *
 * @param stacksize The size of the stack (in bytes) for the new
 * thread. If zero is passed, the nucleus will use a reasonable
 * pre-defined size depending on the underlying real-time control
 * layer.
 *
 * After creation, the new thread can be set a magic cookie by skins
 * using xnthread_set_magic() to unambiguously identify threads
 * created in their realm. This value will be copied as-is to the @a
 * magic field of the thread struct. 0 is a conventional value for "no
 * magic".
 *
 * @return 0 is returned on success. Otherwise, one of the following
 * error codes indicates the cause of the failure:
 *
 *         - -EINVAL is returned if @a flags has invalid bits set.
 *
 *         - -ENOMEM is returned if not enough memory is available
 *         from the system heap to create the new thread's stack.
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnpod_init_thread (xnthread_t *thread,
                       const char *name,
                       int prio,
                       xnflags_t flags,
                       unsigned stacksize)
{
    spl_t s;
    int err;

    if (flags & ~(XNFPU|XNSHADOW|XNSHIELD|XNSUSP))
        return -EINVAL;

    if (stacksize == 0)
        stacksize = XNARCH_THREAD_STACKSZ;

    err = xnthread_init(thread,name,prio,flags,stacksize);

    if (err)
        return err;

    xnltt_log_event(rtai_ev_thrinit,thread->name,flags);

    xnlock_get_irqsave(&nklock,s);
    thread->sched = xnpod_current_sched();
    appendq(&nkpod->threadq,&thread->glink);
    xnpod_suspend_thread(thread,XNDORMANT,XN_INFINITE,NULL);
    xnlock_put_irqrestore(&nklock,s);

    return 0;
}

/*! 
 * \fn int xnpod_start_thread(xnthread_t *thread,xnflags_t mode,int imask,xnarch_cpumask_t affinity,void (*entry)(void *cookie),void *cookie)
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
 * part of this bitmask, each of them affecting the nucleus
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
 * @param affinity The processor affinity of this thread. Passing
 * XNPOD_ALL_CPUS or an empty affinity set means "any cpu".
 *
 * @param entry The address of the thread's body routine. In other
 * words, it is the thread entry point.
 *
 * @param cookie A user-defined opaque cookie the nucleus will pass
 * to the emerging thread as the sole argument of its entry point.
 *
 * The START hooks are called on behalf of the calling context (if
 * any).
 *
 * @retval 0 if @a thread could be started ;
 *
 * @retval -EBUSY if @a thread was already started ;
 *
 * @retval -EINVAL if the value of @a affinity is invalid.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int xnpod_start_thread (xnthread_t *thread,
                        xnflags_t mode,
                        int imask,
                        xnarch_cpumask_t affinity,
                        void (*entry)(void *cookie),
                        void *cookie)
{
    spl_t s;
    int err;

    if (!testbits(thread->status,XNDORMANT))
        return -EBUSY;

    if (xnarch_cpus_empty(affinity))
        affinity = XNARCH_CPU_MASK_ALL;

    xnlock_get_irqsave(&nklock,s);

    thread->affinity = xnarch_cpu_online_map;
    xnarch_cpus_and(thread->affinity, affinity, thread->affinity);

    if (xnarch_cpus_empty(thread->affinity))
        {
        err = -EINVAL;
        goto unlock_and_exit;
        }

#ifdef CONFIG_SMP
    if (!xnarch_cpu_isset(xnsched_cpu(thread->sched), thread->affinity))
        thread->sched = xnpod_sched_slot(xnarch_first_cpu(thread->affinity));
#endif /* CONFIG_SMP */

    if (testbits(thread->status,XNSTARTED))
        {
        err = -EBUSY;
        goto unlock_and_exit;
        }

    __setbits(thread->status,(mode & (XNTHREAD_MODE_BITS|XNSUSP))|XNSTARTED);
    thread->imask = imask;
    thread->imode = (mode & XNTHREAD_MODE_BITS);
    thread->entry = entry;
    thread->cookie = cookie;
    thread->stime = xnarch_get_cpu_time();

    if (testbits(thread->status,XNRRB))
        thread->rrcredit = thread->rrperiod;

    xnltt_log_event(rtai_ev_thrstart,thread->name);

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    if (testbits(thread->status,XNSHADOW))
        {
        xnlock_put_irqrestore(&nklock,s);
        xnshadow_start(thread);
        xnpod_schedule();
        return 0;
        }
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    /* Setup the initial stack frame. */

    xnarch_init_thread(xnthread_archtcb(thread),
                       entry,
                       cookie,
                       imask,
                       thread,
                       thread->name);

    xnpod_resume_thread(thread,XNDORMANT);

#ifdef __RTAI_SIM__
    if (!(mode & XNSUSP) && nkpod->schedhook)
        nkpod->schedhook(thread,XNREADY);
#endif /* __RTAI_SIM__ */

    if (countq(&nkpod->tstartq) > 0 &&
        !testbits(thread->status,XNTHREAD_SYSTEM_BITS))
        {
        xnltt_log_event(rtai_ev_callout,"START",thread->name);
        xnpod_fire_callouts(&nkpod->tstartq,thread);
        }

    xnpod_schedule();

    err = 0;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*! 
 * \fn void xnpod_restart_thread(xnthread_t *thread)
 *
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
 * Self-restarting a thread is allowed. However, restarting the root
 * thread is not.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

void xnpod_restart_thread (xnthread_t *thread)

{
    spl_t s;

    if (!testbits(thread->status,XNSTARTED))
        return; /* Not started yet or not restartable. */

#if defined(CONFIG_RTAI_OPT_DEBUG) || defined(__RTAI_SIM__)
    if (testbits(thread->status,XNROOT|XNSHADOW))
        xnpod_fatal("attempt to restart a user-space thread");
#endif /* CONFIG_RTAI_OPT_DEBUG || __RTAI_SIM__ */

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_threstart,thread->name);

    /* Break the thread out of any wait it is currently in. */
    xnpod_unblock_thread(thread);

    /* Release all ownerships held by the thread on synch. objects */
    xnsynch_release_all_ownerships(thread);

    /* If the task has been explicitely suspended, resume it. */
    if (testbits(thread->status,XNSUSP))
        xnpod_resume_thread(thread,XNSUSP);

    /* Reset modebits. */
    __clrbits(thread->status,XNTHREAD_MODE_BITS);
    __setbits(thread->status,thread->imode);

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
            __clrbits(thread->status,XNLOCK);
            xnarch_atomic_set(&nkpod->schedlck,0);
            }

        __setbits(thread->status,XNRESTART);
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
       be lower than the restarted thread's priority. */

    xnpod_schedule();

    xnlock_put_irqrestore(&nklock,s);
}

/*! 
 * \fn void xnpod_set_thread_mode(xnthread_t *thread,xnflags_t clrmask,xnflags_t setmask)
 * \brief Change a thread's control mode.
 *
 * Change the control mode of a given thread. The control mode affects
 * the behaviour of the nucleus regarding the specified thread.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * @param clrmask Clears the corresponding bits from the control field
 * before setmask is applied. The scheduler lock held by the current
 * thread can be forcibly released by passing the XNLOCK bit in this
 * mask. In this case, the lock nesting count is also reset to zero.
 *
 * @param setmask The new thread mode. The following flags can be part
 * of this bitmask, each of them affecting the nucleus behaviour
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
 * - XNSHIELD enables the interrupt shield for the current user-space
 * task. When engaged, the interrupt shield protects the shadow task
 * running in secondary mode from any preemption by the regular Linux
 * interrupt handlers, without delaying in any way the RTAI interrupt
 * handling. The shield is operated on a per-task basis at each
 * context switch, depending on the setting of this bit.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never, therefore, the caller should reschedule if
 * XNLOCK has been passed into @a clrmask.
 */

xnflags_t xnpod_set_thread_mode (xnthread_t *thread,
                                 xnflags_t clrmask,
                                 xnflags_t setmask)
{
    xnthread_t *runthread = xnpod_current_thread();
    xnflags_t oldmode;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_thrsetmode,thread->name,clrmask,setmask);

    oldmode = (thread->status & XNTHREAD_MODE_BITS);
    __clrbits(thread->status,clrmask & XNTHREAD_MODE_BITS);
    __setbits(thread->status,setmask & XNTHREAD_MODE_BITS);

    if (runthread == thread)
        {
        if (!(oldmode & XNLOCK))
            {
            if (testbits(thread->status,XNLOCK))
                /* Actually grab the scheduler lock. */
                xnpod_lock_sched();
            }
        else if (!testbits(thread->status,XNLOCK))
            xnarch_atomic_set(&nkpod->schedlck,0);
        }

    if (!(oldmode & XNRRB) && testbits(thread->status,XNRRB))
        thread->rrcredit = thread->rrperiod;

    xnlock_put_irqrestore(&nklock,s);

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    if (runthread == thread &&
        testbits(thread->status,XNSHADOW) &&
        ((clrmask|setmask) & XNSHIELD) != 0)
        xnshadow_reset_shield();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    return oldmode;
}

/*! 
 * \fn void xnpod_delete_thread(xnthread_t *thread)
 *
 * \brief Delete a thread.
 *
 * Terminates a thread and releases all the nucleus resources it
 * currently holds. A thread exists in the system since
 * xnpod_init_thread() has been called to create it, so this service
 * must be called in order to destroy it afterwards.
 *
 * @param thread The descriptor address of the terminated thread.
 *
 * The DELETE hooks are called on behalf of the calling context (if
 * any). The information stored in the thread control block remains
 * valid until all hooks have been called.
 *
 * Self-terminating a thread is allowed. In such a case, this service
 * does not return to the caller.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if the current thread self-deletes.
 */

void xnpod_delete_thread (xnthread_t *thread)

{
    xnsched_t *sched;
    spl_t s;

#if defined(CONFIG_RTAI_OPT_DEBUG) || defined(__RTAI_SIM__)
    if (testbits(thread->status,XNROOT))
        xnpod_fatal("attempt to delete the root thread");
#endif /* CONFIG_RTAI_OPT_DEBUG || __RTAI_SIM__ */

#ifdef __RTAI_SIM__
    if (nkpod->schedhook)
        nkpod->schedhook(thread,XNDELETED);
#endif /* __RTAI_SIM__ */

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_thrdelete,thread->name);

    sched = thread->sched;

    removeq(&nkpod->threadq,&thread->glink);

    if (!testbits(thread->status,XNTHREAD_BLOCK_BITS))
        {
        if (testbits(thread->status,XNREADY))
            {
            sched_removepq(&sched->readyq,&thread->rlink);
            __clrbits(thread->status,XNREADY);
            }
        }
    else
        {
        if (testbits(thread->status,XNDELAY))
            xntimer_stop(&thread->rtimer);

        if (testbits(thread->status,XNTHREAD_BLOCK_BITS & ~XNDELAY))
            removeq(&nkpod->suspendq,&thread->slink);
        }

    xntimer_stop(&thread->ptimer);

    /* Ensure the rescheduling can take place if the deleted thread is
       the running one. */

    if (testbits(thread->status,XNLOCK))
        {
        __clrbits(thread->status,XNLOCK);
        xnarch_atomic_set(&nkpod->schedlck,0);
        }

    if (testbits(thread->status,XNPEND))
        xnsynch_forget_sleeper(thread);

    xnsynch_release_all_ownerships(thread);

#ifdef CONFIG_RTAI_HW_FPU
    if (thread == sched->fpuholder)
        sched->fpuholder = NULL;
#endif /* CONFIG_RTAI_HW_FPU */

    __setbits(thread->status,XNZOMBIE);

    if (sched->runthread == thread)
        {
        /* We first need to elect a new runthread before switching out
           the current one forever. Use the thread zombie state to go
           through the rescheduling procedure then actually destroy
           the thread object. */
        xnsched_set_resched(sched);
        xnpod_schedule();
        }
    else
        {
        if (countq(&nkpod->tdeleteq) > 0 &&
            !testbits(thread->status,XNTHREAD_SYSTEM_BITS))
            {
            xnltt_log_event(rtai_ev_callout,"DELETE",thread->name);
            xnpod_fire_callouts(&nkpod->tdeleteq,thread);
            }

        /* Note: the thread control block must remain available until
           the user hooks have been called. */

        xnthread_cleanup_tcb(thread);

        xnarch_finalize_no_switch(xnthread_archtcb(thread));
        }

    xnlock_put_irqrestore(&nklock,s);
}

/*!
 * \fn void xnpod_suspend_thread(xnthread_t *thread,xnflags_t mask,xnticks_t timeout,xnsynch_t *wchan)
 *
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
 * resource to wait for. A reverse call to xnpod_resume_thread()
 * specifying the XNSUSP bit must be issued to remove this condition,
 * which is cumulative with other suspension bits.@a wchan should be
 * NULL when using this suspending mode.
 *
 * - XNDELAY. This flags denotes a counted delay wait (in ticks) which
 * duration is defined by the value of the timeout parameter.
 *
 * - XNPEND. This flag denotes a wait for a synchronization object to
 * be signaled. The wchan argument must points to this object. A
 * timeout value can be passed to bound the wait. This suspending mode
 * should not be used directly by the upper interface, but rather
 * through the xnsynch_sleep_on() call.
 *
 * @param timeout The timeout which may be used to limit the time the
 * thread pends for a resource. This value is a wait time given in
 * ticks (see note).  Passing XN_INFINITE specifies an unbounded
 * wait. All other values are used to initialize a watchdog timer.  If
 * the current operation mode is oneshot and @a timeout elapses before
 * xnpod_suspend_thread() has completed, then the target thread will
 * not be suspended, and this routine leads to a null effect.
 *
 * @param wchan The address of a pended resource. This parameter is
 * used internally by the synchronization object implementation code
 * to specify on which object the suspended thread pends. NULL is a
 * legitimate value when this parameter does not apply to the current
 * suspending mode (e.g. XNSUSP).
 *
 * @note If the target thread is a shadow which has received a
 * Linux-originated signal, then this service immediately exits
 * without suspending the thread, but raises the XNBREAK condition in
 * its status.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if the current thread suspends itself.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the xnpod_start_timer() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

void xnpod_suspend_thread (xnthread_t *thread,
                           xnflags_t mask,
                           xnticks_t timeout,
                           xnsynch_t *wchan)
{
    xnsched_t *sched;
    spl_t s;

#if defined(CONFIG_RTAI_OPT_DEBUG) || defined(__RTAI_SIM__)
    if (testbits(thread->status,XNTHREAD_SYSTEM_BITS))
        xnpod_fatal("attempt to suspend system thread %s",thread->name);

    if (thread->wchan && wchan)
        xnpod_fatal("thread %s attempts a conjunctive wait",thread->name);
#endif /* CONFIG_RTAI_OPT_DEBUG || __RTAI_SIM__ */

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_thrsuspend,thread->name,mask,timeout,wchan);

    sched = thread->sched;

    if (thread == sched->runthread)
        {
#if defined(CONFIG_RTAI_OPT_DEBUG) || defined(__RTAI_SIM__)
        if (sched == xnpod_current_sched() && xnpod_locked_p())
            xnpod_fatal("suspensive call issued while the scheduler was locked");
#endif /* CONFIG_RTAI_OPT_DEBUG || __RTAI_SIM__ */

        xnsched_set_resched(sched);
        }

    /* We must make sure that we don't clear the wait channel if a
       thread is first blocked (wchan != NULL) then forcibly suspended
       (wchan == NULL), since these are conjunctive conditions. */

    if (wchan)
        thread->wchan = wchan;

    /* Is the thread ready to run? */

    if (!testbits(thread->status,XNTHREAD_BLOCK_BITS))
        {
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
        /* If attempting to suspend a runnable (shadow) thread which
           has received a Linux signal, just raise the break condition
           and return immediately. Note: a relaxed shadow never has
           the KICKED bit set, so that xnshadow_relax() is never
           prevented from blocking the current thread. */
        if (testbits(thread->status,XNKICKED))
            {
            __clrbits(thread->status,XNRMID|XNTIMEO);
            __setbits(thread->status,XNBREAK);
            thread->wchan = NULL;
            goto unlock_and_exit;
            }
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

        /* A newly created thread is not linked to the ready thread
           queue yet. */

        if (testbits(thread->status,XNREADY))
            {
            sched_removepq(&sched->readyq,&thread->rlink);
            __clrbits(thread->status,XNREADY);
            }

        if ((mask & ~XNDELAY) != 0)
            /* If the thread is forcibly suspended outside the simple
               delay condition, link it to suspension queue. */
            appendq(&nkpod->suspendq,&thread->slink);

        __clrbits(thread->status,XNRMID|XNTIMEO|XNBREAK);
        }
    else if ((mask & ~XNDELAY) != 0 &&
             !testbits(thread->status,XNTHREAD_BLOCK_BITS & ~XNDELAY))
        /* If the thread is forcibly suspended while undergoing a
           simple delay condition, link it to suspension queue too. */
        appendq(&nkpod->suspendq,&thread->slink);

    __setbits(thread->status,mask);

    if (timeout != XN_INFINITE)
        {
        /* Don't start the timer for a thread indefinitely delayed by
           a call to xnpod_suspend_thread(thread,XNDELAY,0,NULL). */
        __setbits(thread->status,XNDELAY);
        xntimer_set_sched(&thread->rtimer, thread->sched);
        xntimer_start(&thread->rtimer,timeout,XN_INFINITE);
        }
    
#ifdef __RTAI_SIM__
    if (nkpod->schedhook)
        nkpod->schedhook(thread,mask);
#endif /* __RTAI_SIM__ */

    if (thread == sched->runthread)
        /* If "thread" is runnning on another CPU, xnpod_schedule will
           just trigger the IPI. */
        xnpod_schedule();
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* Ok, this one is an interesting corner case, which requires a
       bit of background first. Here, we handle the case of suspending
       a _relaxed_ shadow which is _not_ the current thread.  The net
       effect is that we are attempting to stop the shadow thread at
       the nucleus level, whilst this thread is actually running some
       code under the control of the Linux scheduler (i.e. it's
       relaxed).  To make this possible, we force the target Linux
       task to migrate back to the RTAI domain by sending it a
       SIGCHLD signal the skin interface libraries trap for this
       specific internal purpose, whose handler is expected to call
       back the nucleus's migration service. By forcing this
       migration, we make sure that the real-time nucleus controls,
       hence properly stops, the target thread according to the
       requested suspension condition. Otherwise, the shadow thread in
       secondary mode would just keep running into the Linux domain,
       thus breaking the most common assumptions regarding suspended
       threads. We only care for threads that are not current, and for
       XNSUSP and XNDELAY conditions, because:
     
       - skins are supposed to ask for primary mode switch when
       processing any syscall which may block the caller; IOW,
       __xn_exec_primary must be set in the mode flags for those. So
       there is no need to deal specifically with the relax+suspend
       issue when the about to be suspended thread is current, since
       it must not be relaxed anyway.

       - among all blocking bits (XNTHREAD_BLOCK_BITS), only XNSUSP
       and XNDELAY may be applied by the current thread to a
       non-current thread. XNPEND is always added by the caller to its
       own state, XNDORMANT is a pre-runtime state, and XNRELAX has
       special semantics escaping this issue.
     */

    else if (testbits(thread->status,XNSHADOW|XNRELAX) == (XNSHADOW|XNRELAX) &&
	     (mask & (XNDELAY|XNSUSP)) != 0)
	xnshadow_suspend(thread);

 unlock_and_exit:

#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    xnlock_put_irqrestore(&nklock,s);
}

/*!
 * \fn void xnpod_resume_thread(xnthread_t *thread,xnflags_t mask)
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
 * watchdog is armed, it is automatically disarmed by this
 * call. Unlike the two previous conditions, only the current thread
 * can set this condition for itself, i.e. no thread can force another
 * one to pend on a resource.
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
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xnpod_resume_thread (xnthread_t *thread,
                          xnflags_t mask)
{
    xnsched_t *sched;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_thresume,thread->name,mask);

    sched = thread->sched;

    if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* Is thread blocked? */
        {
        __clrbits(thread->status,mask); /* Remove specified block bit(s) */

        if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* still blocked? */
            {
            if ((mask & XNDELAY) != 0)
                {
                /* Watchdog fired or break requested -- stop waiting
                   for the resource. */

                xntimer_stop(&thread->rtimer);

                mask = testbits(thread->status,XNPEND);

                if (mask)
                    {
                    if (thread->wchan)
                        xnsynch_forget_sleeper(thread);

                    if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* Still blocked? */
                        goto unlock_and_exit;
                    }
                else
                    /* The thread is still suspended (XNSUSP or even
                       XNDORMANT if xnpod_set_thread_periodic() has
                       been applied to a non-started thread) */
                    goto unlock_and_exit;
                }
            else if (testbits(thread->status,XNDELAY))
                {
                if ((mask & XNPEND) != 0)
                    {
                    /* The thread is woken up due to the availability
                       of the requested resource. Cancel the watchdog
                       timer. */
                    xntimer_stop(&thread->rtimer);
                    __clrbits(thread->status,XNDELAY);
                    }

                if (testbits(thread->status,XNTHREAD_BLOCK_BITS)) /* Still blocked? */
                    {
                    if (testbits(thread->status,XNDELAY))
                        /* Funky corner case here: the suspensive
                           condition we have just removed was not
                           XNPEND, and we are still blocked on
                           XNDELAY.  We thus need to remove the thread
                           from the suspension queue to reflect the
                           removal of the cleared condition, since
                           threads on resource-free timed waits are
                           not expected to be linked to this queue. */
                        removeq(&nkpod->suspendq,&thread->slink);

                    goto unlock_and_exit;
                    }
                }
            else
                {
                /* The thread is still suspended, but is no more
                   pending on a resource. */

                if ((mask & XNPEND) != 0 && thread->wchan)
                    xnsynch_forget_sleeper(thread);

                goto unlock_and_exit;
                }
            }
        else if ((mask & XNDELAY) != 0)
            /* The delayed thread has been woken up, either forcibly
               using xnpod_unblock_thread(), or because the specified
               delay has elapsed. In the latter case, stopping the
               timer is simply a no-op. */
            xntimer_stop(&thread->rtimer);

        if ((mask & ~XNDELAY) != 0)
            {
            /* If the thread was actually suspended, remove it from
               the suspension queue -- this allows requests like
               xnpod_suspend_thread(...,thread,XNDELAY,0,...) not to
               run the following code when the suspended thread is
               woken up while undergoing an infinite delay. */

            removeq(&nkpod->suspendq,&thread->slink);

            if (thread->wchan)
                xnsynch_forget_sleeper(thread);
            }
        }
    else if (testbits(thread->status,XNREADY))
        {
        sched_removepq(&sched->readyq,&thread->rlink);
        __clrbits(thread->status,XNREADY);
        }

    /* The readied thread is always put to the end of its priority
       group. */

    sched_insertpqf(&sched->readyq,&thread->rlink,thread->cprio);

    xnsched_set_resched(sched);

    if (thread == sched->runthread)
        {
        __setbits(thread->status,XNREADY);

#ifdef __RTAI_SIM__
        if (nkpod->schedhook &&
            sched_getheadpq(&sched->readyq) != &thread->rlink)
            /* The running thread does no longer lead the ready
               queue. */
            nkpod->schedhook(thread,XNREADY);
#endif /* __RTAI_SIM__ */
        }
    else if (!testbits(thread->status,XNREADY))
        {
        __setbits(thread->status,XNREADY);

#ifdef __RTAI_SIM__
        if (nkpod->schedhook)
            nkpod->schedhook(thread,XNREADY);
#endif /* __RTAI_SIM__ */
        }

unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

/*!
 * \fn int xnpod_unblock_thread(xnthread_t *thread)
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
 * This call neither releases the thread from the XNSUSP, XNRELAX nor
 * the XNDORMANT suspensive conditions.
 *
 * When the thread resumes execution, the XNBREAK bit is set in the
 * unblocked thread's status mask. Unblocking a non-blocked thread is
 * perfectly harmless.
 *
 * @return non-zero is returned if the thread was actually unblocked
 * from a pending wait state, 0 otherwise.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnpod_unblock_thread (xnthread_t *thread)

{
    int ret = 1;
    spl_t s;

    /* Attempt to abort an undergoing wait for the given thread.  If
       this state is due to an alarm that has been armed to limit the
       sleeping thread's waiting time while it pends for a resource,
       the corresponding XNPEND state will be cleared by
       xnpod_resume_thread() in the same move. Otherwise, this call
       may abort an undergoing infinite wait for a resource (if
       any). */

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_thrunblock,thread->name,thread->status);

    if (testbits(thread->status,XNDELAY))
        xnpod_resume_thread(thread,XNDELAY);
    else if (testbits(thread->status,XNPEND))
        xnpod_resume_thread(thread,XNPEND);
    else
        ret = 0;

    /* We should not clear a previous break state if this service is
       called more than once before the target thread actually
       resumes, so we only set the bit here and never clear it. */

    __setbits(thread->status,XNBREAK);

    xnlock_put_irqrestore(&nklock,s);

    return ret;
}

/*!
 * \fn void xnpod_renice_thread(xnthread_t *thread,int prio)
 * \brief Change the base priority of a thread.
 *
 * Changes the base priority of a thread.  If the XNDREORD flag has
 * not been passed to xnpod_init() and the reniced thread is currently
 * blocked waiting in priority-pending mode (XNSYNCH_PRIO) for a
 * synchronization object to be signaled, the nucleus will attempt
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
 * to the end of the ready queue, thus causing a manual round-robin.
 *
 * - If the reniced thread is a user-space shadow, propagate the
 * request to the mated Linux task.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xnpod_renice_thread (xnthread_t *thread, int prio)

{
    xnpod_renice_thread_inner(thread,prio,1);
}

void xnpod_renice_thread_inner (xnthread_t *thread, int prio, int propagate)

{
    int oldprio;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_threnice,thread->name,prio);

    oldprio = thread->cprio;

    /* Change the thread priority, taking in account an undergoing PIP
       boost. */

    thread->bprio = prio;

    /* Since we don't want to mess with the priority inheritance
       scheme, we must take care of never lowering the target thread's
       priority level if it is undergoing a PIP boost. */

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

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    if (propagate && testbits(thread->status,XNRELAX))
        xnshadow_renice(thread);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    xnlock_put_irqrestore(&nklock,s);
}

/** 
 * \fn int xnpod_migrate_thread (int cpu)
 *
 * \brief Migrate the current thread.
 *
 * This call makes the current thread migrate to another CPU if its
 * affinity allows it.
 * 
 * @param cpu The destination CPU.
 * 
 * @retval 0 if the thread could migrate ;
 * @retval -EPERM if the calling context is asynchronous, or the
 * current thread affinity forbids this migration ;
 * @retval -EBUSY if the scheduler is locked.
 */

int xnpod_migrate_thread (int cpu)

{
    xnthread_t *thread;
    int err;
    spl_t s;

    if (xnpod_asynch_p())
        return -EPERM;

    if (xnpod_locked_p())
        return -EBUSY;

    xnlock_get_irqsave(&nklock, s);

    thread = xnpod_current_thread();

    if (!xnarch_cpu_isset(cpu, thread->affinity))
        {
        err = -EPERM;
        goto unlock_and_exit;
        }

    err = 0;

    if (cpu == xnarch_current_cpu())
        goto unlock_and_exit;
    
    xnltt_log_event(rtai_ev_cpumigrate,thread->name,cpu);

#ifdef CONFIG_RTAI_HW_FPU
    if (testbits(thread->status, XNFPU))
        {
        /* Force the FPU save, and nullify the sched->fpuholder pointer, to
           avoid leaving fpuholder pointing on the backup area of the migrated
           thread. */
        xnarch_save_fpu(xnthread_archtcb(thread));    

        thread->sched->fpuholder = NULL;
        }
#endif /* CONFIG_RTAI_HW_FPU */    

    if(testbits(thread->status, XNREADY))
        {
        sched_removepq(&thread->sched->readyq, &thread->rlink);
        clrbits(thread->status, XNREADY);
        }

    xnsched_set_resched(thread->sched);

    thread->sched = xnpod_sched_slot(cpu);

    /* Migrate the thread periodic timer. */
    xntimer_set_sched(&thread->ptimer, thread->sched);

    /* Put thread in the ready queue of the destination CPU's scheduler. */
    xnpod_resume_thread(thread, 0);

    xnpod_schedule();

unlock_and_exit:

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

/*!
 * \fn void xnpod_rotate_readyq(int prio)
 * \brief Rotate a priority level in the ready queue.
 *
 * The thread at the head of the ready queue of the given priority
 * level is moved to the end of this queue. Therefore, the execution
 * of threads having the same priority is switched.  Round-robin
 * scheduling policies may be implemented by periodically issuing this
 * call in a given period of time. It should be noted that the
 * nucleus already provides a built-in round-robin mode though (see
 * xnpod_activate_rr()).
 *
 * @param prio The priority level to rotate. if XNPOD_RUNPRIO is
 * given, the running thread priority is used to rotate the queue.
 *
 * The priority level which is considered is always the base priority
 * of a thread, not the possibly PIP-boosted current priority
 * value. Specifying a priority level with no thread on it is harmless,
 * and will simply lead to a null-effect.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xnpod_rotate_readyq (int prio)

{
    xnpholder_t *pholder;
    xnsched_t *sched;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sched = xnpod_current_sched();

    if (sched_countpq(&sched->readyq) == 0)
        goto unlock_and_exit; /* Nobody is ready. */

    xnltt_log_event(rtai_ev_rdrotate,sched->runthread,prio);

    /* There is _always_ a regular thread, ultimately the root
       one. Use the base priority, not the priority boost. */

    if (prio == XNPOD_RUNPRIO ||
        prio == xnthread_base_priority(sched->runthread))
        xnpod_resume_thread(sched->runthread,0);
    else
        {
        pholder = sched_findpqh(&sched->readyq,prio);

        if (pholder)
            /* This call performs the actual rotation. */
            xnpod_resume_thread(link2thread(pholder,rlink),0);
        }

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

/*! 
 * \fn void xnpod_activate_rr(xnticks_t quantum)
 * \brief Globally activate the round-robin scheduling.
 *
 * This service activates the round-robin scheduling for all threads
 * which have the XNRRB flag set in their status mask (see
 * xnpod_set_thread_mode()). Each of them will run for the given time
 * quantum, then preempted and moved to the end of its priority group
 * in the ready queue. This process is repeated until the round-robin
 * scheduling is disabled for those threads.
 *
 * @param quantum The time credit which will be given to each
 * rr-enabled thread (in ticks).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xnpod_activate_rr (xnticks_t quantum)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_rractivate,quantum);

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

    xnlock_put_irqrestore(&nklock,s);
}

/*! 
 * \fn void xnpod_deactivate_rr(void)
 * \brief Globally deactivate the round-robin scheduling.
 *
 * This service deactivates the round-robin scheduling for all threads
 * which have the XNRRB flag set in their status mask (see
 * xnpod_set_thread_mode()).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xnpod_deactivate_rr (void)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_rrdeactivate);

    holder = getheadq(&nkpod->threadq);

    while (holder)
        {
        xnthread_t *thread = link2thread(holder,glink);

        if (testbits(thread->status,XNRRB))
            thread->rrcredit = XN_INFINITE;

        holder = nextq(&nkpod->threadq,holder);
        }

    xnlock_put_irqrestore(&nklock,s);
}

/*! 
 * @internal
 * \fn void xnpod_dispatch_signals(void)
 * \brief Deliver pending asynchronous signals to the running thread.
 *
 * This internal routine checks for the presence of asynchronous
 * signals directed to the running thread, and attempt to start the
 * asynchronous service routine (ASR) if any.
 */

void xnpod_dispatch_signals (void)

{
    xnthread_t *thread = xnpod_current_thread();
    int asrimask, savedmask;
    xnflags_t oldmode;
    xnsigmask_t sigs;
    xnasr_t asr;

    /* Process user-defined signals if the ASR is enabled for this
       thread. */

    if (thread->signals == 0 ||
        testbits(thread->status,XNASDI) ||
        thread->asr == XNTHREAD_INVALID_ASR)
        return;

    xnltt_log_event(rtai_ev_sigdispatch,thread->name,thread->signals);

    /* Start the asynchronous service routine */
    oldmode = testbits(thread->status,XNTHREAD_MODE_BITS);
    sigs = thread->signals;
    asrimask = thread->asrimask;
    asr = thread->asr;

    /* Clear pending signals mask since an ASR can be reentrant */
    thread->signals = 0;

    /* Reset ASR mode bits */
    __clrbits(thread->status,XNTHREAD_MODE_BITS);
    __setbits(thread->status,thread->asrmode);
    thread->asrlevel++;

    /* Setup ASR interrupt mask then fire it. */
    savedmask = xnarch_setimask(asrimask);
    asr(sigs);
    xnarch_setimask(savedmask);

    /* Reset the thread mode bits */
    thread->asrlevel--;
    __clrbits(thread->status,XNTHREAD_MODE_BITS);
    __setbits(thread->status,oldmode);
}

/*!
 * @internal
 * \fn void xnpod_welcome_thread(xnthread_t *thread)
 * \brief Thread prologue.
 *
 * This internal routine is called on behalf of a (re)starting
 * thread's prologue before the user entry point is invoked. This call
 * is reserved for internal housekeeping chores and cannot be inlined.
 */

void xnpod_welcome_thread (xnthread_t *thread)

{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_thrboot,thread->name);

    if (xnthread_signaled_p(thread))
        xnpod_dispatch_signals();

    if (testbits(thread->status,XNLOCK))
        /* Actually grab the scheduler lock. */
        xnpod_lock_sched();

#ifdef CONFIG_RTAI_HW_FPU
   /* When switching to a newly created thread, it is necessary to switch FPU
      contexts, as a replacement for xnpod_schedule epilogue (a newly created
      was not switched out by calling xnpod_schedule, since it is new). */
   if (testbits(thread->status,XNFPU))
       {
       xnsched_t *sched = thread->sched;

        if (sched->fpuholder != NULL &&
            xnarch_fpu_ptr(xnthread_archtcb(sched->fpuholder)) !=
            xnarch_fpu_ptr(xnthread_archtcb(thread)))
            xnarch_save_fpu(xnthread_archtcb(sched->fpuholder));

        xnarch_init_fpu(xnthread_archtcb(thread));

        sched->fpuholder = thread;
        }
#endif /* CONFIG_RTAI_HW_FPU */

    __clrbits(thread->status,XNRESTART);

    xnlock_clear_irqoff(&nklock);

    /* The xnarch trampoline already took care of the interrupt
       mask. */

    splexit(s);
}

#ifdef CONFIG_RTAI_HW_FPU

static inline void __xnpod_switch_fpu (xnsched_t *sched)

{
    xnthread_t *runthread = sched->runthread;

    if (!testbits(runthread->status,XNFPU))
        return;

    if(sched->fpuholder != runthread)
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
    else
        xnarch_enable_fpu(xnthread_archtcb(runthread));
}

/* xnpod_switch_fpu() -- Switches to the current thread's FPU context,
   saving the previous one as needed. */

void xnpod_switch_fpu (xnsched_t *sched)

{
    __xnpod_switch_fpu(sched);
}

#endif /* CONFIG_RTAI_HW_FPU */

/*! 
 * @internal
 * \fn void xnpod_preempt_current_thread(xnsched_t *sched);
 * \brief Preempts the current thread.
 *
 * Preempts the running thread (because a higher priority thread has
 * just been readied).  The thread is re-inserted to the front of its
 * priority group in the ready thread queue. Must must be called
 * with nklock locked, interrupts off.
 */

static inline void xnpod_preempt_current_thread (xnsched_t *sched)

{
    xnthread_t *thread = sched->runthread;

    sched_insertpql(&sched->readyq,&thread->rlink,thread->cprio);
    __setbits(thread->status,XNREADY);

#ifdef __RTAI_SIM__
    if (sched_getheadpq(&sched->readyq) != &thread->rlink)
        nkpod->schedhook(thread,XNREADY);
    else if (sched_countpq(&sched->readyq) > 1)
        {
        /* The running thread is still heading the ready queue and
           more than one thread is linked to this queue, so we may
           refer to the following element as a thread object
           (obviously distinct from the running thread) safely. */
        thread = link2thread(thread->rlink.plink.next,rlink);
        nkpod->schedhook(thread,XNREADY);
        }
#endif /* __RTAI_SIM__ */
}

/*! 
 * \fn void xnpod_schedule(void)
 * \brief Rescheduling procedure entry point.
 *
 * This is the central rescheduling routine which should be called to
 * validate and apply changes which have previously been made to the
 * nucleus scheduling state, such as suspending, resuming or
 * changing the priority of threads.  This call first determines if a
 * thread switch should take place, and performs it as
 * needed. xnpod_schedule() actually switches threads if:
 *
 * - the running thread has been blocked or deleted.
 * - or, the running thread has become less priority than the first
 *   ready to run thread.
 * - or, the running thread does not lead no more the ready threads
 * (round-robin).
 * - or, a real-time thread became ready to run, ending the
 *   scheduler idle state (i.e. The root thread was
 *   running so far).
 *
 * The nucleus implements a lazy rescheduling scheme so that most
 * of the services affecting the threads state MUST be followed by a
 * call to the rescheduling procedure for the new scheduling state to
 * be applied. In other words, multiple changes on the scheduler state
 * can be done in a row, waking threads up, blocking others, without
 * being immediately translated into the corresponding context
 * switches, like it would be necessary would it appear that a more
 * priority thread than the current one became runnable for
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
 * The rescheduling procedure always leads to a null-effect if the
 * scheduler is locked (XNLOCK bit set in the status mask of the
 * running thread), or if it is called on behalf of an ISR or callout.
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
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine, although this leads to a no-op.
 * - Kernel-based task
 * - User-space task
 *
 * @note The switch hooks are called on behalf of the resuming thread.
 */

void xnpod_schedule (void)

{
    xnthread_t *threadout, *threadin, *runthread;
    xnsched_t *sched;
#if defined(CONFIG_SMP) || defined(CONFIG_RTAI_OPT_DEBUG)
    int need_resched;
#endif /* CONFIG_SMP || CONFIG_RTAI_OPT_DEBUG */
    spl_t s;
#ifdef __KERNEL__
#ifdef CONFIG_RTAI_OPT_FUSION
    int shadow;
#endif /* CONFIG_RTAI_OPT_FUSION */

    if (xnarch_escalate())
        return;

    xnltt_log_event(rtai_ev_resched);
#endif /* __KERNEL__ */

    /* No immediate rescheduling is possible if an ISR or callout
       context is active. */

    if (xnpod_callout_p() || xnpod_interrupt_p())
        return;

    xnlock_get_irqsave(&nklock,s);

    sched = xnpod_current_sched();
    runthread = sched->runthread;

#if defined(CONFIG_SMP) || defined(CONFIG_RTAI_OPT_DEBUG)
    need_resched = xnsched_tst_resched(sched);
#endif
#ifdef CONFIG_SMP
    if (need_resched)
        xnsched_clr_resched(sched);

    if (xnsched_resched_p())
        {
        xnarch_send_ipi(xnsched_resched_mask());
        xnsched_clr_mask(sched);
        }

#ifndef CONFIG_RTAI_OPT_DEBUG
    if (!need_resched)
        goto signal_unlock_and_exit;

    xnsched_set_resched(sched);
#else /* !CONFIG_RTAI_OPT_DEBUG */
    if (need_resched)
        xnsched_set_resched(sched);
#endif /* !CONFIG_RTAI_OPT_DEBUG */

#endif /* CONFIG_SMP */

    if (testbits(runthread->status,XNLOCK))
        /* The running thread has locked the scheduler and is still
           ready to run. Just check for (self-posted) pending signals,
           then exit the procedure without actually switching
           contexts. */
        goto signal_unlock_and_exit;

    /* Clear the rescheduling bit */
    xnsched_clr_resched(sched);

    if (!testbits(runthread->status,XNTHREAD_BLOCK_BITS|XNZOMBIE))
        {
        xnpholder_t *pholder = sched_getheadpq(&sched->readyq);
          
        if (pholder)
            {
            xnthread_t *head = link2thread(pholder,rlink);

            if (head == runthread)
                goto do_switch;
            else if (xnpod_priocompare(head->cprio,runthread->cprio) > 0)
                {
                if (!testbits(runthread->status,XNREADY))
                    /* Preempt the running thread */
                    xnpod_preempt_current_thread(sched);

                goto do_switch;
                }
            else if (testbits(runthread->status,XNREADY))
                goto do_switch;
            }

        goto signal_unlock_and_exit;
        }

 do_switch:

    threadout = runthread;
    threadin = link2thread(sched_getpq(&sched->readyq),rlink);

#ifdef CONFIG_RTAI_OPT_DEBUG
    if (!need_resched)
        {
        xnprintf("xnpod_schedule: scheduler state changed without rescheduling"
                 "bit set\nwhen switching from %s to %s\n",
                 runthread->name,
                 threadin->name);
#ifdef __KERNEL__
        show_stack(NULL, NULL);
#endif
        }
#endif /* CONFIG_RTAI_OPT_DEBUG */

    __clrbits(threadin->status,XNREADY);

    if (threadout == threadin &&
        /* Note: the root thread never restarts. */
        !testbits(threadout->status,XNRESTART))
        goto signal_unlock_and_exit;

    xnltt_log_event(rtai_ev_switch,threadout->name,threadin->name);

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    shadow = testbits(threadout->status,XNSHADOW);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    if (testbits(threadout->status,XNZOMBIE))
        xnpod_switch_zombie(threadout, threadin);

    sched->runthread = threadin;

    if (testbits(threadout->status,XNROOT))
        xnarch_leave_root(xnthread_archtcb(threadout));
    else if (testbits(threadin->status,XNROOT)) {
        xnpod_reset_watchdog();
        xnfreesync();
        xnarch_enter_root(xnthread_archtcb(threadin));
    }

    xnthread_inc_csw(threadin);

    xnarch_switch_to(xnthread_archtcb(threadout),
                     xnthread_archtcb(threadin));

#ifdef CONFIG_SMP
    /* If threadout migrated while suspended, sched is no longer correct. */
    sched = xnpod_current_sched();
#endif
    /* Re-read the currently running thread, this is needed because of
     * relaxed/hardened transitions. */
    runthread = sched->runthread;

#ifdef CONFIG_RTAI_HW_FPU
    __xnpod_switch_fpu(sched);
#endif /* CONFIG_RTAI_HW_FPU */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* Shadow on entry and root without shadow extension on exit? 
       Mmmm... This must be the user-space mate of a deleted real-time
       shadow we've just rescheduled in the Linux domain to have it
       exit properly.  Reap it now. */
    if (shadow &&
        testbits(runthread->status,XNROOT) &&
        xnshadow_ptd(current) == NULL)
        {
        xnlock_clear_irqon(&nklock);
        xnshadow_exit();
        }
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

#ifdef __RTAI_SIM__
    if (nkpod->schedhook)
        nkpod->schedhook(runthread,XNRUNNING);
#endif /* __RTAI_SIM__ */
    
    if (countq(&nkpod->tswitchq) > 0 &&
        !testbits(runthread->status,XNTHREAD_SYSTEM_BITS))
        {
        xnltt_log_event(rtai_ev_callout,"SWITCH",runthread->name);
        xnpod_fire_callouts(&nkpod->tswitchq,runthread);
        }

 signal_unlock_and_exit:

    if (xnthread_signaled_p(runthread))
        xnpod_dispatch_signals();

    xnlock_put_irqrestore(&nklock,s);
}

/*! 
 * @internal
 * \fn void xnpod_schedule_runnable(xnthread_t *thread,int flags)
 * \brief Hidden rescheduling procedure.
 *
 * This internal routine should NEVER be used directly by the upper
 * interfaces. It reinserts the given thread into the ready queue then
 * switches to the highest priority runnable thread. It must be called
 * with nklock locked, interrupts off.
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
 *        contexts. This feature is used to preserve the atomicity of some
 *        operations.
 */

void xnpod_schedule_runnable (xnthread_t *thread, int flags)

{
    xnsched_t *sched = thread->sched;
    xnthread_t *runthread = sched->runthread, *threadin;

    xnltt_log_event(rtai_ev_fastsched);

    if (thread != runthread)
        {
        sched_removepq(&sched->readyq,&thread->rlink);

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
                sched_prependpq(&sched->readyq,&runthread->rlink);
            else
                sched_insertpql(&sched->readyq,&runthread->rlink,runthread->cprio);

            __setbits(runthread->status,XNREADY);
            }
        }
    else if (testbits(thread->status,XNTHREAD_BLOCK_BITS|XNZOMBIE))
        /* Same remark as before in the case this routine is called
           with a soon-to-be-blocked running thread as argument. */
        goto maybe_switch;

    if (flags & XNPOD_SCHEDLIFO)
        /* Insert LIFO inside priority group */
        sched_insertpql(&sched->readyq,&thread->rlink,thread->cprio);
    else
        /* Insert FIFO inside priority group */
        sched_insertpqf(&sched->readyq,&thread->rlink,thread->cprio);

    __setbits(thread->status,XNREADY);

maybe_switch:

    if (flags & XNPOD_NOSWITCH)
        {
        xnsched_set_resched(sched);

        if (testbits(runthread->status,XNREADY))
            {
            sched_removepq(&sched->readyq,&runthread->rlink);
            __clrbits(runthread->status,XNREADY);
            }

        return;
        }

    xnsched_clr_resched(sched);

    threadin = link2thread(sched_getpq(&sched->readyq),rlink);

    __clrbits(threadin->status,XNREADY);

    if (threadin == runthread)
        return; /* No switch. */

    if (testbits(runthread->status, XNZOMBIE))
        xnpod_switch_zombie(runthread, threadin);

    sched->runthread = threadin;

    if (testbits(runthread->status,XNROOT))
        xnarch_leave_root(xnthread_archtcb(runthread));
    else if (testbits(threadin->status,XNROOT)) {
        xnpod_reset_watchdog();
        xnfreesync();
        xnarch_enter_root(xnthread_archtcb(threadin));
    }

#ifdef __RTAI_SIM__
    if (nkpod->schedhook)
        nkpod->schedhook(runthread,XNREADY);
#endif /* __RTAI_SIM__ */

    xnthread_inc_csw(threadin);

    xnarch_switch_to(xnthread_archtcb(runthread),
                     xnthread_archtcb(threadin));

#ifdef CONFIG_SMP
    /* If runthread migrated while suspended, sched is no longer correct. */
    sched = xnpod_current_sched();
#endif

#ifdef CONFIG_RTAI_HW_FPU
    __xnpod_switch_fpu(sched);
#endif /* CONFIG_RTAI_HW_FPU */

#ifdef __RTAI_SIM__
    if (nkpod->schedhook && runthread == sched->runthread)
        nkpod->schedhook(runthread,XNRUNNING);
#endif /* __RTAI_SIM__ */
}

/*! 
 * \fn void xnpod_set_time(xnticks_t newtime);
 * \brief Set the nucleus idea of time.
 *
 * The nucleus tracks the current time as a monotonously increasing
 * count of ticks announced by the timer source since the epoch. The
 * epoch is initially the same as the underlying architecture system
 * time. This service changes the epoch. Running timers use a different
 * time base thus are not affected by this operation.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xnpod_set_time (xnticks_t newtime)

{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);
    xnltt_log_event(rtai_ev_timeset,newtime);
    nkpod->wallclock_offset += newtime - xnpod_get_time();
    setbits(nkpod->status,XNTMSET);
    xnlock_put_irqrestore(&nklock,s);
}

/*! 
 * \fn xnticks_t xnpod_get_time(void);
 * \brief Get the nucleus idea of time.
 *
 * This service gets the nucleus (external) clock time.
 *
 * @return The current nucleus time (in ticks) if the underlying time
 * source runs in periodic mode, or the system time (converted to
 * nanoseconds) as maintained by the CPU if aperiodic mode is in
 * effect, or no timer is running.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

xnticks_t xnpod_get_time (void)

{
    /* Return an adjusted value of the monotonic time with the
       wallclock offset as defined in xnpod_set_time(). */
    return nktimer->get_jiffies() + nkpod->wallclock_offset;
}

/*! 
 * \fn int xnpod_add_hook(int type,void (*routine)(xnthread_t *))
 * \brief Install a nucleus hook.
 *
 * The nucleus allows to register user-defined routines which get
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
 * @return 0 is returned on success. Otherwise, one of the following
 * error codes indicates the cause of the failure:
 *
 *         - -EINVAL is returned if type is incorrect.
 *
 *         - -ENOMEM is returned if not enough memory is available
 *         from the system heap to add the new hook.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnpod_add_hook (int type, void (*routine)(xnthread_t *))

{
    xnqueue_t *hookq;
    xnhook_t *hook;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_addhook,type,routine);

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
            err = -EINVAL;
            goto unlock_and_exit;
        }

    hook = xnmalloc(sizeof(*hook));

    if (hook)
        {
        inith(&hook->link);
        hook->routine = routine;
        prependq(hookq,&hook->link);
        }
    else
        err = -ENOMEM;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*! 
 * \fn int xnpod_remove_hook(int type,void (*routine)(xnthread_t *))
 * \brief Remove a nucleus hook.
 *
 * This service removes a nucleus hook previously registered using
 * xnpod_add_hook().
 *
 * @param type Defines the kind of hook to remove among
 * XNHOOK_THREAD_START, XNHOOK_THREAD_DELETE and XNHOOK_THREAD_SWITCH.
 *
 * @param routine The address of the user-supplied routine to remove.
 *
 * @return 0 is returned on success. Otherwise, -EINVAL is returned if
 * type is incorrect or if the routine has never been registered
 * before.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int xnpod_remove_hook (int type, void (*routine)(xnthread_t *))

{
    xnhook_t *hook = NULL;
    xnholder_t *holder;
    xnqueue_t *hookq;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_remhook,type,routine);

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
            goto bad_hook;
        }

    for (holder = getheadq(hookq);
         holder != NULL; holder = nextq(hookq,holder))
        {
        hook = link2hook(holder);

        if (hook->routine == routine)
            {
            removeq(hookq,holder);
            xnfree(hook);
            goto unlock_and_exit;
            }
        }

 bad_hook:

    err = -EINVAL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

void xnpod_check_context (int mask)

{
    xnsched_t *sched;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    sched = xnpod_current_sched();

    if ((mask & XNPOD_ROOT_CONTEXT) && xnpod_root_p())
        goto unlock_and_exit;

    if ((mask & XNPOD_THREAD_CONTEXT) && !xnpod_asynch_p())
        goto unlock_and_exit;

    if ((mask & XNPOD_INTERRUPT_CONTEXT) && sched->inesting > 0)
        goto unlock_and_exit;

    if ((mask & XNPOD_HOOK_CONTEXT) && xnpod_callout_p())
        goto unlock_and_exit;

    xnpod_fatal("illegal context for call: current=%s, mask=0x%x",
                xnpod_asynch_p() ? "ISR/callout" : xnpod_current_thread()->name,
                mask);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
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
 * unhandled by the nucleus and simply propagated.
 *
 * @param fltinfo An opaque pointer to the arch-specific buffer
 * describing the fault. The actual layout is defined by the
 * xnarch_fltinfo_t type in each arch-dependent layer file.
 *
 */

int xnpod_trap_fault (void *fltinfo)

{
    if (nkpod == NULL || (!xnpod_interrupt_p() && xnpod_idle_p()))
        return 0;

    return nkpod->svctable.faulthandler(fltinfo);
}

/*! 
 * \fn int xnpod_start_timer(u_long nstick,xnisr_t tickhandler)
 * \brief Start the system timer.
 *
 * The nucleus needs a time source to provide the time-related
 * services to the upper interfaces. xnpod_start_timer() tunes the
 * timer hardware so that a user-defined routine is called according
 * to a given frequency. On architectures that provide a
 * oneshot-programmable time source, the system timer can operate
 * either in aperiodic or periodic mode. Using the aperiodic mode
 * still allows to run periodic timings over it: the underlying
 * hardware will simply be reprogrammed after each tick by the timer
 * manager using the appropriate interval value (see xntimer_start()).
 *
 * The time interval that elapses between two consecutive invocations
 * of the handler is called a tick.
 *
 * @param nstick The timer period in nanoseconds. XNPOD_DEFAULT_TICK
 * can be used to set this value according to the arch-dependent
 * settings. If this parameter is equal to XN_APERIODIC_TICK, the
 * underlying hardware timer is set to operate in oneshot-programming
 * mode. In this mode, timing accuracy is higher - since it is not
 * rounded to a constant time slice - at the expense of a lesser
 * efficicency when many timers are simultaneously active. The
 * aperiodic mode gives better results in configuration involving a
 * few threads requesting timing services over different time scales
 * that cannot be easily expressed as multiples of a single base tick,
 * or would lead to a waste of high frequency periodical ticks.
 *
 * @param tickhandler The address of the tick handler which will process
 * each incoming tick. XNPOD_DEFAULT_TICKHANDLER can be passed to use
 * the system-defined entry point (i.e. xnpod_announce_tick()). In any
 * case, a user-supplied handler should end up calling
 * xnpod_announce_tick() to inform the nucleus of the incoming
 * tick.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EBUSY is returned if the timer has already been set.
 * xnpod_stop_timer() must be issued before xnpod_start_timer() is
 * called again.
 *
 * - -EINVAL is returned if an invalid null tick handler has been
 * passed, or if the timer precision cannot represent the duration of
 * a single host tick.
 *
 * - -ENODEV is returned if the underlying architecture does not
 * support the requested periodic timing.
 *
 * - -ENOSYS is returned if no active pod exists.
 *
 * Side-effect: A host timing service is started in order to relay the
 * canonical periodical tick to the underlying architecture,
 * regardless of the frequency used for RTAI/fusion's system
 * tick. This routine does not call the rescheduling procedure.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task in secondary mode
 *
 * Rescheduling: never.
 */

int xnpod_start_timer (u_long nstick, xnisr_t tickhandler)

{
    xnticks_t wallclock;
    int err, delta;
    spl_t s;

    if (tickhandler == NULL)
        return -EINVAL;

#ifndef CONFIG_RTAI_HW_PERIODIC_TIMER
    if (nstick != XN_APERIODIC_TICK)
        return -ENODEV; /* No periodic support */
#endif /* CONFIG_RTAI_HW_PERIODIC_TIMER */
        
    xnlock_get_irqsave(&nklock,s);

    if (!nkpod || testbits(nkpod->status,XNPIDLE))
        {
        err = -ENOSYS;
        goto unlock_and_exit;
        }

    if (testbits(nkpod->status,XNTIMED))
        {
        err = -EBUSY;
        goto unlock_and_exit;
        }

#ifdef CONFIG_RTAI_HW_PERIODIC_TIMER
    if (nstick != XN_APERIODIC_TICK) /* Periodic mode. */
        {
        setbits(nkpod->status,XNTMPER);
        /* Pre-calculate the number of ticks per second. */
        nkpod->tickvalue = nstick;
        nkpod->ticks2sec = 1000000000 / nstick;
	xntimer_set_periodic_mode();
        }
    else /* Periodic setup. */
#endif /* CONFIG_RTAI_HW_PERIODIC_TIMER */
        {
        clrbits(nkpod->status,XNTMPER);
        nkpod->tickvalue = 1; /* Virtually the highest precision: 1ns */
        nkpod->ticks2sec = 1000000000;
	xntimer_set_aperiodic_mode();
        }

    if (XNARCH_HOST_TICK > 0 && XNARCH_HOST_TICK < nkpod->tickvalue)
        {
        /* Host tick needed but shorter than the timer precision;
           bad... */
        err = -EINVAL;

unlock_and_exit:

        xnlock_put_irqrestore(&nklock,s);

        return err;
        }

    xnltt_log_event(rtai_ev_tmstart,nstick);

    /* The clock interrupt does not need to be attached since the
       timer service will handle the arch-dependent setup. The IRQ
       number (arg #2) is not used since the IRQ source will be
       attached directly by the arch-dependent layer
       (xnarch_start_timer). */

    xnintr_init(&nkclock,0,tickhandler,NULL,0);

    setbits(nkpod->status,XNTIMED);

#ifdef CONFIG_RTAI_OPT_WATCHDOG
    nkpod->watchdog_reload = xnarch_ns_to_tsc(4000000000LL);
    xnpod_reset_watchdog();
#endif /* CONFIG_RTAI_OPT_WATCHDOG */

    xnlock_put_irqrestore(&nklock,s);

    /* The following service should return the remaining time before
       the next host jiffy elapses, expressed in internal clock
       ticks. Returning zero is always valid and means to use a full
       tick duration; in such a case, the elapsed portion of the
       current tick would be lost, but this is not that critical.
       Negative values are for errors. */

    delta = xnarch_start_timer(nstick,&xnintr_clock_handler);

    if (delta < 0)
        return -ENODEV;

    wallclock = xnpod_ns2ticks(xnarch_get_sys_time());
    
    xnpod_set_time(wallclock + XNARCH_HOST_TICK / nkpod->tickvalue - delta);
    
    if (delta == 0)
        delta = XNARCH_HOST_TICK / nkpod->tickvalue;

    /* When no host ticking service is required for the underlying
       arch, the host timer exists but simply never ticks since
       xntimer_start() is passed a null interval value. CAUTION:
       kernel timers over aperiodic mode can be started by
       xntimer_start() only _after_ the hw timer has been set up
       through xnarch_start_timer(). */

    xntimer_set_sched(&nkpod->htimer, xnpod_sched_slot(XNTIMER_KEEPER_ID));

    xntimer_start(&nkpod->htimer,
                  delta,
                  XNARCH_HOST_TICK / nkpod->tickvalue);
    return 0;
}

/*! 
 * \fn void xnpod_stop_timer(void)
 * \brief Stop the system timer.
 *
 * Stops the system timer previously started by a call to
 * xnpod_start_timer().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task in secondary mode
 *
 * Rescheduling: never.
 */

void xnpod_stop_timer (void)

{
    spl_t s;

    xntimer_freeze();

    xnlock_get_irqsave(&nklock,s);

    if (!nkpod || testbits(nkpod->status,XNPIDLE))
        goto unlock_and_exit;

    xnltt_log_event(rtai_ev_tmstop);

    if (testbits(nkpod->status,XNTIMED))
        {
        clrbits(nkpod->status,XNTIMED|XNTMPER);
        /* NOTE: The nkclock interrupt object is not destroyed on
           purpose since this would be redundant with
           xnarch_stop_timer() called when freezing timers. In any
           case, no resource is associated with this object. */
	xntimer_set_aperiodic_mode();
        }

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

/*! 
 * \fn int xnpod_announce_tick(xnintr_t *intr)
 *
 * \brief Announce a new clock tick.
 *
 * This is the default service routine for clock ticks which performs
 * the necessary housekeeping chores for time-related services managed
 * by the nucleus. In a way or another, this routine must be called
 * to announce each incoming clock tick to the nucleus.
 *
 * @param intr The descriptor address of the interrupt object
 * associated to the timer interrupt.
 *
 * Side-effect: Since this routine manages the round-robin scheduling,
 * the running thread (which has been preempted by the timer
 * interrupt) can be switched out as a result of its time credit being
 * exhausted. The nucleus always calls the rescheduling procedure
 * after the outer interrupt has been processed.
 *
 * @return XN_ISR_HANDLED is always returned.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Interrupt service routine
 *
 * Rescheduling: possible.
 */

int xnpod_announce_tick (xnintr_t *intr)

{
    unsigned cpu;
    spl_t s;

    cpu = xnarch_current_cpu();

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_tmtick,xnpod_current_thread()->name);

#ifdef CONFIG_RTAI_OPT_WATCHDOG
    if (xnarch_get_cpu_tsc() >= nkpod->watchdog_trigger) {
        if (!xnpod_root_p() && nkpod->watchdog_armed) {
            xnltt_log_event(rtai_ev_watchdog,xnpod_current_thread()->name);
            xnprintf("RTAI: watchdog triggered -- suspending runaway thread '%s'\n",
                     xnpod_current_thread()->name);
            xnpod_suspend_thread(xnpod_current_thread(),XNSUSP,XN_INFINITE,NULL);
        } else {
            xnpod_reset_watchdog();
        }
    }
    nkpod->watchdog_armed = !xnpod_root_p();
#endif /* CONFIG_RTAI_OPT_WATCHDOG */

    nktimer->do_tick(); /* Fire the timeouts, if any. */

    /* Do the round-robin processing. */

#ifdef CONFIG_RTAI_HW_PERIODIC_TIMER
    {
    xnthread_t *runthread;

    /* Round-robin in aperiodic mode makes no sense. */
    if (!testbits(nkpod->status,XNTMPER))
        goto unlock_and_exit;

    runthread = xnpod_sched_slot(cpu)->runthread;

    if (testbits(runthread->status,XNRRB) &&
        runthread->rrcredit != XN_INFINITE &&
        !testbits(runthread->status,XNLOCK))
        {
        /* The thread can be preempted and undergoes a round-robin
           scheduling. Round-robin time credit is only consumed by a
           running thread. Thus, if a higher priority thread outside
           the priority group which started the time slicing grabs the
           processor, the current time credit of the preempted thread
           is kept unchanged, and will not be reset when this thread
           resumes execution. */
        
        if (runthread->rrcredit <= 1)
            {
            /* If the time slice is exhausted for the running thread,
               put it back on the ready queue (in last position) and
               reset its credit for the next run. */
            runthread->rrcredit = runthread->rrperiod;
            xnpod_resume_thread(runthread,0);
            }
        else
            runthread->rrcredit--;
        }
    }

 unlock_and_exit:

#endif /* CONFIG_RTAI_HW_PERIODIC_TIMER */

    xnlock_put_irqrestore(&nklock,s);

    return XN_ISR_HANDLED;
}

/*! 
 * \fn int xnpod_set_thread_periodic(xnthread_t *thread,xnticks_t idate,xnticks_t period)
 * \brief Make a thread periodic.
 *
 * Make a thread periodic by programming its first release point and
 * its period in the processor time line.  Subsequent calls to
 * xnpod_wait_thread_period() will delay the thread until the next
 * periodic release point in the processor timeline is reached.
 *
 * @param thread The descriptor address of the affected thread. This
 * thread is immediately delayed until the first periodic release
 * point is reached.
 *
 * @param idate The initial (absolute) date of the first release
 * point, expressed in clock ticks (see note). The affected thread
 * will be delayed until this point is reached. If @a idate is equal
 * to XN_INFINITE, the current system date is used, and no initial
 * delay takes place.

 * @param period The period of the thread, expressed in clock ticks
 * (see note). Passing XN_INFINITE attempts to stop the thread's
 * periodic timer; in the latter case, the routine always exits
 * succesfully, regardless of the pervious state of this timer.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ETIMEDOUT is returned @a idate is different from XN_INFINITE and
 * represents a date in the past.
 *
 * - -EWOULDBLOCK is returned if the system timer has not been
 * started using xnpod_start_timer().
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible if the operation affects the current thread
 * and @a idate has not elapsed yet.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the xnpod_start_timer() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

int xnpod_set_thread_periodic (xnthread_t *thread,
                               xnticks_t idate,
                               xnticks_t period)
{
    xnticks_t now;
    int err = 0;
    spl_t s;

    if (!testbits(nkpod->status,XNTIMED))
        return -EWOULDBLOCK;

    xnlock_get_irqsave(&nklock,s);

    xnltt_log_event(rtai_ev_thrperiodic,thread->name,idate,period);

    if (period == XN_INFINITE)
        {
        if (xntimer_running_p(&thread->ptimer))
            xntimer_stop(&thread->ptimer);

        goto unlock_and_exit;
        }

    xntimer_set_sched(&thread->ptimer, thread->sched);

    if (idate == XN_INFINITE)
        xntimer_start(&thread->ptimer,period,period);
    else
        {
        now = xnpod_get_time();

        if (idate > now)
	    {
	    xntimer_start(&thread->ptimer,idate - now,period);
            xnpod_suspend_thread(thread,XNDELAY,XN_INFINITE,NULL);
	    }
        else
            err = -ETIMEDOUT;
        }

 unlock_and_exit:

    thread->poverrun = -1;

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int xnpod_wait_thread_period(void)
 * @brief Wait for the next periodic release point.
 *
 * Make the current thread wait for the next periodic release point in
 * the processor time line.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EWOULDBLOCK is returned if xnpod_set_thread_periodic() has not
 * previously been called for the calling thread.
 *
 * - -EINTR is returned if xnpod_unblock_thread() has been called for
 * the waiting thread before the next periodic release point has been
 * reached. In this case, the overrun counter is reset too.
 *
 * - -ETIMEDOUT is returned if a timer overrun occurred, which
 * indicates that a previous release point has been missed by the
 * calling thread.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: always, unless an overrun has been detected.  In the
 * latter case, the current thread immediately returns from this
 * service without being delayed.
 */

int xnpod_wait_thread_period (void)

{
    xnthread_t *thread;
    int err = 0;
    spl_t s;

    thread = xnpod_current_thread();

    xnlock_get_irqsave(&nklock,s);

    if (!xntimer_running_p(&thread->ptimer))
        {
        err = -EWOULDBLOCK;
        goto unlock_and_exit;
        }

    xnltt_log_event(rtai_ev_thrwait,thread->name);

    if (thread->poverrun < 0)
        {
        xnpod_suspend_thread(thread,XNDELAY,XN_INFINITE,NULL);

        if (xnthread_test_flags(thread,XNBREAK))
            {
	    thread->poverrun = -1;
            err = -EINTR;
            goto unlock_and_exit;
            }

	if (thread->poverrun > 0)
	    err = -ETIMEDOUT;
        }
    else
        err = -ETIMEDOUT;

    thread->poverrun--;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
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
EXPORT_SYMBOL(xnpod_migrate_thread);
EXPORT_SYMBOL(xnpod_remove_hook);
EXPORT_SYMBOL(xnpod_renice_thread);
EXPORT_SYMBOL(xnpod_restart_thread);
EXPORT_SYMBOL(xnpod_resume_thread);
EXPORT_SYMBOL(xnpod_rotate_readyq);
EXPORT_SYMBOL(xnpod_schedule);
EXPORT_SYMBOL(xnpod_schedule_runnable);
EXPORT_SYMBOL(xnpod_set_thread_mode);
EXPORT_SYMBOL(xnpod_set_thread_periodic);
EXPORT_SYMBOL(xnpod_set_time);
EXPORT_SYMBOL(xnpod_shutdown);
EXPORT_SYMBOL(xnpod_start_thread);
EXPORT_SYMBOL(xnpod_start_timer);
EXPORT_SYMBOL(xnpod_stop_timer);
EXPORT_SYMBOL(xnpod_suspend_thread);
EXPORT_SYMBOL(xnpod_trap_fault);
EXPORT_SYMBOL(xnpod_unblock_thread);
EXPORT_SYMBOL(xnpod_wait_thread_period);
EXPORT_SYMBOL(xnpod_welcome_thread);

EXPORT_SYMBOL(nkclock);
EXPORT_SYMBOL(nkpod);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(nklock);
#endif /* CONFIG_SMP */
