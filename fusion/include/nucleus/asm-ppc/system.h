/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
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
 * As a special exception, the RTAI project gives permission for
 * additional uses of the text contained in its release of Xenomai.
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

#ifndef _RTAI_ASM_PPC_SYSTEM_H
#define _RTAI_ASM_PPC_SYSTEM_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/adeos.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/mmu_context.h>
#include <rtai_config.h>
#include <nucleus/asm/hal.h>
#include <nucleus/asm/atomic.h>
#include <nucleus/shadow.h>

#if ADEOS_RELEASE_NUMBER < 0x02060607
#error "Adeos 2.6r6c7/ppc or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/ppc/"
#endif /* ADEOS_RELEASE_NUMBER < 0x02060801 */

#define MODULE_PARM_VALUE(parm) (parm)

typedef unsigned long spl_t;

#define splhigh(x)  rthal_local_irq_save(x)
#ifdef CONFIG_SMP
#define splexit(x)  rthal_local_irq_restore((x) & 1)
#else /* !CONFIG_SMP */
#define splexit(x)  rthal_local_irq_restore(x)
#endif /* CONFIG_SMP */
#define splnone()   rthal_sti()
#define spltest()   rthal_local_irq_test()
#define splget(x)   rthal_local_irq_flags(x)

typedef unsigned long xnlock_t;

#define XNARCH_LOCK_UNLOCKED 0
#define XNARCH_PASSTHROUGH_IRQS /*empty*/

#ifdef CONFIG_SMP

#define xnlock_get_irqsave(lock,x)  ((x) = __xnlock_get_irqsave(lock))
#define xnlock_clear_irqoff(lock)   xnlock_put_irqrestore(lock,1)
#define xnlock_clear_irqon(lock)    xnlock_put_irqrestore(lock,0)

static inline void xnlock_init (xnlock_t *lock) {

    *lock = XNARCH_LOCK_UNLOCKED;
}

static inline spl_t __xnlock_get_irqsave (xnlock_t *lock)

{
    adeos_declare_cpuid;
    spl_t flags;

    rthal_local_irq_save(flags);

    adeos_load_cpuid();

    if (!test_and_set_bit(cpuid,lock))
	while (test_and_set_bit(BITS_PER_LONG - 1,lock))
	    {
            if (!flags)
                {
                clear_bit(cpuid, lock);
                rthal_sync_irqs();
                adeos_load_cpuid(); /* Could have been migrated by interrupts. */
                set_bit(cpuid, lock);
                }
            else
                rthal_cpu_relax(cpuid); /* FIXME: is this really needed ? */
            }
    else
        flags |= 2;

    return flags;
}

static inline void xnlock_put_irqrestore (xnlock_t *lock, spl_t flags)

{
    if (!(flags & 2))
        {
        adeos_declare_cpuid;

        rthal_cli();

        adeos_load_cpuid();

        if (test_bit(cpuid,lock))
            {
            clear_bit(cpuid,lock);
            clear_bit(BITS_PER_LONG - 1,lock);
            rthal_cpu_relax(cpuid); /* FIXME: is this really needed ? */
            }
        }

    rthal_local_irq_restore(flags & 1);
}

#else /* !CONFIG_SMP */

#define xnlock_init(lock)              do { } while(0)
#define xnlock_get_irqsave(lock,x)     rthal_local_irq_save(x)
#define xnlock_put_irqrestore(lock,x)  rthal_local_irq_restore(x)
#define xnlock_clear_irqoff(lock)      rthal_cli()
#define xnlock_clear_irqon(lock)       rthal_sti()

#endif /* CONFIG_SMP */

#define XNARCH_NR_CPUS               RTHAL_NR_CPUS

#define XNARCH_DEFAULT_TICK          1000000 /* ns, i.e. 1ms */
#define XNARCH_IRQ_MAX               IPIPE_NR_XIRQS /* Do _not_ use NR_IRQS here. */
#define XNARCH_HOST_TICK             (1000000000UL/HZ)
/* Using 2/3 of the average jitter is some constant obtained from
   experimentation that proved to fit on tested PPC platforms with
   respect to auto-calibration. In any case, a more accurate
   scheduling latency can still be fixed by setting
   CONFIG_RTAI_HW_SCHED_LATENCY properly. */
#define xnarch_adjust_calibration(x) ((x) * 2 / 3)

#define XNARCH_THREAD_STACKSZ 4096
#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define XNARCH_PROMPT "RTAI[nucleus]: "
#define xnarch_loginfo(fmt,args...)  printk(KERN_INFO XNARCH_PROMPT fmt, ##args)
#define xnarch_logwarn(fmt,args...)  printk(KERN_WARNING XNARCH_PROMPT fmt, ##args)
#define xnarch_logerr(fmt,args...)   printk(KERN_ERR XNARCH_PROMPT fmt, ##args)
#define xnarch_printf(fmt,args...)   printk(KERN_INFO XNARCH_PROMPT fmt, ##args)

#define xnarch_ullmod(ull,uld,rem)   ({ xnarch_ulldiv(ull,uld,rem); (*rem); })
#define xnarch_ulldiv                rthal_ulldiv
#define xnarch_imuldiv               rthal_imuldiv
#define xnarch_llimd                 rthal_llimd
#define xnarch_get_cpu_tsc           rthal_rdtsc

typedef cpumask_t xnarch_cpumask_t;
#ifdef CONFIG_SMP
#define xnarch_cpu_online_map            cpu_online_map
#else
#define xnarch_cpu_online_map            cpumask_of_cpu(0)
#endif
#define xnarch_num_online_cpus()         num_online_cpus()
#define xnarch_cpu_set(cpu, mask)        cpu_set(cpu, mask)
#define xnarch_cpu_clear(cpu, mask)      cpu_clear(cpu, mask)
#define xnarch_cpus_clear(mask)          cpus_clear(mask)
#define xnarch_cpu_isset(cpu, mask)      cpu_isset(cpu, mask)
#define xnarch_cpus_and(dst, src1, src2) cpus_and(dst, src1, src2)
#define xnarch_cpus_equal(mask1, mask2)  cpus_equal(mask1, mask2)
#define xnarch_cpus_empty(mask)          cpus_empty(mask)
#define xnarch_cpumask_of_cpu(cpu)       cpumask_of_cpu(cpu)
#define xnarch_first_cpu(mask)           first_cpu(mask)
#define XNARCH_CPU_MASK_ALL              CPU_MASK_ALL

struct xnthread;
struct task_struct;

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* Kernel mode side */

#ifdef CONFIG_RTAI_HW_FPU
    /* We only care for basic FPU handling in kernel-space; Altivec
       and SPE are not available to kernel-based Xenomai threads. */
    rthal_fpenv_t fpuenv  __attribute__ ((aligned (16)));
    rthal_fpenv_t *fpup;	/* Pointer to the FPU backup area */
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

static inline unsigned long long xnarch_tsc_to_ns (unsigned long long ts) {
    return xnarch_llimd(ts,1000000000,RTHAL_CPU_FREQ);
}

static inline unsigned long long xnarch_ns_to_tsc (unsigned long long ns) {
    return xnarch_llimd(ns,RTHAL_CPU_FREQ,1000000000);
}

static inline unsigned long long xnarch_get_cpu_time (void) {
    return xnarch_tsc_to_ns(xnarch_get_cpu_tsc());
}

static inline unsigned long long xnarch_get_cpu_freq (void) {
    return RTHAL_CPU_FREQ;
}

static inline unsigned xnarch_current_cpu (void) {
    return adeos_processor_id();
}

#define xnarch_declare_cpuid  adeos_declare_cpuid
#define xnarch_get_cpu(flags) adeos_get_cpu(flags)
#define xnarch_put_cpu(flags) adeos_put_cpu(flags)

#define xnarch_halt(emsg) \
do { \
    adeos_set_printk_sync(adp_current); \
    xnarch_logerr("fatal: %s\n",emsg); \
    show_stack(NULL,NULL);		\
    for (;;) ;				\
} while(0)

#define xnarch_alloc_stack xnmalloc
#define xnarch_free_stack  xnfree

static inline int xnarch_setimask (int imask)

{
    spl_t s;
    splhigh(s);
    splexit(!!imask);
    return !!s;
}

#ifdef XENO_INTR_MODULE

static inline int xnarch_hook_irq (unsigned irq,
				   void (*handler)(unsigned irq,
						   void *cookie),
				   void *cookie)
{
    int err = rthal_request_irq(irq,handler,cookie);

    if (!err)
	rthal_enable_irq(irq);

    return err;
}

static inline int xnarch_release_irq (unsigned irq) {

    return rthal_release_irq(irq);
}

static inline int xnarch_enable_irq (unsigned irq)

{
    if (irq >= XNARCH_IRQ_MAX)
	return -EINVAL;

    rthal_enable_irq(irq);

    return 0;
}

static inline int xnarch_disable_irq (unsigned irq)

{
    if (irq >= XNARCH_IRQ_MAX)
	return -EINVAL;

    rthal_disable_irq(irq);

    return 0;
}

static inline void xnarch_isr_chain_irq (unsigned irq) {
    rthal_pend_linux_irq(irq);
}

static inline void xnarch_isr_enable_irq (unsigned irq) {
    rthal_enable_irq(irq);
}

static inline void xnarch_relay_tick (void) {

    rthal_pend_linux_irq(__adeos_timer_virq);
}

static inline cpumask_t xnarch_set_irq_affinity (unsigned irq,
						 cpumask_t affinity) {
    return adeos_set_irq_affinity(irq,affinity);
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_POD_MODULE

void xnpod_welcome_thread(struct xnthread *);

void xnpod_delete_thread(struct xnthread *);

unsigned long xnarch_calibrate_timer (void)

{
#if  CONFIG_RTAI_HW_TIMER_LATENCY != 0
    return xnarch_ns_to_tsc(CONFIG_RTAI_HW_TIMER_LATENCY);
#else /* CONFIG_RTAI_HW_TIMER_LATENCY unspecified. */
    /* Compute the time needed to program the decrementer in aperiodic
       mode. The return value is expressed in timebase ticks. */
    return xnarch_ns_to_tsc(rthal_calibrate_timer());
#endif /* CONFIG_RTAI_HW_TIMER_LATENCY != 0 */
}

static inline int xnarch_start_timer (unsigned long ns,
				      void (*tickhandler)(void)) {
    return rthal_request_timer(tickhandler,ns);
}

static inline void xnarch_leave_root (xnarchtcb_t *rootcb)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    /* rthal_cpu_realtime is only tested for the current processor,
       and always inside a critical section. */
    __set_bit(cpuid,&rthal_cpu_realtime);
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = rootcb->active_task = rthal_get_current(cpuid);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
#ifdef CONFIG_RTAI_HW_FPU
    rootcb->fpup = (rthal_fpenv_t *)&rootcb->user_task->thread.fpr[0];
#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb) {
    __clear_bit(xnarch_current_cpu(),&rthal_cpu_realtime);
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *outproc = out_tcb->active_task;
    struct task_struct *inproc = in_tcb->user_task;
    unsigned long flags;

    rthal_hw_lock(flags);

    in_tcb->active_task = inproc ?: outproc;

    if (inproc && inproc != outproc) /* User-space thread switch? */
	{
	struct mm_struct *prev = outproc->active_mm;
	struct mm_struct *next = inproc->active_mm;

	/* Switch the mm context.*/

	inproc->thread.pgdir = next->pgd;

	if (prev != next)
	    {
	    get_mmu_context(next);
	    set_context(next->context, next->pgd);
	    }

	if (!inproc->mm)
	    enter_lazy_tlb(prev,inproc);

	/* The following has been lifted from
	   arch/ppc/kernel/process.c. */

#ifdef CONFIG_SMP
	if (outproc->thread.regs && (outproc->thread.regs->msr & MSR_FP))
	    giveup_fpu(outproc);
#ifdef CONFIG_ALTIVEC
	if ((outproc->thread.regs && (outproc->thread.regs->msr & MSR_VEC)))
	    giveup_altivec(outproc);
#endif /* CONFIG_ALTIVEC */
#ifdef CONFIG_SPE
	if ((outproc->thread.regs && (outproc->thread.regs->msr & MSR_SPE)))
	    giveup_spe(outproc);
#endif /* CONFIG_SPE */
#endif /* CONFIG_SMP */

#ifdef CONFIG_ALTIVEC
	if (inproc->thread.regs && last_task_used_altivec == inproc)
	    inproc->thread.regs->msr |= MSR_VEC;
#endif /* CONFIG_ALTIVEC */

#ifdef CONFIG_SPE
	if (inproc->thread.regs && last_task_used_spe == inproc)
	    inproc->thread.regs->msr |= MSR_SPE;
#endif /* CONFIG_SPE */

	_switch(&outproc->thread,&inproc->thread);
	}
    else
	/* Kernel-to-kernel context switch. */
        rthal_switch_context(out_tcb->kspp,in_tcb->kspp);

    rthal_hw_unlock(flags);
}

static inline void xnarch_finalize_and_switch (xnarchtcb_t *dead_tcb,
					       xnarchtcb_t *next_tcb) {
    xnarch_switch_to(dead_tcb,next_tcb);
}

static inline void xnarch_finalize_no_switch (xnarchtcb_t *dead_tcb) {
    /* Empty */
}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_RTAI_HW_FPU

    struct task_struct *task = tcb->user_task;

    if (task)
	{
	if (task->thread.regs && (task->thread.regs->msr & MSR_FP))
	    giveup_fpu(task);

	return;
	}

    /* Save the fp regs of the kernel thread owning the FPU. */

    rthal_save_fpu(tcb->fpup);

#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_RTAI_HW_FPU

    if (tcb->user_task)
	/* On PowerpPC, we let the unavailability exception happen for
	   the incoming user-space task if it happens to use the FPU,
	   instead of eagerly reloading the fp regs upon switch. */
	return;

    /* Restore the fp regs of the incoming kernel thread. */

    rthal_restore_fpu(tcb->fpup);

#endif /* CONFIG_RTAI_HW_FPU */
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
    tcb->fpup = NULL;
#endif /* CONFIG_RTAI_HW_FPU */
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->kspp = &tcb->ksp;
#ifdef CONFIG_RTAI_HW_FPU
    tcb->fpup = &tcb->fpuenv;
#endif /* CONFIG_RTAI_HW_FPU */
    /* Must be followed by xnarch_init_thread(). */
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
    ksp[26] = flags & ~MSR_EE; /* msr */

    tcb->entry = entry;
    tcb->cookie = cookie;
    tcb->self = thread;
    tcb->imask = imask;
    tcb->name = name;
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

void xnarch_sleep_on (int *flagp) {

    while (!*flagp)
	{
	set_current_state(TASK_UNINTERRUPTIBLE);
	schedule_timeout(1);
	}
}

#ifdef CONFIG_SMP

static inline int xnarch_send_ipi (cpumask_t cpumask) {

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

static inline int xnarch_send_ipi (cpumask_t cpumask) {

    return 0;
}

static inline int xnarch_hook_ipi (void (*handler)(void)) {

    return 0;
}

static inline int xnarch_release_ipi (void) {

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
    xnshadow_release_events();
    /* Wait for the currently processed events to drain. */
    set_current_state(TASK_UNINTERRUPTIBLE);
    schedule_timeout(50);
    xnarch_release_ipi();
}

static inline void xnarch_escalate (void) {

    extern int xnarch_escalation_virq;
    adeos_trigger_irq(xnarch_escalation_virq);
}

static inline void *xnarch_sysalloc (u_long bytes) {

    return kmalloc(bytes,GFP_ATOMIC);
}

static inline void xnarch_sysfree (void *chunk, u_long bytes) {

    kfree(chunk);
}

static void xnarch_notify_ready (void) {
    
    xnshadow_grab_events();
}

#endif /* XENO_POD_MODULE */

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
    tcb->fpup = (rthal_fpenv_t *)&task->thread.fpr[0];
#endif /* CONFIG_RTAI_HW_FPU */
    tcb->entry = NULL;
    tcb->cookie = NULL;
    tcb->self = thread;
    tcb->imask = 0;
    tcb->name = name;
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long long delay) {
    /* Delays are expressed in CPU ticks, so we need to keep a 64bit
       value here, especially for 64bit arch ports using an interval
       timer based on the internal cycle counter of the CPU. Since the
       timebase value is used to express CPU ticks on the PowerPC
       port, there is no need to rescale the delay value. */ 
    rthal_set_timer_shot(delay);
}

static inline void xnarch_stop_timer (void) {
    rthal_release_timer();
}

#endif /* XENO_TIMER_MODULE */

#ifdef XENO_MAIN_MODULE

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

static inline int xnarch_init (void)

{
#ifdef CONFIG_SMP
    /* The HAL layer also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */

    xnarch_escalation_virq = adeos_alloc_irq();

    if (xnarch_escalation_virq == 0)
	return -ENOSYS;

    adeos_virtualize_irq_from(&rthal_domain,
			      xnarch_escalation_virq,
			      (void (*)(unsigned))&xnpod_schedule_handler,
			      NULL,
			      IPIPE_HANDLE_MASK);

    xnarch_old_trap_handler = rthal_set_trap_handler(&xnarch_trap_fault);

    return xnshadow_init();
}

static inline void xnarch_exit (void) {

    xnshadow_cleanup();
    rthal_set_trap_handler(xnarch_old_trap_handler);
    adeos_free_irq(xnarch_escalation_virq);
}

#endif /* XENO_MAIN_MODULE */

#ifdef __cplusplus
}
#endif

/* Dashboard and graph control. */
#define XNARCH_DECL_DISPLAY_CONTEXT();
#define xnarch_init_display_context(obj)
#define xnarch_create_display(obj,name,tag)
#define xnarch_delete_display(obj)
#define xnarch_post_graph(obj,state)
#define xnarch_post_graph_if(obj,state,cond)

#else /* !__KERNEL__ */

#include <nucleus/system.h>

#endif /* __KERNEL__ */

#define XNARCH_CALIBRATION_PERIOD    1000000 /* ns */

#endif /* !_RTAI_ASM_PPC_SYSTEM_H */
