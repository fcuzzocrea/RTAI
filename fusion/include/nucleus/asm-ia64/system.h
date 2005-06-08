/*
 * Copyright &copy; 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 * Copyright &copy; 2004 The HYADES project <http://www.hyades-itea.org>
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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

#ifndef _RTAI_ASM_IA64_SYSTEM_H
#define _RTAI_ASM_IA64_SYSTEM_H

#include <nucleus/asm-generic/system.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/ptrace.h>

#if ADEOS_RELEASE_NUMBER < 0x0206070a
#error "Adeos 2.6r7c10/ia64 or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/ia64/"
#endif

#ifdef CONFIG_IA64_HP_SIM
#define XNARCH_DEFAULT_TICK    31250000 /* ns, i.e. 31ms */
#else
#define XNARCH_DEFAULT_TICK    XNARCH_HOST_TICK
#endif
#define XNARCH_HOST_TICK       (1000000000UL/HZ)

#define XNARCH_THREAD_STACKSZ  (1<<KERNEL_STACK_SIZE_ORDER)

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)

void *xnarch_alloc_stack(unsigned long stacksize);
void xnarch_free_stack(void *block);

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* Kernel mode side */

    unsigned long *espp;	/* Pointer to ESP backup area (&esp or
                                   &user->thread.esp).
                                   DONT MOVE THIS MEMBER,
                                   switch_to depends on it. */

    struct ia64_fpreg fpuenv[96]; /* FIXME FPU: check if alignment constraints
                                     are needed. */
    
    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */
    unsigned long esp;		/* Saved ESP for kernel-based threads */

    /* User mode side */

    struct task_struct *user_task;	/* Shadowed user-space task */
    struct task_struct *active_task;	/* Active user-space task */

    struct ia64_fpreg *fpup;
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)

} xnarchtcb_t;

typedef struct xnarch_fltinfo {

    ia64trapinfo_t ia64;
    unsigned trap;

} xnarch_fltinfo_t;

#define xnarch_fault_trap(fi)  ((fi)->trap)
#define xnarch_fault_code(fi)  ((fi)->ia64.isr)
#define xnarch_fault_pc(fi)    ((fi)->ia64.regs->cr_iip)
/* fault is caused by use FPU while FPU disabled. */
#define xnarch_fault_fpu_p(fi) ((fi)->trap == ADEOS_FPDIS_TRAP)
/* The following predicate is guaranteed to be called over a regular
   Linux stack context. */
#define xnarch_fault_notify(fi) (!(current->ptrace & PT_PTRACED) || \
				 (fi)->trap != ADEOS_DEBUG_TRAP)
#ifdef __cplusplus
extern "C" {
#endif

static inline void *xnarch_sysalloc (u_long bytes)

{
    if (bytes >= 128*1024)
	return vmalloc(bytes);

    return kmalloc(bytes,GFP_KERNEL);
}

static inline void xnarch_sysfree (void *chunk, u_long bytes)

{
    if (bytes >= 128*1024)
	vfree(chunk);
    else
	kfree(chunk);
}

static inline void xnarch_relay_tick (void)

{
#ifdef CONFIG_SMP
    adeos_send_ipi(RTHAL_HOST_TIMER_IRQ, cpu_online_map);
#else /* ! CONFIG_SMP */
    adeos_trigger_irq(RTHAL_HOST_TIMER_IRQ);
#endif
}

#ifdef XENO_POD_MODULE

void xnpod_welcome_thread(struct xnthread *);

void xnpod_delete_thread(struct xnthread *);

static inline int xnarch_start_timer (unsigned long ns,
				      void (*tickhandler)(void))
{
    int err = rthal_timer_request(tickhandler,ns);
    adeos_declare_cpuid;
    long long delta;

    if (err)
        return err;

    adeos_load_cpuid();
    delta = __adeos_itm_next[cpuid] - ia64_get_itc();
    
    return delta < 0LL ? 0LL : xnarch_tsc_to_ns(delta);
}

static inline void xnarch_leave_root (xnarchtcb_t *rootcb)

{
    struct task_struct *fpu_owner
        = (struct task_struct *)ia64_get_kr(IA64_KR_FPU_OWNER);
    adeos_declare_cpuid;

    adeos_load_cpuid();

    __set_bit(cpuid,&rthal_cpu_realtime);
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = rootcb->active_task = rthal_root_host_task(cpuid);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
    rootcb->fpup = fpu_owner ? fpu_owner->thread.fph : NULL;
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb)
{
    __clear_bit(xnarch_current_cpu(),&rthal_cpu_realtime);
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *outproc = out_tcb->active_task;
    struct task_struct *inproc = in_tcb->user_task;

    in_tcb->active_task = inproc ?: outproc;

    if (inproc && inproc != outproc)
        {
        /* We are switching to a user task different from the last
           preempted or running user task, so that we can use the
           Linux context switch routine. */
        struct mm_struct *oldmm = outproc->active_mm;
        struct task_struct *last;

        switch_mm(oldmm,inproc->active_mm,inproc);

        if (!inproc->mm)
            enter_lazy_tlb(oldmm,inproc);

        __switch_to(outproc, inproc, last);
        }
    else
        {
        /* Use our own light switch routine. */
        unsigned long gp;

        ia64_stop();
        gp = ia64_getreg(_IA64_REG_GP);
        ia64_stop();
        rthal_switch_context(out_tcb,in_tcb);
        ia64_stop();
        ia64_setreg(_IA64_REG_GP, gp);
        ia64_stop();

	/* fph will be enabled by xnarch_restore_fpu if needed, and
	   returns the root thread in its usual mode. */
        ia64_fph_disable();
        }
}

static inline void xnarch_finalize_and_switch (xnarchtcb_t *dead_tcb,
					       xnarchtcb_t *next_tcb)
{
    xnarch_switch_to(dead_tcb,next_tcb);
}

static inline void xnarch_finalize_no_switch (xnarchtcb_t *dead_tcb)

{
    /* Empty */
}

#define fph2task(faddr)                                 \
    ((struct task_struct *)((char *) (faddr) -          \
                            (size_t) &((struct task_struct *) 0)->thread.fph[0]))

#define xnarch_fpu_init_p(task) ((task)->thread.flags & IA64_THREAD_FPH_VALID)

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{
    /* Initialize the FPU for an emerging kernel-based RT thread. This
       must be run on behalf of the emerging thread. */
    ia64_fph_enable();
    __ia64_init_fpu();
    /* The mfh bit is automatically armed, since the init_fpu routine
       modifies the FPH registers. */
}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)
{
    unsigned long lpsr = ia64_getreg(_IA64_REG_PSR);
    struct ia64_psr *current_psr = (struct ia64_psr *) &lpsr;
    
    if (current_psr->mfh)
        {
        if(tcb->user_task && tcb->fpup)
            {
            struct task_struct *linux_fpu_owner = fph2task(tcb->fpup);
            struct ia64_psr *psr = ia64_psr(ia64_task_regs(linux_fpu_owner));

            /* Keep the FPU save zone in sync with what Linux expects. */
            psr->mfh = 0;
            linux_fpu_owner->thread.flags |= IA64_THREAD_FPH_VALID;
            }

        ia64_fph_enable();
        __ia64_save_fpu(tcb->fpup);
        ia64_rsm(IA64_PSR_MFH);
        ia64_srlz_d();
        }
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
    struct task_struct *linux_fpu_owner;
    int need_disabled_fph;

    if (tcb->user_task && tcb->fpup)
        {
        linux_fpu_owner = fph2task(tcb->fpup);

        if(!xnarch_fpu_init_p(linux_fpu_owner))
            return;	/* Uninit fpu area -- do not restore. */

        /* Disable fph, if we are not switching back to the task which
           owns the FPU. */
        need_disabled_fph = linux_fpu_owner != tcb->user_task;
        }
    else
        need_disabled_fph = 0;

    /* Restore the FPU hardware with valid fp registers from a
       user-space or kernel thread. */
    ia64_fph_enable();
    __ia64_load_fpu(tcb->fpup);
    ia64_rsm(IA64_PSR_MFH);
    ia64_srlz_d();

    if(need_disabled_fph)
        ia64_fph_disable();
}


static inline void xnarch_enable_fpu(xnarchtcb_t *tcb)
{
    ia64_fph_enable();
}

static inline void xnarch_init_root_tcb (xnarchtcb_t *tcb,
					 struct xnthread *thread,
					 const char *name)
{
    tcb->user_task = current;
    tcb->active_task = NULL;
    tcb->espp = &tcb->esp;
    tcb->fpup = current->thread.fph;
}

static void xnarch_thread_trampoline (struct xnthread *self,
				      int imask,
				      void(*entry)(void *),
				      void *cookie)
{
    /* xnpod_welcome_thread() will do ia64_fpu_enable() if needed. */
    ia64_fph_disable();
    rthal_local_irq_restore(!!imask);
    rthal_hw_enable();
    xnpod_welcome_thread(self);
    entry(cookie);
    xnpod_delete_thread(self);
}

static inline void xnarch_init_thread (xnarchtcb_t *tcb,
				       void (*entry)(void *),
				       void *cookie,
				       int imask,
				       struct xnthread *thread,
				       char *name)
{
    unsigned long rbs,bspstore,child_stack,child_rbs,rbs_size;
    unsigned long stackbase = (unsigned long) tcb->stackbase;
    struct switch_stack *swstack;    

    tcb->esp = 0;
    
    /* the stack should have already been allocated */   
    rthal_prepare_stack(stackbase+KERNEL_STACK_SIZE);

    /* The value of esp is used as a marker to indicate whether we are
       initializing a new task or we are back from the context switch. */

    if (tcb->esp != 0)
        xnarch_thread_trampoline(thread, imask, entry, cookie);

    child_stack = stackbase + KERNEL_STACK_SIZE - IA64_SWITCH_STACK_SIZE;
    tcb->esp = child_stack;
    swstack = (struct switch_stack *)child_stack;
    bspstore = swstack->ar_bspstore;

    rbs = (ia64_getreg(_IA64_REG_SP) & ~(KERNEL_STACK_SIZE-1)) + IA64_RBS_OFFSET;
    child_rbs = stackbase + IA64_RBS_OFFSET;
    rbs_size = bspstore - rbs;

    memcpy((void *)child_rbs,(void *)rbs,rbs_size);
    swstack->ar_bspstore = child_rbs + rbs_size;
    tcb->esp -= 16 ;	/* Provide for the (bloody) scratch area... */
}

#ifdef CONFIG_SMP

static inline int xnarch_send_ipi (xnarch_cpumask_t cpumask) {

    return adeos_send_ipi(ADEOS_SERVICE_IPI0, cpumask);
}

static inline int xnarch_hook_ipi (void (*handler)(void))

{
    return adeos_virtualize_irq_from(&rthal_domain,
                                     ADEOS_SERVICE_IPI0,
                                     (void (*)(unsigned)) handler,
                                     NULL,
                                     IPIPE_HANDLE_MASK);
}

static inline int xnarch_release_ipi (void)

{
    return adeos_virtualize_irq_from(&rthal_domain,
                                     ADEOS_SERVICE_IPI0,
                                     NULL,
                                     NULL,
                                     IPIPE_PASS_MASK);
}

static struct semaphore xnarch_finalize_sync;

static void xnarch_finalize_cpu(unsigned irq)
{
    up(&xnarch_finalize_sync);
}

static inline void xnarch_notify_halt(void)

{
    unsigned cpu, nr_cpus = num_online_cpus();
    cpumask_t other_cpus = cpu_online_map;
    unsigned long flags;
    adeos_declare_cpuid;

    init_MUTEX_LOCKED(&xnarch_finalize_sync);

    /* Here adp_current is in fact root, since xnarch_notify_halt is
       called from xnpod_shutdown, itself called from Linux
       context. */
    adeos_virtualize_irq_from(adp_current, ADEOS_SERVICE_IPI2,
                              xnarch_finalize_cpu, NULL, IPIPE_HANDLE_MASK);

    adeos_lock_cpu(flags);
    cpu_clear(cpuid, other_cpus);
    adeos_send_ipi(ADEOS_SERVICE_IPI2, other_cpus);
    adeos_unlock_cpu(flags);

    for(cpu=0; cpu < nr_cpus-1; ++cpu)
        down(&xnarch_finalize_sync);
    
    adeos_virtualize_irq_from(adp_current, ADEOS_SERVICE_IPI2, NULL, NULL,
                              IPIPE_PASS_MASK);
}

#else /* !CONFIG_SMP */

static inline int xnarch_send_ipi (xnarch_cpumask_t cpumask)

{
    return 0;
}

static inline int xnarch_hook_ipi (void (*handler)(void))

{
    return 0;
}

static inline int xnarch_release_ipi (void)

{
    return 0;
}

#define xnarch_notify_halt() /* Nullified */

#endif /* CONFIG_SMP */

static inline void xnarch_notify_shutdown(void)

{
#ifdef CONFIG_SMP
    /* The HAL layer also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */
#ifdef CONFIG_RTAI_OPT_FUSION
    xnshadow_release_events();
#endif /* CONFIG_RTAI_OPT_FUSION */
    /* Wait for the currently processed events to drain. */
    set_current_state(TASK_UNINTERRUPTIBLE);
    schedule_timeout(50);
    xnarch_release_ipi();
}

static inline int xnarch_escalate (void)

{
    extern int xnarch_escalation_virq;

    if (adp_current == adp_root)
	{
	spl_t s;
	splsync(s);
	adeos_trigger_irq(xnarch_escalation_virq);
	splexit(s);
	return 1;
	}

    return 0;
}

static void xnarch_notify_ready (void)

{
#ifdef CONFIG_RTAI_OPT_FUSION
    xnshadow_grab_events();
#endif /* CONFIG_RTAI_OPT_FUSION */
}

#endif /* XENO_POD_MODULE */

#ifdef XENO_THREAD_MODULE

static inline void xnarch_init_tcb (xnarchtcb_t *tcb)
{
    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->espp = &tcb->esp;
    tcb->fpup = tcb->fpuenv;
    /* Must be followed by xnarch_init_thread(). */
}

#endif /* XENO_THREAD_MODULE */

#ifdef XENO_SHADOW_MODULE

static inline void xnarch_init_shadow_tcb (xnarchtcb_t *tcb,
					   struct xnthread *thread,
					   const char *name)
{
    struct task_struct *task = current;

    tcb->user_task = task;
    tcb->active_task = NULL;
    tcb->esp = 0;
    tcb->espp = &task->thread.ksp;
    tcb->fpup = task->thread.fph;
}

static inline void xnarch_grab_xirqs (void (*handler)(unsigned irq))

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	adeos_virtualize_irq(irq,
			     handler,
			     NULL,
			     IPIPE_DYNAMIC_MASK);
}

static inline void xnarch_lock_xirqs (adomain_t *adp, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	{
	unsigned vector = __ia64_local_vector_to_irq(irq);

	switch (vector)
	    {
#ifdef CONFIG_SMP
	    case ADEOS_CRITICAL_VECTOR:
	    case IA64_IPI_RESCHEDULE:
	    case IA64_IPI_VECTOR:

		/* Never lock out these ones. */
		continue;
#endif /* CONFIG_SMP */

	    default:

		__adeos_lock_irq(adp,cpuid,irq);
	    }
	}
}

static inline void xnarch_unlock_xirqs (adomain_t *adp, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	{
	unsigned vector = local_vector_to_irq(irq);

	switch (vector)
	    {
#ifdef CONFIG_SMP
	    case ADEOS_CRITICAL_VECTOR:
	    case IA64_IPI_RESCHEDULE:
	    case IA64_IPI_VECTOR:

		continue;
#endif /* CONFIG_SMP */

	    default:

		__adeos_unlock_irq(adp,irq);
	    }
	}
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay)
{
    rthal_timer_program_shot(delay);
}

static inline void xnarch_stop_timer (void)
{
    rthal_timer_release();
}

static inline int xnarch_send_timer_ipi (xnarch_cpumask_t mask)

{
    return adeos_send_ipi(RTHAL_TIMER_IRQ, mask);
}

static inline void xnarch_read_timings (unsigned long long *shot,
					unsigned long long *delivery,
					unsigned long long defval)
{
#ifdef CONFIG_ADEOS_PROFILING
    int cpuid = adeos_processor_id();
    *shot = __adeos_profile_data[cpuid].irqs[RTHAL_TIMER_IRQ].t_handled;
    *delivery = __adeos_profile_data[cpuid].irqs[RTHAL_TIMER_IRQ].t_synced;
#else /* !CONFIG_ADEOS_PROFILING */
    *shot = defval;
    *delivery = defval;
#endif /* CONFIG_ADEOS_PROFILING */
}

#endif /* XENO_TIMER_MODULE */

#ifdef XENO_MAIN_MODULE

#include <linux/init.h>
#include <nucleus/asm/calibration.h>

extern u_long nkschedlat;

extern u_long nktimerlat;

int xnarch_escalation_virq;

int xnpod_trap_fault(xnarch_fltinfo_t *fltinfo);

void xnpod_schedule_handler(void);

static rthal_trap_handler_t xnarch_old_trap_handler;

typedef struct xnarch_stack {
    struct xnarch_stack *next;
} xnarch_stack_t;

#ifdef CONFIG_SMP
static xnlock_t xnarch_stacks_lock = XNARCH_LOCK_UNLOCKED;
#endif
static atomic_counter_t xnarch_allocated_stacks;

static xnarch_stack_t xnarch_free_stacks_q;
static atomic_counter_t xnarch_free_stacks_count;

static int xnarch_trap_fault (adevinfo_t *evinfo)

{
    xnarch_fltinfo_t fltinfo;

    fltinfo.trap = evinfo->event;
    fltinfo.ia64 = *(ia64trapinfo_t *)evinfo->evdata;

    return xnpod_trap_fault(&fltinfo);
}

unsigned long xnarch_calibrate_timer (void)

{
#if CONFIG_RTAI_HW_TIMER_LATENCY != 0
    return xnarch_ns_to_tsc(CONFIG_RTAI_HW_TIMER_LATENCY);
#else /* CONFIG_RTAI_HW_TIMER_LATENCY unspecified. */
    /* Compute the time needed to program the ITM in aperiodic
       mode. The return value is expressed in CPU ticks. */
    return xnarch_ns_to_tsc(rthal_timer_calibrate()) ?: 1;
#endif /* CONFIG_RTAI_HW_TIMER_LATENCY != 0 */
}

int xnarch_calibrate_sched (void)

{
    nktimerlat = xnarch_calibrate_timer();

    if (!nktimerlat)
	return -ENODEV;

    nkschedlat = xnarch_ns_to_tsc(xnarch_get_sched_latency());

    return 0;
}

static inline void stacksq_push(xnarch_stack_t *q, xnarch_stack_t *stack)
{
    stack->next = q->next;
    q->next = stack;
}

static inline xnarch_stack_t *stacksq_pop(xnarch_stack_t *q)
{
    xnarch_stack_t *stack = q->next;

    if(stack)
        q->next = stack->next;

    return stack;
}

void *xnarch_alloc_stack(unsigned long stacksize)

{
    xnarch_stack_t *stack;
    spl_t s;

    if (stacksize > KERNEL_STACK_SIZE)
        return NULL;

    if (adp_current == adp_root &&
        atomic_read(&xnarch_free_stacks_count) <= CONFIG_RTAI_HW_STACK_POOL)
        {
        stack = (xnarch_stack_t *)
            __get_free_pages(GFP_KERNEL,KERNEL_STACK_SIZE_ORDER);

        if(stack)
            atomic_inc(&xnarch_allocated_stacks);

        return stack;
        }

    xnlock_get_irqsave(&xnarch_stacks_lock, s);
    stack = stacksq_pop(&xnarch_free_stacks_q);
    xnlock_put_irqrestore(&xnarch_stacks_lock, s);

    if (stack)
        atomic_dec(&xnarch_free_stacks_count);

    return stack;
}

void xnarch_free_stack(void *block)

{
    xnarch_stack_t *stack = (xnarch_stack_t *) block;
    spl_t s;

    if (!stack)
        return;

    if (adp_current == adp_root
        && atomic_read(&xnarch_free_stacks_count) > CONFIG_RTAI_HW_STACK_POOL)
        {
        atomic_dec(&xnarch_allocated_stacks);
            
        free_pages((unsigned long) block,KERNEL_STACK_SIZE_ORDER);

        return ;
        }

    xnlock_get_irqsave(&xnarch_stacks_lock, s);
    stacksq_push(&xnarch_free_stacks_q, stack);
    xnlock_put_irqrestore(&xnarch_stacks_lock, s);
    
    atomic_inc(&xnarch_free_stacks_count);
}

static int xnarch_stack_pool_init(void)

{
    while (atomic_read(&xnarch_free_stacks_count) < CONFIG_RTAI_HW_STACK_POOL)
        {
        void *stack = xnarch_alloc_stack(KERNEL_STACK_SIZE);

        if(!stack)
            return -ENOMEM;

        xnarch_free_stack(stack);
        }

    return 0;
}

static void xnarch_stack_pool_destroy(void)

{
    xnarch_stack_t *stack;

    stack = stacksq_pop(&xnarch_free_stacks_q);

    while (stack)
        {
        free_pages((unsigned long) stack, KERNEL_STACK_SIZE_ORDER);
        stack = stacksq_pop(&xnarch_free_stacks_q);

        if(atomic_dec_and_test(&xnarch_allocated_stacks))
            break;
        }

    if (atomic_read(&xnarch_allocated_stacks) != 0)
        xnarch_logwarn("leaked %u kernel threads stacks.\n",
                       atomic_read(&xnarch_allocated_stacks));

    if (xnarch_free_stacks_q.next)
        xnarch_logwarn("kernel threads stacks pool corrupted.\n");
}

static inline int xnarch_init (void)

{
    int err;

#ifdef CONFIG_SMP
    /* The HAL layer also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */

    err = xnarch_calibrate_sched();

    if (err)
	return err;

    xnarch_escalation_virq = adeos_alloc_irq();

    if (xnarch_escalation_virq == 0)
	return -ENOSYS;

    adeos_virtualize_irq_from(&rthal_domain,
			      xnarch_escalation_virq,
			      (void (*)(unsigned))&xnpod_schedule_handler,
			      NULL,
			      IPIPE_HANDLE_MASK);

    xnarch_old_trap_handler = rthal_trap_catch(&xnarch_trap_fault);

#ifdef CONFIG_RTAI_OPT_FUSION
    err = xnshadow_mount();
#endif /* CONFIG_RTAI_OPT_FUSION */

    if (err)
	goto release_trap;

    err = xnarch_stack_pool_init();

    if (!err)
	return 0;

#ifdef CONFIG_RTAI_OPT_FUSION
    xnshadow_cleanup();
#endif /* CONFIG_RTAI_OPT_FUSION */

 release_trap:
    rthal_trap_catch(xnarch_old_trap_handler);
    adeos_free_irq(xnarch_escalation_virq);

    return err;
}

static inline void xnarch_exit (void)

{
#ifdef CONFIG_RTAI_OPT_FUSION
    xnshadow_cleanup();
#endif /* CONFIG_RTAI_OPT_FUSION */
    rthal_trap_catch(xnarch_old_trap_handler);
    adeos_free_irq(xnarch_escalation_virq);
    xnarch_stack_pool_destroy();
}

#endif /* XENO_MAIN_MODULE */

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#include <nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_RTAI_ASM_IA64_SYSTEM_H */
