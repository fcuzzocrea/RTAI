/*
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
 * This file implements the interface between the Xenomai nucleus and
 * RTAI/Adeos in kernel space.
 */

#ifndef _RTAI_ASM_I386_XENO_H
#define _RTAI_ASM_I386_XENO_H

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/wrapper.h>
#include <linux/adeos.h>
#include <asm/uaccess.h>
#include <asm/mmu_context.h>
#include <rtai_config.h>
#include <asm/rtai_hal.h>
#include <asm/rtai_xnatomic.h>
#include <xenomai/shadow.h>

#define MODULE_PARM_VALUE(parm) (parm)
#ifndef MODULE_LICENSE
#define MODULE_LICENSE(s)
#endif /* MODULE_LICENSE */

#define MAIN_INIT_MODULE     init_module
#define MAIN_CLEANUP_MODULE  cleanup_module
#define SKIN_INIT_MODULE     init_module
#define SKIN_CLEANUP_MODULE  cleanup_module
#define USER_INIT_MODULE     init_module
#define USER_CLEANUP_MODULE  cleanup_module

typedef unsigned long spl_t;

#define splhigh(x)  arti_local_irq_save(x)
#define splexit(x)  arti_local_irq_restore(x)
#define splnone()   arti_sti()
#define spltest()   arti_local_irq_test()
#define splget(x)   arti_local_irq_flags(x)

#define XNARCH_DEFAULT_TICK  1000000 /* ns, i.e. 1ms */
#define XNARCH_IRQ_MAX       NR_IRQS

#define XNARCH_THREAD_COOKIE  (THIS_MODULE)
#define XNARCH_THREAD_STACKSZ 4096
#define XNARCH_ROOT_STACKSZ   0	/* Only a placeholder -- no stack */

#define xnarch_printf                printk /* Yup! This is safe under ARTI */
#define xnarch_ullmod(ull,uld,rem)   (xnarch_ulldiv(ull,uld,rem), (*rem))
#define xnarch_ulldiv                arti_ulldiv
#define xnarch_imuldiv               arti_imuldiv
#define xnarch_llimd                 arti_llimd
#define xnarch_get_cpu_tsc           arti_rdtsc

struct xnthread;
struct xnmutex;
struct module;
struct task_struct;

#define xnarch_stack_size(tcb)  ((tcb)->stacksize)

typedef struct xnarchtcb {	/* Per-thread arch-dependent block */

    /* Kernel mode side */
    union i387_union fpuenv __attribute__ ((aligned (16))); /* FPU backup area */
    unsigned stacksize;		/* Aligned size of stack (bytes) */
    unsigned long *stackbase;	/* Stack space */
    unsigned long esp;		/* Saved ESP for kernel-based threads */
    unsigned long eip;		/* Saved EIP for kernel-based threads */
    int cr0;			/* A (somewhat phony) version of the cr0 register */
    struct module *module;	/* Creator's module */

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

extern adomain_t xeno_domain;

#ifdef __cplusplus
extern "C" {
#endif

#ifdef XENO_INTR_MODULE

static inline int xnarch_hook_irq (unsigned irq,
				   void (*handler)(unsigned irq,
						   void *cookie),
				   void *cookie)
{
    int err = rt_request_irq(irq,handler,cookie);

    if (!err)
	rt_enable_irq(irq);

    return err;
}

static inline int xnarch_release_irq (unsigned irq) {

    return rt_release_irq(irq);
}

static inline int xnarch_enable_irq (unsigned irq)

{
    if (irq >= XNARCH_IRQ_MAX)
	return -EINVAL;

    rt_enable_irq(irq);

    return 0;
}

static inline int xnarch_disable_irq (unsigned irq)

{
    if (irq >= XNARCH_IRQ_MAX)
	return -EINVAL;

    rt_disable_irq(irq);

    return 0;
}

static inline void xnarch_isr_chain_irq (unsigned irq) {
    rt_pend_linux_irq(irq);
}

static inline void xnarch_isr_enable_irq (unsigned irq) {
    rt_enable_irq(irq);
}

#endif /* XENO_INTR_MODULE */

#ifdef XENO_POD_MODULE

void xnpod_welcome_thread(struct xnthread *);

void xnpod_delete_thread(struct xnthread *,
			 struct xnmutex *mutex);

static void xnarch_recover_jiffies (int irq,
				    void *dev_id,
				    struct pt_regs *regs)
{
    spl_t s;

    splhigh(s);

    if (rt_times.tick_time >= rt_times.linux_time)
	{
	rt_times.linux_time += rt_times.linux_tick;
	rt_pend_linux_irq(TIMER_8254_IRQ);
	}

    splexit(s);
} 

static inline void xnarch_start_timer (int ns, void (*tickhandler)(void))

{
    unsigned period = (unsigned)llimd(ns,ARTI_FREQ_8254,1000000000);
    /* Always use periodic mode. */
    arti_tunables.timers_tol[0] = (rt_times.periodic_tick + 1) >> 1;
    rt_request_timer(tickhandler,period > LATCH ? LATCH : period,0);
}

static inline void xnarch_stop_timer (void)

{
    rt_free_linux_irq(TIMER_8254_IRQ,&xnarch_recover_jiffies);
    rt_free_timer();
}

static inline void xnarch_leave_root (xnarchtcb_t *rootcb) {
    TRACE_RTAI_SWITCHTO_RT(0);
    set_bit(0,&arti_cpu_realtime);
    /* Remember the preempted non-RT task pointer. */
    rootcb->user_task = rootcb->active_task = arti_get_current(0);
}

static inline void xnarch_enter_root (xnarchtcb_t *rootcb) {
    TRACE_RTAI_SWITCHTO_LINUX(0);
    clear_bit(0,&arti_cpu_realtime);
}

#define __switch_threads(out_tcb, in_tcb, outproc, inproc) \
do { \
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
"1: 	 popl %%ebp\n\t" \
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
        "d" (inproc)); \
} while (0)

static inline void xnarch_switch_to (xnarchtcb_t *out_tcb,
				     xnarchtcb_t *in_tcb)
{
    struct task_struct *outproc = out_tcb->active_task;
    struct task_struct *inproc = in_tcb->user_task;

    in_tcb->active_task = inproc ?: outproc;

    if (inproc && inproc != outproc)
	arti_switch_linux_mm(outproc,inproc,0);

    __switch_threads(out_tcb,in_tcb,outproc,inproc);
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
#ifdef CONFIG_RTAI_FPU_SUPPORT

    /* If EM(bit 2) is cleared (i.e. FPU emulate is false), then this
       is a real cr0 value, so we are considering a user-space thread
       (root or shadow).  Otherwise, it is a phony cr0 register, so it
       is a kernel-based thread. */

    if (!(tcb->cr0 & 0x4))
	{
	__asm__ __volatile__ ("movl %%cr0,%0": "=r" (tcb->cr0));
	clts();
	}

    if (cpu_has_fxsr)
	__asm__ __volatile__ ("fxsave %0; fnclex" : "=m" (*tcb->fpup));
    else
	__asm__ __volatile__ ("fnsave %0; fwait" : "=m" (*tcb->fpup));
#endif /* CONFIG_RTAI_FPU_SUPPORT */
}

static inline void xnarch_restore_fpu (xnarchtcb_t *tcb)

{
#ifdef CONFIG_RTAI_FPU_SUPPORT
    clts();

    if (cpu_has_fxsr)
	__asm__ __volatile__ ("fxrstor %0": /* no output */ : "m" (*tcb->fpup));
    else
	__asm__ __volatile__ ("frstor %0": /* no output */ : "m" (*tcb->fpup));

    /* If TS was set for the user-space thread before it yielded
       control to a kernel-based one, reset this bit in cr0 before
       control is given back to the user-space thread. */
       
    if ((tcb->cr0 & 0xc) == 0x8) /* EM=0 and TS=1? */
	stts();
#endif /* CONFIG_RTAI_FPU_SUPPORT */
}

static inline void xnarch_init_root_tcb (xnarchtcb_t *tcb,
					 struct xnthread *thread,
					 const char *name)
{
    tcb->module = THIS_MODULE;
    tcb->user_task = current;
    tcb->active_task = NULL;
    tcb->esp = 0;
    tcb->espp = &tcb->esp;
    tcb->eipp = &tcb->eip;
    tcb->fpup = &tcb->fpuenv;
    tcb->cr0 = 0; /* Clear EM so we know it is a user-space thread */
}

static inline void xnarch_init_tcb (xnarchtcb_t *tcb, void *adcookie) {

    tcb->module = (struct module *)adcookie;
    tcb->user_task = NULL;
    tcb->active_task = NULL;
    tcb->espp = &tcb->esp;
    tcb->eipp = &tcb->eip;
    tcb->fpup = &tcb->fpuenv;
    tcb->cr0 = 0x4; /* Set EM so we know it is a phony cr0 */
    /* Must be followed by xnarch_init_thread(). */
}

static void xnarch_thread_redirect (struct xnthread *self,
				    int imask,
				    void(*entry)(void *),
				    void *cookie)
{
    arti_local_irq_restore(!!imask);
    xnpod_welcome_thread(self);
    entry(cookie);
    xnpod_delete_thread(self,NULL);
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

#ifdef CONFIG_RTAI_FPU_SUPPORT

static inline void xnarch_init_fpu (xnarchtcb_t *tcb)

{
    __asm__ __volatile__ ("clts; fninit");

    if (cpu_has_xmm)
	load_mxcsr(0x1f80);
}

#else /* !CONFIG_RTAI_FPU_SUPPORT */

static inline void xnarch_init_fpu (xnarchtcb_t *tcb) {
}

#endif /* CONFIG_RTAI_FPU_SUPPORT */

int xnarch_setimask (int imask)

{
    spl_t s;
    splhigh(s);
    splexit(!!imask);
    return !!s;
}

static inline void xnarch_announce_tick (void)

{
    rt_times.tick_time = rt_times.intr_time;

    if (rt_times.tick_time >= rt_times.linux_time)
	{
	rt_times.linux_time += rt_times.linux_tick;
	rt_pend_linux_irq(TIMER_8254_IRQ);
	}

    rt_times.intr_time += rt_times.periodic_tick;
}

#define xnarch_notify_ready() /* Nullified */

#endif /* XENO_POD_MODULE */

#ifdef XENO_SHADOW_MODULE

static inline void xnarch_init_shadow_tcb (xnarchtcb_t *tcb,
					   struct xnthread *thread,
					   const char *name)
{
    struct task_struct *task = current;

    tcb->module = THIS_MODULE;
    tcb->user_task = task;
    tcb->active_task = NULL;
    tcb->esp = 0;
    tcb->espp = &task->thread.esp;
    tcb->eipp = &task->thread.eip;
    tcb->fpup = &task->thread.i387;
    tcb->cr0 = 0;
}

#endif /* XENO_SHADOW_MODULE */

#ifdef XENO_HEAP_MODULE

void *xnarch_sysalloc (unsigned bytes) {

    return kmalloc(bytes,GFP_ATOMIC);
}

void xnarch_sysfree (void *chunk, unsigned bytes) {

    kfree(chunk);
}

#else /* !XENO_HEAP_MODULE */

void *xnarch_sysalloc(unsigned bytes);

void xnarch_sysfree(void *chunk,
		    unsigned bytes);

#endif /* XENO_HEAP_MODULE */

#ifdef XENO_MAIN_MODULE

static RT_TRAP_HANDLER xnarch_old_trap_handler;

adomain_t xeno_domain;

extern int xnpod_trap_fault(xnarch_fltinfo_t *fltinfo);

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

static void xnarch_irq_trampoline (unsigned irq) {
    adeos_propagate_irq(irq);
}

static void xnarch_domain_entry (int iflag)

{
    unsigned irq;

    if (iflag)
	for (irq = 0; irq < NR_IRQS; irq++)
	    adeos_virtualize_irq(irq,
				 &xnarch_irq_trampoline,
				 NULL,
				 IPIPE_DYNAMIC_MASK);
    for (;;)
	adeos_suspend_domain();
}

static inline int xnarch_init (void)

{
    adattr_t attr;

    if (!boot_cpu_data.wp_works_ok) /* Needed by the shadow support. */
	{
	printk(KERN_ERR "Sorry, a functional WP bit is needed to run Xenomai/x86");
	return -ENOSYS;
	}

    xnarch_old_trap_handler = rt_set_trap_handler(&xnarch_trap_fault);

    adeos_init_attr(&attr);
    attr.name = "Xenomai";
    attr.domid = 0x59454e4f;
    attr.entry = &xnarch_domain_entry;
    attr.priority = ADEOS_ROOT_PRI + 50;

    if (adeos_register_domain(&xeno_domain,&attr))
	return 1;

    xnshadow_init();

    return 0;
}

static inline void xnarch_exit (void) {

    xnshadow_cleanup();
    rt_set_rtai_trap_handler(xnarch_old_trap_handler);
    adeos_unregister_domain(&xeno_domain);
}

#endif /* XENO_MAIN_MODULE */

static inline unsigned long long xnarch_tsc_to_ns (unsigned long long ts) {
    return xnarch_llimd(ts,1000000000,ARTI_CPU_FREQ);
}

static inline unsigned long long xnarch_get_cpu_time (void) {
    return xnarch_tsc_to_ns(xnarch_get_cpu_tsc());
}

static inline unsigned long long xnarch_get_cpu_freq (void) {
    return ARTI_CPU_FREQ;
}

#define xnarch_halt(emsg) \
do { \
    xnarch_printf("Xenomai: fatal: %s\n",emsg); \
    BUG(); \
} while(0)

int xnarch_setimask(int imask);

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

#endif /* !_RTAI_ASM_I386_XENO_H */
