/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The HYADES Project (http://www.hyades-itea.org).
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

#ifndef _RTAI_ASM_I386_SYSTEM_H
#define _RTAI_ASM_I386_SYSTEM_H

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

#define MODULE_PARM_VALUE(parm) (parm)
#ifndef MODULE_LICENSE
#define MODULE_LICENSE(s)
#endif /* MODULE_LICENSE */

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
            clear_bit(cpuid, lock);
            rthal_sync_irqs();
            adeos_load_cpuid(); /* Could have been migrated by interrupts. */
            set_bit(cpuid, lock);
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
            rthal_cpu_relax(cpuid);
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
#define XNARCH_IRQ_MAX               NR_IRQS
#define XNARCH_HOST_TICK             (1000000000UL/HZ)
#define XNARCH_SCHED_LATENCY         CONFIG_RTAI_HW_SCHED_LATENCY
#define XNARCH_HAVE_APERIODIC_TIMER  1
#define XNARCH_CALIBRATION_PERIOD    150000 /* ns */

#define XNARCH_THREAD_STACKSZ 4096
#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define xnarch_printf                printk
#define xnarch_ullmod(ull,uld,rem)   ({ xnarch_ulldiv(ull,uld,rem); (*rem); })
#define xnarch_ulldiv                rthal_ulldiv
#define xnarch_imuldiv               rthal_imuldiv
#define xnarch_llimd                 rthal_llimd
#define xnarch_get_cpu_tsc           rthal_rdtsc
#define xnarch_num_online_cpus()     num_online_cpus()

struct xnthread;
struct task_struct;

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* Kernel mode side */
    union i387_union fpuenv __attribute__ ((aligned (16))); /* FPU backup area */
    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */
    unsigned long esp;		/* Saved ESP for kernel-based threads */
    unsigned long eip;		/* Saved EIP for kernel-based threads */

    /* User mode side */
    struct task_struct *user_task;	/* Shadowed user-space task */
    struct task_struct *active_task;	/* Active user-space task */

    unsigned long *espp;	/* Pointer to ESP backup area (&esp or &user->thread.esp) */
    unsigned long *eipp;	/* Pointer to EIP backup area (&eip or &user->thread.eip) */
    union i387_union *fpup;	/* Pointer to the FPU backup area (&fpuenv or &user->thread.i387.f[x]save */

} xnarchtcb_t;

typedef struct xnarch_fltinfo {

    unsigned vector;
    long errcode;
    struct pt_regs *regs;

} xnarch_fltinfo_t;

#define xnarch_fault_trap(fi)  ((fi)->vector)
#define xnarch_fault_code(fi)  ((fi)->errcode)
#define xnarch_fault_pc(fi)    ((fi)->regs->eip)

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
    xnarch_printf("Xenomai: fatal: %s\n",emsg); \
    show_stack(NULL,NULL);			\
    for (;;) safe_halt();			\
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

    rthal_pend_linux_irq(RTHAL_8254_IRQ);
}

static inline unsigned long xnarch_set_irq_affinity (unsigned irq,
						     unsigned long affinity) {
    return adeos_set_irq_affinity(irq,affinity);
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_POD_MODULE

void xnpod_welcome_thread(struct xnthread *);

void xnpod_delete_thread(struct xnthread *);

unsigned long xnarch_calibrate_timer (void) {

#if  CONFIG_RTAI_HW_8254_LATENCY != 0
    return xnarch_ns_to_tsc(CONFIG_RTAI_HW_8254_LATENCY);
#else /* CONFIG_RTAI_HW_8254_LATENCY unspecified. */
    /* Compute the time needed to program the 8254 PIT in aperiodic
       mode. The return value is expressed in CPU ticks. */
    return xnarch_ns_to_tsc(rthal_calibrate_8254());
#endif /* CONFIG_RTAI_HW_8254_LATENCY != 0 */
}

static inline void xnarch_start_timer (int ns, void (*tickhandler)(void))

{
    if (ns > 0)	/* Periodic setup. */
	{
	unsigned period = (unsigned)xnarch_llimd(ns,RTHAL_8254_FREQ,1000000000);
	rthal_request_timer(tickhandler,period > LATCH ? LATCH : period,0);
	}
    else  /* Aperiodic setup. */
	rthal_request_timer(tickhandler,0,0);
}

static inline void xnarch_leave_root (xnarchtcb_t *rootcb)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    set_bit(cpuid,&rthal_cpu_realtime);
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = rootcb->active_task = rthal_get_current(cpuid);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
    rootcb->fpup = &rootcb->user_task->thread.i387;
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb) {
    clear_bit(xnarch_current_cpu(),&rthal_cpu_realtime);
}

static inline void __switch_threads(xnarchtcb_t *out_tcb,
				    xnarchtcb_t *in_tcb,
				    struct task_struct *outproc,
				    struct task_struct *inproc
                                    )
{
#if __GNUC__ < 3 || __GNUC__ == 3 && __GNUC_MINOR__ < 2

    __asm__ __volatile__( \
        "pushl %%ecx\n\t" \
        "pushl %%edi\n\t" \
        "pushl %%ebp\n\t" \
        "movl %0,%%ecx\n\t" \
        "movl %%esp,(%%ecx)\n\t" \
        "movl %1,%%ecx\n\t" \
        "movl $1f,(%%ecx)\n\t" \
        "movl %2,%%ecx\n\t" \
        "movl %3,%%edi\n\t" \
        "movl (%%ecx),%%esp\n\t" \
        "pushl (%%edi)\n\t" \
        "testl %%edx,%%edx\n\t" \
        "jne  __switch_to\n\t" \
        "ret\n\t" \
"1:      popl %%ebp\n\t" \
        "popl %%edi\n\t" \
        "popl %%ecx\n\t" \
      : /* no output */ \
      : "m" (out_tcb->espp), \
        "m" (out_tcb->eipp), \
        "m" (in_tcb->espp), \
        "m" (in_tcb->eipp), \
        "b" (out_tcb), \
        "S" (in_tcb), \
        "a" (outproc), \
        "d" (inproc));

#else /* Gcc version >= 3.2 */

    long ebx_out, ecx_out, edi_out, esi_out;
    
    __asm__ __volatile__( \
        "movl %6,%%ecx\n\t" \
	"movl %%esp,(%%ecx)\n\t" \
	"movl %7,%%ecx\n\t" \
	"movl $1f,(%%ecx)\n\t" \
	"movl %8,%%ecx\n\t" \
	"movl %9,%%edi\n\t" \
	"movl (%%ecx),%%esp\n\t" \
	"pushl (%%edi)\n\t" \
	"testl %%edx,%%edx\n\t" \
	"jne  __switch_to\n\t" \
	"ret\n\t" \
"1:      \n\t" \
      : "=b" (ebx_out), \
        "=&c" (ecx_out), \
        "=S" (esi_out), \
        "=D" (edi_out), \
        "+a" (outproc), \
        "+d" (inproc) \
      : "m" (out_tcb->espp), \
        "m" (out_tcb->eipp), \
        "m" (in_tcb->espp), \
        "m" (in_tcb->eipp) \
      : "ebp");

#endif /* Gcc version < 3.2 */
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *outproc = out_tcb->active_task;
    struct task_struct *inproc = in_tcb->user_task;
    static int cr0;

    if (out_tcb->user_task)
	{
	__asm__ __volatile__ ("movl %%cr0,%0": "=r" (cr0));
	clts();
	}

    in_tcb->active_task = inproc ?: outproc;

    if (inproc && inproc != outproc)
	{
	struct mm_struct *oldmm = outproc->active_mm;

	switch_mm(oldmm,inproc->active_mm,inproc);

	if (!inproc->mm)
	    enter_lazy_tlb(oldmm,inproc);
	}

    __switch_threads(out_tcb,in_tcb,outproc,inproc);

    /* If TS was set for the restored user-space thread, set it
       back. */

    if (out_tcb->user_task && (cr0 & 0x8) != 0)
	stts();
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

    if (!tcb->user_task) /* __switch_to() will take care otherwise. */
	{
	if (cpu_has_fxsr)
	    __asm__ __volatile__ ("fxsave %0; fnclex" : "=m" (*tcb->fpup));
	else
	    __asm__ __volatile__ ("fnsave %0; fwait" : "=m" (*tcb->fpup));
	}

#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_RTAI_HW_FPU
    struct task_struct *task = tcb->user_task;

    if (task)
	{
	if (!task->used_math)
	    return;	/* Uninit fpu area -- do not restore. */

	/* Tell Linux that this task has altered the state of the FPU
	   hardware. */
	task->thread_info->status |= TS_USEDFPU;
	}

    /* Restore the FPU hardware with valid fp registers from a
       user-space or kernel thread. */

    clts();

    if (cpu_has_fxsr)
	__asm__ __volatile__ ("fxrstor %0": /* no output */ : "m" (*tcb->fpup));
    else
	__asm__ __volatile__ ("frstor %0": /* no output */ : "m" (*tcb->fpup));

#endif /* CONFIG_RTAI_HW_FPU */
}

static inline void xnarch_init_root_tcb (xnarchtcb_t *tcb,
					 struct xnthread *thread,
					 const char *name)
{
    tcb->user_task = current;
    tcb->active_task = NULL;
    tcb->esp = 0;
    tcb->espp = &tcb->esp;
    tcb->eipp = &tcb->eip;
    tcb->fpup = NULL;
}

static inline void xnarch_init_tcb (xnarchtcb_t *tcb) {

    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->espp = &tcb->esp;
    tcb->eipp = &tcb->eip;
    tcb->fpup = &tcb->fpuenv;
    /* Must be followed by xnarch_init_thread(). */
}

asmlinkage static void xnarch_thread_redirect (struct xnthread *self,
					       int imask,
					       void(*entry)(void *),
					       void *cookie)
{
    rthal_local_irq_restore(!!imask);
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
    unsigned long **psp = (unsigned long **)&tcb->esp;

    tcb->eip = (unsigned long)&xnarch_thread_redirect;
    tcb->esp = (unsigned long)tcb->stackbase;
    **psp = 0;	/* Commit bottom stack memory */
    *psp = (unsigned long *)(((unsigned long)*psp + tcb->stacksize - 0x10) & ~0xf);
    *--(*psp) = (unsigned long)cookie;
    *--(*psp) = (unsigned long)entry;
    *--(*psp) = (unsigned long)imask;
    *--(*psp) = (unsigned long)thread;
    *--(*psp) = 0;
}

#ifdef CONFIG_RTAI_HW_FPU

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{
    /* Initialize the FPU for an emerging kernel-based RT thread. This
       must be run on behalf of the emerging thread. */

    __asm__ __volatile__ ("clts; fninit");

    if (cpu_has_xmm)
	rthal_load_mxcsr(0x1f80);
}

#else /* !CONFIG_RTAI_HW_FPU */

static inline void xnarch_init_fpu (xnarchtcb_t *tcb) {
}

#endif /* CONFIG_RTAI_HW_FPU */

void xnarch_sleep_on (int *flagp) {

    while (!*flagp)
	schedule_timeout(1);
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

static inline void xnarch_notify_shutdown(void)

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

#define xnarch_notify_shutdown() /* Nullified */

#endif /* CONFIG_SMP */

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

#define xnarch_notify_ready() /* Nullified */

#endif /* XENO_POD_MODULE */

#ifdef XENO_SHADOW_MODULE

static inline void xnarch_init_shadow_tcb (xnarchtcb_t *tcb,
					   struct xnthread *thread,
					   const char *name)
{
    struct task_struct *task = current;

    tcb->user_task = task;
    tcb->active_task = NULL;
    tcb->esp = 0;
    tcb->espp = &task->thread.esp;
    tcb->eipp = &task->thread.eip;
    tcb->fpup = &task->thread.i387;
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static void xnarch_program_timer_shot (unsigned long long delay) /* <= in CPU ticks */ {

    rthal_set_timer_shot(rthal_imuldiv(delay,RTHAL_8254_FREQ,RTHAL_CPU_FREQ));
}

static inline void xnarch_stop_timer (void) {
    rthal_free_timer();
}

#endif /* XENO_TIMER_MODULE */

#ifdef XENO_MAIN_MODULE

int xnarch_escalation_virq;

int xnpod_trap_fault(xnarch_fltinfo_t *fltinfo);

void xnpod_schedule_handler(void);

static rthal_trap_handler_t xnarch_old_trap_handler;

static int xnarch_trap_fault (int vector,
			      int signo,
			      struct pt_regs *regs,
			      void *dummy)
{
    xnarch_fltinfo_t fltinfo;

    fltinfo.vector = vector;
    fltinfo.errcode = regs->orig_eax;
    fltinfo.regs = regs;

    return xnpod_trap_fault(&fltinfo);
}

static inline int xnarch_init (void)

{
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

#endif /* !_RTAI_ASM_I386_SYSTEM_H */
