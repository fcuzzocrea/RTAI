/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Hardware Abstraction Layer for PPC.
 *
 *   Original RTAI/ppc layer implementation: \n
 *   Copyright &copy; 2000 Paolo Mantegazza, \n
 *   Copyright &copy; 2001 David Schleef, \n
 *   Copyright &copy; 2001 Lineo, Inc, \n
 *   Copyright &copy; 2004 Wolfgang Grandegger, \n
 *   and others.
 *
 *   Copyright &copy 2002-2004 Philippe Gerum.
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 *   USA; either version 2 of the License, or (at your option) any later
 *   version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/**
 * @addtogroup hal
 *@{*/

#ifndef _RTAI_ASM_PPC_HAL_H
#define _RTAI_ASM_PPC_HAL_H

#include <rtai_config.h>

#ifdef CONFIG_SMP
#define RTHAL_NR_CPUS  CONFIG_RTAI_HW_NRCPUS
#else /* !CONFIG_SMP */
#define RTHAL_NR_CPUS  1
#endif /* CONFIG_SMP */

typedef unsigned long long rthal_time_t;

static inline unsigned long long rthal_ulldiv (unsigned long long ull,
					       unsigned long uld,
					       unsigned long *r)
{
    unsigned long long q, rf;
    unsigned long qh, rh, ql, qf;
    
    q = 0;
    rf = (unsigned long long)(0xFFFFFFFF - (qf = 0xFFFFFFFF / uld) * uld) + 1ULL;
    
    while (ull >= uld) 
	{
	((unsigned long *)&q)[0] += (qh = ((unsigned long *)&ull)[0] / uld);
	rh = ((unsigned long *)&ull)[0] - qh * uld;
	q += rh * (unsigned long long)qf + (ql = ((unsigned long *)&ull)[1] / uld);
	ull = rh * rf + (((unsigned long *)&ull)[1] - ql * uld);
	}

    if (r)
	*r = ull;

    return q;
}

static inline unsigned long long rthal_ullmul(unsigned long m0, 
					      unsigned long m1)
{
    unsigned long long res;
    
    __asm__ __volatile__ ("mulhwu %0, %1, %2"
			  : "=r" (((unsigned long *)&res)[0]) 
			  : "%r" (m0), "r" (m1));

    ((unsigned long *)&res)[1] = m0*m1;
    
    return res;
}

static inline int rthal_imuldiv (int i, int mult, int div) {

    /* Returns (int)i = (int)i*(int)(mult)/(int)div. */
    
    unsigned long q, r;
    q = rthal_ulldiv(rthal_ullmul(i, mult), div, &r);
    return (r + r) > div ? q + 1 : q;
}

static inline long long rthal_llimd(long long ll, int mult, int div) {

    /* Returns (long long)ll = (int)ll*(int)(mult)/(int)div. */

    unsigned long long low;
    unsigned long q, r;
    
    low  = rthal_ullmul(((unsigned long *)&ll)[1], mult);	
    q = rthal_ulldiv(rthal_ullmul(((unsigned long *)&ll)[0], mult) + 
		     ((unsigned long *)&low)[0], div, (unsigned long *)&low);
    low = rthal_ulldiv(low, div, &r);
    ((unsigned long *)&low)[0] += q;
    
    return (r + r) > div ? low + 1 : low;
}

static inline unsigned long ffnz (unsigned long ul) {

    __asm__ __volatile__ ("cntlzw %0, %1" : "=r" (ul) : "r" (ul & (-ul)));
    return 31 - ul;
}

#if defined(__KERNEL__) && !defined(__cplusplus)
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/timex.h>
#include <nucleus/asm/atomic.h>
#include <asm/processor.h>

typedef void (*rthal_irq_handler_t)(unsigned irq,
				    void *cookie);

struct rthal_calibration_data {

    unsigned long cpu_freq;
    unsigned long timer_freq;
};

extern struct rthal_calibration_data rthal_tunables;

extern volatile unsigned long rthal_cpu_realtime;

extern struct rthal_switch_info {
    struct task_struct *oldtask;
    unsigned long oldflags;
} rthal_linux_context[RTHAL_NR_CPUS];

extern adomain_t rthal_domain;

#define RTHAL_DOMAIN_ID  0x52544149

#define RTHAL_NR_SRQS    32

#define RTHAL_TIMER_FREQ  (rthal_tunables.timer_freq)
#define RTHAL_CPU_FREQ    (rthal_tunables.cpu_freq)

static inline unsigned long long rthal_rdtsc (void) {
    unsigned long long t;
    adeos_hw_tsc(t);
    return t;
}

#define rthal_cli()                     adeos_stall_pipeline_from(&rthal_domain)
#define rthal_sti()                     adeos_unstall_pipeline_from(&rthal_domain)
#define rthal_sync_irqs()               adeos_sync_pipeline()
#define rthal_local_irq_save(x)         ((x) = !!adeos_test_and_stall_pipeline_from(&rthal_domain))
#define rthal_local_irq_restore(x)      adeos_restore_pipeline_from(&rthal_domain,(x))
#define rthal_local_irq_flags(x)        ((x) = !!adeos_test_pipeline_from(&rthal_domain))
#define rthal_local_irq_test()          (!!adeos_test_pipeline_from(&rthal_domain))

#define rthal_hw_lock(flags)            adeos_hw_local_irq_save(flags)
#define rthal_hw_unlock(flags)          adeos_hw_local_irq_restore(flags)
#define rthal_hw_enable()               adeos_hw_sti()
#define rthal_hw_disable()              adeos_hw_cli()

#define rthal_linux_sti()                adeos_unstall_pipeline_from(adp_root)
#define rthal_linux_cli()                adeos_stall_pipeline_from(adp_root)
#define rthal_linux_local_irq_save(x)    ((x) = !!adeos_test_and_stall_pipeline_from(adp_root))
#define rthal_linux_local_irq_restore(x) adeos_restore_pipeline_from(adp_root,x)
#define rthal_linux_local_irq_restore_nosync(x,cpuid) adeos_restore_pipeline_nosync(adp_root,x,cpuid)

#define rthal_spin_lock(lock)    adeos_spin_lock(lock)
#define rthal_spin_unlock(lock)  adeos_spin_unlock(lock)

static inline void rthal_spin_lock_irq(spinlock_t *lock) {

    rthal_cli();
    rthal_spin_lock(lock);
}

static inline void rthal_spin_unlock_irq(spinlock_t *lock) {

    rthal_spin_unlock(lock);
    rthal_sti();
}

static inline unsigned long rthal_spin_lock_irqsave(spinlock_t *lock) {

    unsigned long flags;
    rthal_local_irq_save(flags);
    rthal_spin_lock(lock);
    return flags;
}

static inline void rthal_spin_unlock_irqrestore(unsigned long flags,
						spinlock_t *lock) {
    rthal_spin_unlock(lock);
    rthal_local_irq_restore(flags);
}

#ifdef CONFIG_SMP
#define rthal_cpu_relax(x) \
do { \
   int i = 0; \
   do \
     cpu_relax(); \
   while (++i < x); \
} while(0)
#endif /* CONFIG_SMP */

/* Since real-time interrupt handlers are called on behalf of the RTAI
   domain stack, we cannot infere the "current" Linux task address
   using %esp. We must use the suspended Linux domain's stack pointer
   instead. */

static inline struct task_struct *rthal_get_root_current (int cpuid) {
    return ((struct thread_info *)(((u_long)adp_root->esp[cpuid]) & (~8191UL)))->task;
}

static inline struct task_struct *rthal_get_current (int cpuid)

{
    register int *esp asm ("r1");
    
    if (esp >= rthal_domain.estackbase[cpuid] && esp < rthal_domain.estackbase[cpuid] + 2048)
	return rthal_get_root_current(cpuid);

    return current;
}

static inline void rthal_set_timer_shot (unsigned long delay) {

    if (delay) {
        unsigned long flags;
        rthal_hw_lock(flags);
	
        rthal_hw_unlock(flags);
    }
}

    /* Private interface -- Internal use only */

unsigned long rthal_critical_enter(void (*synch)(void));

void rthal_critical_exit(unsigned long flags);

void rthal_set_linux_task_priority(struct task_struct *task,
				   int policy,
				   int prio);

void rthal_switch_context(unsigned long *out_kspp,
			  unsigned long *in_kspp);

#ifdef CONFIG_RTAI_HW_FPU

typedef struct rthal_fpenv {
    
    /* This layout must follow exactely the definition of the FPU
       backup area in a PPC thread struct available from
       <asm-ppc/processor.h>. Specifically, fpr[] an fpscr words must
       be contiguous in memory (see arch/ppc/fpu.S). */

    double fpr[32];
    unsigned long fpscr_pad;	/* <= Hi-word of the FPR used to */
    unsigned long fpscr;	/* retrieve the FPSCR. */

} rthal_fpenv_t;

void rthal_init_fpu(rthal_fpenv_t *fpuenv);

void rthal_save_fpu(rthal_fpenv_t *fpuenv);

void rthal_restore_fpu(rthal_fpenv_t *fpuenv);

#endif /* CONFIG_RTAI_HW_FPU */

#endif /* __KERNEL__ && !__cplusplus */

    /* Public interface */

#ifdef __KERNEL__

#include <linux/kernel.h>

typedef int (*rthal_trap_handler_t)(adevinfo_t *evinfo);

#define rthal_printk    printk /* This is safe over Adeos */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int rthal_request_irq(unsigned irq,
		      void (*handler)(unsigned irq, void *cookie),
		      void *cookie);

int rthal_release_irq(unsigned irq);

/**
 * @name Programmable Interrupt Controllers (PIC) management functions.
 *
 *@{*/

int rthal_enable_irq(unsigned irq);

int rthal_disable_irq(unsigned irq);

/*@}*/

int rthal_request_linux_irq(unsigned irq,
			    irqreturn_t (*handler)(int irq,
						   void *dev_id,
						   struct pt_regs *regs), 
			    char *name,
			    void *dev_id);

int rthal_release_linux_irq(unsigned irq,
			    void *dev_id);

int rthal_pend_linux_irq(unsigned irq);

int rthal_pend_linux_srq(unsigned srq);

int rthal_request_srq(unsigned label,
		      void (*handler)(void));

int rthal_release_srq(unsigned srq);

int rthal_set_irq_affinity(unsigned irq,
			   unsigned long cpumask);

int rthal_reset_irq_affinity(unsigned irq);

int rthal_request_timer(void (*handler)(void),
			unsigned long nstick);

void rthal_release_timer(void);

rthal_trap_handler_t rthal_set_trap_handler(rthal_trap_handler_t handler);

unsigned long rthal_calibrate_timer(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

/*@}*/

#endif /* !_RTAI_ASM_PPC_HAL_H */
