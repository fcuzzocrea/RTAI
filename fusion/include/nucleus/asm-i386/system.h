/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The HYADES Project (http://www.hyades-itea.org).
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

#ifndef _RTAI_ASM_I386_SYSTEM_H
#define _RTAI_ASM_I386_SYSTEM_H

#ifdef __KERNEL__

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/adeos.h>
#include <linux/vmalloc.h>
#include <asm/uaccess.h>
#include <asm/param.h>
#include <asm/mmu_context.h>
#include <rtai_config.h>
#include <nucleus/asm/hal.h>
#include <nucleus/asm/atomic.h>
#include <nucleus/shadow.h>

#if ADEOS_RELEASE_NUMBER < 0x02060901
#error "Adeos 2.6r9c1/x86 or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/i386/"
#endif

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
#define splsync(x)  rthal_local_irq_sync(x)

typedef struct {

    volatile unsigned long lock;
#if CONFIG_RTAI_OPT_DEBUG
    const char *file;
    const char *function;
    unsigned line;
    int cpu;
#endif /* CONFIG_RTAI_OPT_DEBUG */
} xnlock_t;

#ifndef CONFIG_RTAI_OPT_DEBUG
#define XNARCH_LOCK_UNLOCKED (xnlock_t) { 0 }
#else
#define XNARCH_LOCK_UNLOCKED (xnlock_t) {       \
        0,                                      \
        NULL,                                   \
        0,                                      \
        -1                                      \
        }
#endif /* !CONFIG_RTAI_OPT_DEBUG */

#ifdef CONFIG_SMP

#ifndef CONFIG_RTAI_OPT_DEBUG
#define xnlock_get_irqsave(lock,x)  ((x) = __xnlock_get_irqsave(lock))
#else /* !CONFIG_RTAI_OPT_DEBUG */
#define xnlock_get_irqsave(lock,x) \
    ((x) = __xnlock_get_irqsave(lock, __FILE__, __LINE__,__FUNCTION__))
#endif /* CONFIG_RTAI_OPT_DEBUG */
#define xnlock_clear_irqoff(lock)   xnlock_put_irqrestore(lock,1)
#define xnlock_clear_irqon(lock)    xnlock_put_irqrestore(lock,0)

static inline void xnlock_init (xnlock_t *lock) {

    *lock = XNARCH_LOCK_UNLOCKED;
}

#if CONFIG_RTAI_OPT_DEBUG
#define XNARCH_DEBUG_SPIN_LIMIT 3000000

static inline spl_t
__xnlock_get_irqsave (xnlock_t *lock, const char *file, unsigned line, const char *function)
{
    unsigned spin_count = 0;
#else /* !CONFIG_RTAI_OPT_DEBUG */
static inline spl_t __xnlock_get_irqsave (xnlock_t *lock)
{
#endif /* CONFIG_RTAI_OPT_DEBUG */
    adeos_declare_cpuid;
    unsigned long flags;

    rthal_local_irq_save(flags);

    adeos_load_cpuid();

    if (!test_and_set_bit(cpuid,&lock->lock))
	{
        while (test_and_set_bit(BITS_PER_LONG - 1,&lock->lock))
            /* Use an non-locking test in the inner loop, as Linux'es
               bit_spin_lock. */
            while (test_bit(BITS_PER_LONG - 1,&lock->lock))
                {
                cpu_relax();

#if CONFIG_RTAI_OPT_DEBUG
                if (++spin_count == XNARCH_DEBUG_SPIN_LIMIT)
                    {
                    adeos_set_printk_sync(adp_current);
                    printk(KERN_ERR
                           "RTAI: stuck on nucleus lock %p\n"
                           "      waiter = %s:%u (%s(), CPU #%d)\n"
                           "      owner  = %s:%u (%s(), CPU #%d)\n",
                           lock,file,line,function,cpuid,
                           lock->file,lock->line,lock->function,lock->cpu);
                    show_stack(NULL,NULL);
                    for (;;)
                        safe_halt();
                    }
#endif /* CONFIG_RTAI_OPT_DEBUG */
                }

#if CONFIG_RTAI_OPT_DEBUG
	lock->file = file;
	lock->function = function;
	lock->line = line;
	lock->cpu = cpuid;
#endif /* CONFIG_RTAI_OPT_DEBUG */
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

        if (test_and_clear_bit(cpuid,&lock->lock))
            clear_bit(BITS_PER_LONG - 1,&lock->lock);
        }

    rthal_local_irq_restore(flags & 1);
}

#define XNARCH_PASSTHROUGH_IRQS \
case INVALIDATE_TLB_VECTOR - FIRST_EXTERNAL_VECTOR: \
case CALL_FUNCTION_VECTOR - FIRST_EXTERNAL_VECTOR: \
case RESCHEDULE_VECTOR - FIRST_EXTERNAL_VECTOR:

#else /* !CONFIG_SMP */

#define xnlock_init(lock)              do { } while(0)
#define xnlock_get_irqsave(lock,x)     rthal_local_irq_save(x)
#define xnlock_put_irqrestore(lock,x)  rthal_local_irq_restore(x)
#define xnlock_clear_irqoff(lock)      rthal_cli()
#define xnlock_clear_irqon(lock)       rthal_sti()

#endif /* CONFIG_SMP */

#define XNARCH_NR_CPUS               RTHAL_NR_CPUS

#define XNARCH_DEFAULT_TICK          1000000 /* ns, i.e. 1ms */
#ifdef CONFIG_X86_LOCAL_APIC
/* When the local APIC is enabled, we do not need to relay the host
   tick since 8254 interrupts are already flowing normally to Linux
   (i.e. the nucleus does not intercept it, but rather an APIC-based
   timer interrupt instead. */
#define XNARCH_HOST_TICK             0
#else /* CONFIG_X86_LOCAL_APIC */
#define XNARCH_HOST_TICK             (1000000000UL/HZ)
#endif /* CONFIG_X86_LOCAL_APIC */

#define XNARCH_THREAD_STACKSZ 4096
#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define XNARCH_PROMPT "RTAI[nucleus]: "
#define xnarch_loginfo(fmt,args...)  printk(KERN_INFO XNARCH_PROMPT fmt , ##args)
#define xnarch_logwarn(fmt,args...)  printk(KERN_WARNING XNARCH_PROMPT fmt , ##args)
#define xnarch_logerr(fmt,args...)   printk(KERN_ERR XNARCH_PROMPT fmt , ##args)
#define xnarch_printf(fmt,args...)   printk(KERN_INFO XNARCH_PROMPT fmt , ##args)

#define xnarch_ullmod(ull,uld,rem)   ({ xnarch_ulldiv(ull,uld,rem); (*rem); })
#define xnarch_uldiv(ull, d)         rthal_uldivrem(ull, d, NULL)
#define xnarch_ulmod(ull, d)         ({ u_long _rem;                    \
                                        rthal_uldivrem(ull,d,&_rem); _rem; })

#define xnarch_ullmul                rthal_ullmul
#define xnarch_uldivrem              rthal_uldivrem
#define xnarch_ulldiv                rthal_ulldiv
#define xnarch_imuldiv               rthal_imuldiv
#define xnarch_llimd                 rthal_llimd
#define xnarch_get_cpu_tsc           rthal_rdtsc

typedef cpumask_t xnarch_cpumask_t;
#ifdef CONFIG_SMP
#define xnarch_cpu_online_map            cpu_online_map
#else
#define xnarch_cpu_online_map		 cpumask_of_cpu(0)
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
struct xnheap;
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

typedef struct xnarch_heapcb {

    atomic_t numaps;	/* # of active user-space mappings. */

    int kmflags;	/* Kernel memory flags (0 if vmalloc()). */

    void *heapbase;	/* Shared heap memory base (possibly unaligned). */

    void *shmbase;	/* Shared memory base (page-aligned). */

} xnarch_heapcb_t;

#ifdef __cplusplus
extern "C" {
#endif

static inline long long xnarch_tsc_to_ns (long long ts) {
    return xnarch_llimd(ts,1000000000,RTHAL_CPU_FREQ);
}

static inline long long xnarch_ns_to_tsc (long long ns) {
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

#define xnarch_declare_cpuid  adeos_declare_cpuid
#define xnarch_get_cpu(flags) adeos_get_cpu(flags)
#define xnarch_put_cpu(flags) adeos_put_cpu(flags)

#define xnarch_halt(emsg) \
do { \
    adeos_set_printk_sync(adp_current); \
    xnarch_logerr("fatal: %s\n",emsg); \
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
    return rthal_request_irq(irq,handler,cookie);
}

static inline int xnarch_release_irq (unsigned irq)

{
    return rthal_release_irq(irq);
}

static inline int xnarch_enable_irq (unsigned irq)

{
    return rthal_enable_irq(irq);
}

static inline int xnarch_disable_irq (unsigned irq)

{
    return rthal_disable_irq(irq);
}

static inline void xnarch_chain_irq (unsigned irq)

{
    rthal_pend_linux_irq(irq);
}

static inline void xnarch_relay_tick (void)

{
    rthal_pend_linux_irq(RTHAL_8254_IRQ);
}

static inline cpumask_t xnarch_set_irq_affinity (unsigned irq,
                                                 xnarch_cpumask_t affinity)
{
    return adeos_set_irq_affinity(irq,affinity);
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_POD_MODULE

void xnpod_welcome_thread(struct xnthread *);

void xnpod_delete_thread(struct xnthread *);

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
    rootcb->fpup = &rootcb->user_task->thread.i387;
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb) {
    __clear_bit(xnarch_current_cpu(),&rthal_cpu_realtime);
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

#else /* GCC version >= 3.2 */

    long ebx_out, ecx_out, edi_out, esi_out;
    
    __asm__ __volatile__( \
        "pushl %%ebp\n\t" \
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
"1:      popl %%ebp\n\t" \
      : "=b" (ebx_out), \
        "=&c" (ecx_out), \
        "=S" (esi_out), \
        "=D" (edi_out), \
        "+a" (outproc), \
        "+d" (inproc) \
      : "m" (out_tcb->espp), \
        "m" (out_tcb->eipp), \
        "m" (in_tcb->espp), \
        "m" (in_tcb->eipp));

#endif /* GCC version < 3.2 */
}

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *outproc = out_tcb->active_task;
    struct task_struct *inproc = in_tcb->user_task;

    if (inproc && outproc->thread_info->status & TS_USEDFPU)        
        /* __switch_to will try and use __unlazy_fpu, so that the ts
           bit need to be cleared. */
        clts();
    
    in_tcb->active_task = inproc ?: outproc;

    if (inproc && inproc != outproc)
	{
	struct mm_struct *oldmm = outproc->active_mm;

	switch_mm(oldmm,inproc->active_mm,inproc);

	if (!inproc->mm)
	    enter_lazy_tlb(oldmm,inproc);
	}

    __switch_threads(out_tcb,in_tcb,outproc,inproc);

    stts();
}

static inline void xnarch_finalize_and_switch (xnarchtcb_t *dead_tcb,
					       xnarchtcb_t *next_tcb) {
    xnarch_switch_to(dead_tcb,next_tcb);
}

static inline void xnarch_finalize_no_switch (xnarchtcb_t *dead_tcb) {
    /* Empty */
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
    /* xnpod_welcome_thread() will do clts() if needed. */
    stts();
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
	{
	unsigned long __mxcsr = 0x1f80UL & 0xffbfUL;
	__asm__ __volatile__ ("ldmxcsr %0": : "m" (__mxcsr));
	}
}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{
    if (!tcb->user_task) /* __switch_to() will take care otherwise. */
	{
        clts();

	if (cpu_has_fxsr)
	    __asm__ __volatile__ ("fxsave %0; fnclex" : "=m" (*tcb->fpup));
	else
	    __asm__ __volatile__ ("fnsave %0; fwait" : "=m" (*tcb->fpup));
	}
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
    struct task_struct *task = tcb->user_task;

    if (task)
	{
	if (!task->used_math)
            {
            stts();
	    return;	/* Uninit fpu area -- do not restore. */
            }

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
}

#else /* !CONFIG_RTAI_HW_FPU */

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{}

#endif /* CONFIG_RTAI_HW_FPU */

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
    xnarch_cpumask_t other_cpus = cpu_online_map;
    unsigned cpu, nr_cpus = num_online_cpus();
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

static inline int xnarch_send_ipi (xnarch_cpumask_t cpumask) {

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
	switch (irq)
	    {
#ifdef CONFIG_SMP
	    case ADEOS_CRITICAL_IPI:
	    case INVALIDATE_TLB_VECTOR - FIRST_EXTERNAL_VECTOR:
	    case CALL_FUNCTION_VECTOR - FIRST_EXTERNAL_VECTOR:
	    case RESCHEDULE_VECTOR - FIRST_EXTERNAL_VECTOR:

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
	switch (irq)
	    {
#ifdef CONFIG_SMP
	    case ADEOS_CRITICAL_IPI:
	    case INVALIDATE_TLB_VECTOR - FIRST_EXTERNAL_VECTOR:
	    case CALL_FUNCTION_VECTOR - FIRST_EXTERNAL_VECTOR:
	    case RESCHEDULE_VECTOR - FIRST_EXTERNAL_VECTOR:

		continue;
#endif /* CONFIG_SMP */

	    default:

		__adeos_unlock_irq(adp,irq);
	    }
	}
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay) {
    /* Even though some architectures may use a 64 bits delay here, we
       voluntarily limit to 32 bits, 4 billions ticks should be enough for
       now. If a timer need more, a spurious but harmless call to the tick
       handler will occur after 4 billions ticks. */
    rthal_set_timer_shot(rthal_imuldiv(delay,RTHAL_TIMER_FREQ,RTHAL_CPU_FREQ));
}

static inline void xnarch_stop_timer (void) {
    rthal_release_timer();
}

static inline int xnarch_send_timer_ipi (xnarch_cpumask_t mask)

{
#if CONFIG_SMP
    return adeos_send_ipi(RTHAL_APIC_TIMER_IPI, mask);
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

#ifdef XENO_HEAP_MODULE

static inline void xnarch_init_heapcb (xnarch_heapcb_t *hcb)

{
    atomic_set(&hcb->numaps,0);
    hcb->kmflags = 0;
    hcb->heapbase = NULL;
    hcb->shmbase = NULL;
}

static inline int xnarch_remap_page_range(struct vm_area_struct *vma,
					  unsigned long uvaddr,
					  unsigned long paddr,
					  unsigned long size,
					  pgprot_t prot)
{
    return remap_page_range(vma,uvaddr,paddr,size,prot);
}

#endif /* XENO_HEAP_MODULE */

#ifdef XENO_MAIN_MODULE

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

    fltinfo.vector = evinfo->event;
    fltinfo.errcode = ((struct pt_regs *)evinfo->evdata)->orig_eax;
    fltinfo.regs = (struct pt_regs *)evinfo->evdata;

    return xnpod_trap_fault(&fltinfo);
}

static inline unsigned long xnarch_calibrate_timer (void)

{
#if CONFIG_RTAI_HW_TIMER_LATENCY != 0
    return xnarch_ns_to_tsc(CONFIG_RTAI_HW_TIMER_LATENCY);
#else /* CONFIG_RTAI_HW_TIMER_LATENCY unspecified. */
    /* Compute the time needed to program the PIT in aperiodic
       mode. The return value is expressed in CPU ticks. Depending on
       whether CONFIG_X86_LOCAL_APIC is enabled or not in the kernel
       configuration RTAI is compiled against,
       CONFIG_RTAI_HW_TIMER_LATENCY will either refer to the local
       APIC or 8254 timer latency value. */
    return xnarch_ns_to_tsc(rthal_calibrate_timer());
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
    int err = 0;

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

#ifdef CONFIG_RTAI_OPT_FUSION
    err = xnshadow_mount();
#endif /* CONFIG_RTAI_OPT_FUSION */

    if (err)
        adeos_free_irq(xnarch_escalation_virq);
    else
	err = xnarch_calibrate_sched();

    return err;
}

static inline void xnarch_exit (void)

{
#ifdef CONFIG_RTAI_OPT_FUSION
    xnshadow_cleanup();
#endif /* CONFIG_RTAI_OPT_FUSION */
    rthal_set_trap_handler(xnarch_old_trap_handler);
    adeos_free_irq(xnarch_escalation_virq);
}

#endif /* XENO_MAIN_MODULE */

#ifdef XENO_TRACES_MODULE

#include <asm/timex.h>          /* For cpu_khz */
#include <linux/kernel_stat.h>  /* For kstat_irqs */

#if CONFIG_X86_LOCAL_APIC

#define RTAI_TRACE_TIMER_IRQ       RTHAL_APIC_TIMER_IPI
#define linux_timer_irq_count(cpu) (irq_stat[(cpu)].apic_timer_irqs)

#else /* !CONFIG_X86_LOCAL_APIC */

#define RTAI_TRACE_TIMER_IRQ       RTHAL_8254_IRQ
#define linux_timer_irq_count(cpu) (kstat_cpu(cpu).irqs[RTHAL_8254_IRQ])

#endif /* CONFIG_X86_LOCAL_APIC */

#define tsc2ms(timestamp)          rthal_ulldiv((timestamp), cpu_khz, NULL)

#endif /* XENO_TRACES_MODULE */

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
