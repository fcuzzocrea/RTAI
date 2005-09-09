/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2004 The HYADES Project (http://www.hyades-itea.org).
 * Copyright (C) 2004,2005 Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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

#include <nucleus/asm-generic/system.h>

#ifdef __KERNEL__

#include <linux/config.h>
#include <linux/ptrace.h>

#ifdef CONFIG_ADEOS_CORE
#if ADEOS_RELEASE_NUMBER < 0x02060b01
#error "Adeos 2.6r11c1/x86 or above is required to run this software; please upgrade."
#error "See http://download.gna.org/adeos/patches/v2.6/i386/"
#endif
#endif /* CONFIG_ADEOS_CORE */

#define XNARCH_DEFAULT_TICK          1000000 /* ns, i.e. 1ms */
#ifdef CONFIG_X86_LOCAL_APIC
/* When the local APIC is enabled, we do not need to relay the host
   tick since 8254 interrupts are already flowing normally to Linux
   (i.e. the nucleus does not intercept them, but uses a dedicated
   APIC-based timer interrupt instead, i.e. RTHAL_APIC_TIMER_IPI). */
#define XNARCH_HOST_TICK             0
#else /* CONFIG_X86_LOCAL_APIC */
#define XNARCH_HOST_TICK             (1000000000UL/HZ)
#endif /* CONFIG_X86_LOCAL_APIC */

#define XNARCH_THREAD_STACKSZ 4096

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)
#define xnarch_fpu_ptr(tcb)     ((tcb)->fpup)
#define xnarch_user_task(tcb)   ((tcb)->user_task)
#define xnarch_user_pid(tcb)    ((tcb)->user_task->pid)

#define xnarch_alloc_stack xnmalloc
#define xnarch_free_stack  xnfree

struct xnthread;
struct task_struct;

typedef struct xnarchtcb {      /* Per-thread arch-dependent block */

    /* Kernel mode side */
    union i387_union fpuenv __attribute__ ((aligned (16))); /* FPU backup area */
    unsigned stacksize;         /* Aligned size of stack (bytes) */
    unsigned long *stackbase;   /* Stack space */
    unsigned long esp;          /* Saved ESP for kernel-based threads */
    unsigned long eip;          /* Saved EIP for kernel-based threads */

    /* User mode side */
    struct task_struct *user_task;      /* Shadowed user-space task */
    struct task_struct *active_task;    /* Active user-space task */

    unsigned long *espp;        /* Pointer to ESP backup area (&esp or &user->thread.esp) */
    unsigned long *eipp;        /* Pointer to EIP backup area (&eip or &user->thread.eip) */
    union i387_union *fpup;     /* Pointer to the FPU backup area (&fpuenv or &user->thread.i387.f[x]save */

} xnarchtcb_t;

typedef struct xnarch_fltinfo {

    unsigned vector;
    long errcode;
    struct pt_regs *regs;

} xnarch_fltinfo_t;

#define xnarch_fault_trap(fi)   ((fi)->vector)
#define xnarch_fault_code(fi)   ((fi)->errcode)
#define xnarch_fault_pc(fi)     ((fi)->regs->eip)
/* fault is caused by use FPU while FPU disabled. */
#define xnarch_fault_fpu_p(fi)  ((fi)->vector == 7)
/* The following predicates are only usable over a regular Linux stack
   context. */
#define xnarch_fault_pf_p(fi)   ((fi)->vector == 14)
#define xnarch_fault_bp_p(fi)   ((current->ptrace & PT_PTRACED) && \
                                 ((fi)->vector == 1 || (fi)->vector == 3))
#define xnarch_fault_notify(fi) (!xnarch_fault_bp_p(fi))

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

static inline int xnarch_shadow_p (xnarchtcb_t *tcb, struct task_struct *task)
{
    return tcb->espp == &task->thread.esp; /* Sign of shadow... */
}

static inline void xnarch_relay_tick (void)

{
    rthal_irq_host_pend(RTHAL_8254_IRQ);
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
    rthal_declare_cpuid;

    rthal_load_cpuid();

    /* rthal_cpu_realtime is only tested for the current processor,
       and always inside a critical section. */
    __set_bit(cpuid,&rthal_cpu_realtime);
    /* Remember the preempted Linux task pointer. */
    rootcb->user_task = rootcb->active_task = rthal_current_host_task(cpuid);
    /* So that xnarch_save_fpu() will operate on the right FPU area. */
    rootcb->fpup = &rootcb->user_task->thread.i387;
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb)
{
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
        "pushfl\n\t" \
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
        "popfl\n\t" \
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
        "pushfl\n\t" \
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
        "popfl\n\t" \
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
    unsigned long fs, gs;

    if (inproc && outproc->thread_info->status & TS_USEDFPU)
        /* __switch_to will try and use __unlazy_fpu, so we need to
           clear the ts bit. */
        clts();
    
    in_tcb->active_task = inproc ?: outproc;

    if (inproc && inproc != outproc)
        {
        struct mm_struct *oldmm = outproc->active_mm;

        switch_mm(oldmm,inproc->active_mm,inproc);

        if (!inproc->mm)
            enter_lazy_tlb(oldmm,inproc);
        }

    if (out_tcb->user_task) {
       /* Make sure that __switch_to() will always reload the correct
	  %fs and %gs registers, even if we happen to migrate the task
	  across domains in the meantime. */
	asm volatile("mov %%fs,%0":"=m" (fs));
	asm volatile("mov %%gs,%0":"=m" (gs));
    }

    __switch_threads(out_tcb,in_tcb,outproc,inproc);

    if (xnarch_shadow_p(out_tcb,outproc)) {

        struct thread_struct *thread = &outproc->thread;

	loadsegment(fs, fs);
	loadsegment(gs, gs);

        barrier();

        /* Eagerly reinstate the I/O bitmap of any incoming shadow
           thread which has previously requested I/O permissions. We
           don't want the unexpected latencies induced by lazy update
           from the GPF handler to bite shadow threads that
           explicitely told the kernel that they would need to perform
           raw I/O ops. */

        if (thread->io_bitmap_ptr) {
            struct tss_struct *tss = &per_cpu(init_tss, rthal_processor_id());

            if (tss->io_bitmap_base == INVALID_IO_BITMAP_OFFSET_LAZY) {
                
                memcpy(tss->io_bitmap, thread->io_bitmap_ptr,thread->io_bitmap_max);

                if (thread->io_bitmap_max < tss->io_bitmap_max)
                    memset((char *) tss->io_bitmap +
                           thread->io_bitmap_max, 0xff,
                           tss->io_bitmap_max - thread->io_bitmap_max);

                tss->io_bitmap_max = thread->io_bitmap_max;
                tss->io_bitmap_base = IO_BITMAP_OFFSET;
            }
        }
    }

    stts();
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
    tcb->esp = 0;
    tcb->espp = &tcb->esp;
    tcb->eipp = &tcb->eip;
    tcb->fpup = NULL;
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
    **psp = 0;  /* Commit bottom stack memory */
    *psp = (unsigned long *)(((unsigned long)*psp + tcb->stacksize - 0x10) & ~0xf);
    *--(*psp) = (unsigned long)cookie;
    *--(*psp) = (unsigned long)entry;
    *--(*psp) = (unsigned long)imask;
    *--(*psp) = (unsigned long)thread;
    *--(*psp) = 0;
}

#ifdef CONFIG_RTAI_HW_FPU

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 11)
#define xnarch_fpu_init_p(task)   ((task)->used_math)
#define xnarch_set_fpu_init(task) ((task)->used_math = 1)
#else
#define xnarch_fpu_init_p(task)   tsk_used_math(task)
#define xnarch_set_fpu_init(task) set_stopped_child_used_math(task)
#endif

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{
    struct task_struct *task = tcb->user_task;
    /* Initialize the FPU for a task. This must be run on behalf of the
       task. */

    __asm__ __volatile__ ("clts; fninit");

    if (cpu_has_xmm)
        {
        unsigned long __mxcsr = 0x1f80UL & 0xffbfUL;
        __asm__ __volatile__ ("ldmxcsr %0": : "m" (__mxcsr));
        }

    if(task)
        {
        /* Real-time shadow FPU initialization: tell Linux that this thread
          initialized its FPU hardware. The TS_USEDFPU bit is necessary for
          xnarch_save_fpu to save the FPU state at next switch. */
        xnarch_set_fpu_init(task);
        task->thread_info->status |= TS_USEDFPU;
        }   
}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{
    struct task_struct *task = tcb->user_task;
    
    if(task)
        {
        if(!(task->thread_info->status & TS_USEDFPU))
            return;

        /* Tell Linux that we already saved the state of the FPU hardware
           of this task. */
        task->thread_info->status &= ~TS_USEDFPU;
        }

    clts();
    
    if (cpu_has_fxsr)
        __asm__ __volatile__ ("fxsave %0; fnclex" : "=m" (*tcb->fpup));
    else
        __asm__ __volatile__ ("fnsave %0; fwait" : "=m" (*tcb->fpup));
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
    struct task_struct *task = tcb->user_task;

    if (task)
        {
        if (!xnarch_fpu_init_p(task))
            {
            stts();
            return;     /* Uninit fpu area -- do not restore. */
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

static inline void xnarch_enable_fpu(xnarchtcb_t *tcb)

{
    struct task_struct *task = tcb->user_task;

    if(task &&  !(task->thread_info->status & TS_USEDFPU))
        return;

    clts();

    if(!cpu_has_fxsr && tcb->user_task)
    /* fnsave also initializes the FPU state, so that on cpus prior to PII
       (i.e. without fxsr), we need to restore the saved state. */
        xnarch_restore_fpu(tcb);
}

#else /* !CONFIG_RTAI_HW_FPU */

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{}

static inline void xnarch_save_fpu (xnarchtcb_t *tcb)

{}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{}

static inline void xnarch_enable_fpu (xnarchtcb_t *tcb)

{}

#endif /* CONFIG_RTAI_HW_FPU */

#endif /* XENO_POD_MODULE */

#ifdef XENO_THREAD_MODULE

static inline void xnarch_init_tcb (xnarchtcb_t *tcb)
{
    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->espp = &tcb->esp;
    tcb->eipp = &tcb->eip;
    tcb->fpup = &tcb->fpuenv;
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
    tcb->espp = &task->thread.esp;
    tcb->eipp = &task->thread.eip;
    tcb->fpup = &task->thread.i387;
}

static inline void xnarch_grab_xirqs (void (*handler)(unsigned irq))

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
        rthal_virtualize_irq(rthal_current_domain,
			     irq,
                             handler,
                             NULL,
                             IPIPE_DYNAMIC_MASK);
}

static inline void xnarch_lock_xirqs (rthal_pipeline_stage_t *ipd, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
        {
        switch (irq)
            {
#ifdef CONFIG_SMP
            case RTHAL_CRITICAL_IPI:
            case INVALIDATE_TLB_VECTOR - FIRST_EXTERNAL_VECTOR:
            case CALL_FUNCTION_VECTOR - FIRST_EXTERNAL_VECTOR:
            case RESCHEDULE_VECTOR - FIRST_EXTERNAL_VECTOR:

                /* Never lock out these ones. */
                continue;
#endif /* CONFIG_SMP */

            default:

                rthal_lock_irq(ipd,cpuid,irq);
            }
        }
}

static inline void xnarch_unlock_xirqs (rthal_pipeline_stage_t *ipd, int cpuid)

{
    unsigned irq;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
        {
        switch (irq)
            {
#ifdef CONFIG_SMP
            case RTHAL_CRITICAL_IPI:
            case INVALIDATE_TLB_VECTOR - FIRST_EXTERNAL_VECTOR:
            case CALL_FUNCTION_VECTOR - FIRST_EXTERNAL_VECTOR:
            case RESCHEDULE_VECTOR - FIRST_EXTERNAL_VECTOR:

                continue;
#endif /* CONFIG_SMP */

            default:

                rthal_unlock_irq(ipd,irq);
            }
        }
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_TIMER_MODULE

static inline void xnarch_program_timer_shot (unsigned long delay)
{
    /* Even though some architectures may use a 64 bits delay here, we
       voluntarily limit to 32 bits, 4 billions ticks should be enough
       for now. Would a timer needs more, an extra call to the tick
       handler would simply occur after 4 billions ticks. */
    rthal_timer_program_shot(rthal_imuldiv(delay,RTHAL_TIMER_FREQ,RTHAL_CPU_FREQ));
}

static inline void xnarch_stop_timer (void)
{
    rthal_timer_release();
}

static inline int xnarch_send_timer_ipi (xnarch_cpumask_t mask)
{
#ifdef CONFIG_SMP
    return rthal_send_ipi(RTHAL_APIC_TIMER_IPI, mask);
#else /* ! CONFIG_SMP */
    return 0;
#endif /* CONFIG_SMP */
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

static int xnarch_trap_fault (unsigned event, unsigned domid, void *data)
{
    struct pt_regs *regs = (struct pt_regs *)data;
    xnarch_fltinfo_t fltinfo;

    fltinfo.vector = event;
    fltinfo.errcode = regs->orig_eax;
    fltinfo.regs = regs;

    return xnpod_trap_fault(&fltinfo);
}

static inline unsigned long xnarch_calibrate_timer (void)

{
#if CONFIG_RTAI_HW_TIMER_LATENCY != 0
    return xnarch_ns_to_tsc(CONFIG_RTAI_HW_TIMER_LATENCY) ?: 1;
#else /* CONFIG_RTAI_HW_TIMER_LATENCY unspecified. */
    /* Compute the time needed to program the PIT in aperiodic
       mode. The return value is expressed in CPU ticks. Depending on
       whether CONFIG_X86_LOCAL_APIC is enabled or not in the kernel
       configuration RTAI is compiled against,
       CONFIG_RTAI_HW_TIMER_LATENCY will either refer to the local
       APIC or 8254 timer latency value. */
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

    xnarch_escalation_virq = rthal_alloc_virq();

    if (xnarch_escalation_virq == 0)
        return -ENOSYS;

    rthal_virtualize_irq(&rthal_domain,
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
        rthal_free_virq(xnarch_escalation_virq);
        }

    return err;
}

static inline void xnarch_exit (void)

{
#ifdef CONFIG_RTAI_OPT_FUSION
    xnshadow_cleanup();
#endif /* CONFIG_RTAI_OPT_FUSION */
    rthal_trap_catch(xnarch_old_trap_handler);
    rthal_free_virq(xnarch_escalation_virq);
}

#endif /* XENO_MAIN_MODULE */

#ifdef __cplusplus
}
#endif

#else /* !__KERNEL__ */

#include <nucleus/system.h>
#include <bits/local_lim.h>

#endif /* __KERNEL__ */

#endif /* !_RTAI_ASM_I386_SYSTEM_H */
