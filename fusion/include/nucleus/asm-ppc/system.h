/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _RTAI_ASM_PPC_SYSTEM_H
#define _RTAI_ASM_PPC_SYSTEM_H

#include <nucleus/asm-generic/system.h>

#ifdef __KERNEL__

#if ADEOS_RELEASE_NUMBER < 0x02060701
#error "Adeos 2.6r7c1/ppc or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/ppc/"
#endif

#define XNARCH_PASSTHROUGH_IRQS /*empty*/

#define XNARCH_DEFAULT_TICK     1000000 /* ns, i.e. 1ms */
#define XNARCH_HOST_TICK        (1000000000UL/HZ)

#define XNARCH_THREAD_STACKSZ   4096

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_user_task(tcb)   ((tcb)->user_task)

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* Kernel mode side */

#ifdef CONFIG_RTAI_HW_FPU
    /* We only care for basic FPU handling in kernel-space; Altivec
       and SPE are not available to kernel-based nucleus threads. */
    rthal_fpenv_t fpuenv  __attribute__ ((aligned (16)));
    rthal_fpenv_t *fpup;	/* Pointer to the FPU backup area */
    struct task_struct *user_fpu_owner;
    /* Pointer the the FPU owner in userspace:
       - NULL for RT K threads,
       - last_task_used_math for Linux US threads (only current or NULL when MP)
       - current for RT US threads.
    */
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#else /* !CONFIG_RTAI_HW_FPU */
#define xnarch_fpu_ptr(tcb)     NULL
#endif /* CONFIG_RTAI_HW_FPU */

    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */
    unsigned long ksp;		/* Saved KSP for kernel-based threads */
    unsigned long *kspp;	/* Pointer to saved KSP (&ksp or &user->thread.ksp) */

    /* User mode side */
    struct task_struct *user_task;	/* Shadowed user-space task */
    struct task_struct *active_task;	/* Active user-space task */

    /* Init block */
    struct xnthread *self;
    int imask;
    const char *name;
    void (*entry)(void *cookie);
    void *cookie;

} xnarchtcb_t;

typedef struct xnarch_fltinfo {

    struct pt_regs *regs;

} xnarch_fltinfo_t;

#define xnarch_fault_trap(fi)  ((unsigned int)(fi)->regs->trap)
#define xnarch_fault_code(fi)  ((fi)->regs->dar)
#define xnarch_fault_pc(fi)    ((fi)->regs->nip)

#ifdef __cplusplus
extern "C" {
#endif

static inline void *xnarch_sysalloc (u_long bytes)

{
#if 0	/* FIXME: likely on-demand mapping bug here */
    if (bytes >= 128*1024)
	return vmalloc(bytes);
#endif

    return kmalloc(bytes,GFP_KERNEL);
}

static inline void xnarch_sysfree (void *chunk, u_long bytes)

{
#if 0	/* FIXME: likely on-demand mapping bug here */
    if (bytes >= 128*1024)
	vfree(chunk);
    else
#endif
	kfree(chunk);
}

static inline void xnarch_relay_tick (void)

{
    rthal_irq_host_pend(ADEOS_TIMER_VIRQ);
}

#ifdef XENO_POD_MODULE

void xnpod_welcome_thread(struct xnthread *);

void xnpod_delete_thread(struct xnthread *);

static inline int xnarch_start_timer (unsigned long ns,
				      void (*tickhandler)(void))
{
    return rthal_timer_request(tickhandler,ns);
}

static inline void xnarch_leave_root (xnarchtcb_t *rootcb)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    /* rthal_cpu_realtime is only tested for the current processor,
       and always inside a critical section. */
    __set_bit(cpuid,&rthal_cpu_realtime);
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = rootcb->active_task = rthal_current_host_task(cpuid);
#ifdef CONFIG_RTAI_HW_FPU
    rootcb->user_fpu_owner = rthal_get_fpu_owner(rootcb->user_task);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
    rootcb->fpup = (rootcb->user_fpu_owner
                    ? (rthal_fpenv_t *)&rootcb->user_fpu_owner->thread.fpr[0]
                    : NULL);
#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb) {
    __clear_bit(xnarch_current_cpu(),&rthal_cpu_realtime);
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *prev = out_tcb->active_task;
    struct task_struct *next = in_tcb->user_task;

    in_tcb->active_task = next ?: prev;

    if (next && next != prev) /* Switch to new user-space thread? */
	{
	struct mm_struct *mm = next->active_mm;

	/* Switch the mm context.*/

#ifdef CONFIG_ALTIVEC
	/* Don't rely on FTR fixups --
	   they don't work properly in our context. */
	if (cur_cpu_spec[0]->cpu_features & CPU_FTR_ALTIVEC) {
	    asm volatile (
		"dssall;\n"
#ifndef CONFIG_POWER4
		"sync;\n"
#endif
		: : );
	}
#endif /* CONFIG_ALTIVEC */

	next->thread.pgdir = mm->pgd;
	get_mmu_context(mm);
	set_context(mm->context,mm->pgd);

	/* _switch expects a valid "current" (r2) for storing
	 * ALTIVEC and SPE state. */
	current = prev;
        _switch(&prev->thread, &next->thread);

	barrier();
	}
    else
        /* Kernel-to-kernel context switch. */
        rthal_switch_context(out_tcb->kspp,in_tcb->kspp);
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

static inline void xnarch_init_root_tcb (xnarchtcb_t *tcb,
					 struct xnthread *thread,
					 const char *name)
{
    tcb->user_task = current;
    tcb->active_task = NULL;
    tcb->ksp = 0;
    tcb->kspp = &tcb->ksp;
#ifdef CONFIG_RTAI_HW_FPU
    tcb->user_fpu_owner = NULL;
    tcb->fpup = NULL;
#endif /* CONFIG_RTAI_HW_FPU */
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

asmlinkage static void xnarch_thread_trampoline (xnarchtcb_t *tcb)

{
    rthal_local_irq_restore(!!tcb->imask);
    xnpod_welcome_thread(tcb->self);
    tcb->entry(tcb->cookie);
    xnpod_delete_thread(tcb->self);
}

static inline void xnarch_init_thread (xnarchtcb_t *tcb,
				       void (*entry)(void *),
				       void *cookie,
				       int imask,
				       struct xnthread *thread,
				       char *name)
{
    unsigned long *ksp, flags;

    adeos_hw_local_irq_flags(flags);

    *tcb->stackbase = 0;
    ksp = (unsigned long *)((((unsigned long)tcb->stackbase + tcb->stacksize - 0x10) & ~0xf) - RTHAL_SWITCH_FRAME_SIZE);
    tcb->ksp = (unsigned long)ksp - STACK_FRAME_OVERHEAD;
    ksp[19] = (unsigned long)tcb; /* r3 */
    ksp[25] = (unsigned long)&xnarch_thread_trampoline; /* lr */
    ksp[26] = flags & ~(MSR_EE | MSR_FP); /* msr */

    tcb->entry = entry;
    tcb->cookie = cookie;
    tcb->self = thread;
    tcb->imask = imask;
    tcb->name = name;
}

static inline void xnarch_enable_fpu (xnarchtcb_t *current_tcb)

{
#ifdef CONFIG_RTAI_HW_FPU
    if(!current_tcb->user_task)
        rthal_enable_fpu();
#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_RTAI_HW_FPU
    /* Initialize the FPU for an emerging kernel-based RT thread. This
       must be run on behalf of the emerging thread. */
    memset(&tcb->fpuenv,0,sizeof(tcb->fpuenv));
    rthal_init_fpu(&tcb->fpuenv);
#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_RTAI_HW_FPU

    if(tcb->fpup)
        {
        rthal_save_fpu(tcb->fpup);

        if(tcb->user_fpu_owner && tcb->user_fpu_owner->thread.regs)
            tcb->user_fpu_owner->thread.regs->msr &= ~MSR_FP;
        }   

#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_RTAI_HW_FPU

    if(tcb->fpup)
        {
        rthal_restore_fpu(tcb->fpup);

        if(tcb->user_fpu_owner && tcb->user_fpu_owner->thread.regs)
            tcb->user_fpu_owner->thread.regs->msr |= MSR_FP;
        }   

    /* FIXME: We restore FPU "as it was" when RTAI preempted Linux, whereas we
       could be much lazier. */
    if(tcb->user_task)
        rthal_disable_fpu();

#endif /* CONFIG_RTAI_HW_FPU */
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

static inline void xnarch_notify_halt(void)

{
    unsigned long flags = adeos_critical_enter(NULL);
    adeos_critical_exit(flags);
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

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->kspp = &tcb->ksp;
#ifdef CONFIG_RTAI_HW_FPU
    tcb->user_fpu_owner = NULL;
    tcb->fpup = &tcb->fpuenv;
#endif /* CONFIG_RTAI_HW_FPU */
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
    tcb->ksp = 0;
    tcb->kspp = &task->thread.ksp;
#ifdef CONFIG_RTAI_HW_FPU
    tcb->user_fpu_owner = task;
    tcb->fpup = (rthal_fpenv_t *)&task->thread.fpr[0];
#endif /* CONFIG_RTAI_HW_FPU */
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

static inline void xnarch_grab_xirqs (void (*handler)(unsigned irq))

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	adeos_virtualize_irq(irq,
			     handler,
			     NULL,
			     IPIPE_DYNAMIC_MASK);

    /* On this arch, the decrementer trap is not an external IRQ but
       it is instead mapped to a virtual IRQ, so we must grab it
       individually. */

    adeos_virtualize_irq(ADEOS_TIMER_VIRQ,
			 handler,
			 NULL,
			 IPIPE_DYNAMIC_MASK);
}

static inline void xnarch_lock_xirqs (adomain_t *adp, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	{
	switch (irq)
	    {
#ifdef CONFIG_SMP
	    case ADEOS_CRITICAL_IPI:

		/* Never lock out this one. */
		continue;
#endif /* CONFIG_SMP */

	    default:

		__adeos_lock_irq(adp,cpuid,irq);
	    }
	}

    __adeos_lock_irq(adp,cpuid,ADEOS_TIMER_VIRQ);
}

static inline void xnarch_unlock_xirqs (adomain_t *adp, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	{
	switch (irq)
	    {
#ifdef CONFIG_SMP
	    case ADEOS_CRITICAL_IPI:

		continue;
#endif /* CONFIG_SMP */

	    default:

		__adeos_unlock_irq(adp,irq);
	    }
	}

    __adeos_unlock_irq(adp,ADEOS_TIMER_VIRQ);
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay) {
    /* Even though some architectures may use a 64 bits delay here, we
       voluntarily limit to 32 bits, 4 billions ticks should be enough
       for now. Would a timer needs more, an extra call to the tick
       handler would simply occur after 4 billions ticks.  Since the
       timebase value is used to express CPU ticks on the PowerPC
       port, there is no need to rescale the delay value. */
    rthal_timer_program_shot(delay);
}

static inline void xnarch_stop_timer (void) {
    rthal_timer_release();
}

static inline int xnarch_send_timer_ipi (xnarch_cpumask_t mask)

{
#ifdef CONFIG_SMP
    return -1;		/* FIXME */
#else /* ! CONFIG_SMP */
    return 0;
#endif /* CONFIG_SMP */
}

static inline void xnarch_read_timings (unsigned long long *shot,
					unsigned long long *delivery,
					unsigned long long defval)
{
#ifdef CONFIG_ADEOS_PROFILING
    int cpuid = adeos_processor_id();
    *shot = __adeos_profile_data[cpuid].irqs[__adeos_tick_irq].t_handled;
    *delivery = __adeos_profile_data[cpuid].irqs[__adeos_tick_irq].t_synced;
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

static int xnarch_trap_fault (adevinfo_t *evinfo)

{
    xnarch_fltinfo_t fltinfo;
    fltinfo.regs = (struct pt_regs *)evinfo->evdata;
    return xnpod_trap_fault(&fltinfo);
}

unsigned long xnarch_calibrate_timer (void)

{
#if CONFIG_RTAI_HW_TIMER_LATENCY != 0
    return xnarch_ns_to_tsc(CONFIG_RTAI_HW_TIMER_LATENCY) ?: 1;
#else /* CONFIG_RTAI_HW_TIMER_LATENCY unspecified. */
    /* Compute the time needed to program the decrementer in aperiodic
       mode. The return value is expressed in timebase ticks. */
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
	{
	rthal_trap_catch(xnarch_old_trap_handler);
        adeos_free_irq(xnarch_escalation_virq);
	}

    return err;
}

static inline void xnarch_exit (void)

{
#ifdef CONFIG_RTAI_OPT_FUSION
    xnshadow_cleanup();
#endif /* CONFIG_RTAI_OPT_FUSION */
    rthal_trap_catch(xnarch_old_trap_handler);
    adeos_free_irq(xnarch_escalation_virq);
}

#endif /* XENO_MAIN_MODULE */

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#include <nucleus/system.h>

#endif /* __KERNEL__ */

#endif /* !_RTAI_ASM_PPC_SYSTEM_H */
