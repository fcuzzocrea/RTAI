/**
 *   @ingroup hal_ppc
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
 *   Copyright &copy; 2002-2004 Philippe Gerum.
 *
 *   RTAI/fusion is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   RTAI/fusion is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with RTAI/fusion; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */

/**
 * @addtogroup hal_ppc
 *@{*/

#ifndef _RTAI_ASM_PPC_HAL_H
#define _RTAI_ASM_PPC_HAL_H

#include <rtai_config.h>
#include <asm/div64.h>

#define RTHAL_NR_CPUS  ADEOS_NR_CPUS

typedef unsigned long long rthal_time_t;

#define __rthal_u64tou32(ull, h, l) ({                  \
        union { unsigned long long _ull;                \
            struct { u_long _h; u_long _l; } _s; } _u;  \
        _u._ull = (ull);                                \
        (h) = _u._s._h;                                 \
        (l) = _u._s._l;                                 \
        })

#define __rthal_u64fromu32(h, l) ({                     \
        union { unsigned long long _ull;                \
            struct { u_long _h; u_long _l; } _s; } _u;  \
        _u._s._h = (h);                                 \
        _u._s._l = (l);                                 \
        _u._ull;                                        \
        })

static inline unsigned long long rthal_ullmul(const unsigned long m0, 
					      const unsigned long m1)
{
    return (unsigned long long) m0 * m1;
}

static inline unsigned long long rthal_ulldiv (unsigned long long ull,
					       const unsigned long uld,
					       unsigned long *const rp)
{
#if defined(__KERNEL__) && BITS_PER_LONG == 32
    const unsigned long r = __div64_32(&ull, uld);
#else /* !__KERNEL__ || BITS_PER_LONG == 64 */
    const unsigned long r = ull % uld;
    ull /= uld;
#endif /* __KERNEL__ */

    if (rp)
	*rp = r;

    return ull;
}

#define rthal_uldivrem(ull,ul,rp) ((u_long) rthal_ulldiv((ull),(ul),(rp)))

static inline int rthal_imuldiv (int i, int mult, int div) {

    /* Returns (int)i = (unsigned long long)i*(u_long)(mult)/(u_long)div. */
    const unsigned long long ull = rthal_ullmul(i, mult);
    return rthal_uldivrem(ull, div, NULL);
}

static inline __attribute_const__
unsigned long long __rthal_ullimd (const unsigned long long op,
                                   const unsigned long m,
                                   const unsigned long d)
{
    u_long oph, opl, tlh, tll, qh, rh, ql;
    unsigned long long th, tl;

    __rthal_u64tou32(op, oph, opl);
    tl = rthal_ullmul(opl, m);
    __rthal_u64tou32(tl, tlh, tll);
    th = rthal_ullmul(oph, m);
    th += tlh;

    qh = rthal_uldivrem(th, d, &rh);
    th = __rthal_u64fromu32(rh, tll);
    ql = rthal_uldivrem(th, d, NULL);
    return __rthal_u64fromu32(qh, ql);
}

static inline long long rthal_llimd (long long op,
                                     unsigned long m,
                                     unsigned long d)
{

    if(op < 0LL)
        return -__rthal_ullimd(-op, m, d);
    return __rthal_ullimd(op, m, d);
}

static inline unsigned long ffnz (unsigned long ul) {

    __asm__ __volatile__ ("cntlzw %0, %1" : "=r" (ul) : "r" (ul & (-ul)));
    return 31 - ul;
}

#if defined(__KERNEL__) && !defined(__cplusplus)
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/time.h>
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

extern adomain_t rthal_domain;

#define RTHAL_DOMAIN_ID  0x52544149

#define RTHAL_NR_SRQS    32

#define RTHAL_TIMER_IRQ   ADEOS_TIMER_VIRQ
#define RTHAL_TIMER_FREQ  (rthal_tunables.timer_freq)
#define RTHAL_CPU_FREQ    (rthal_tunables.cpu_freq)

static inline unsigned long long rthal_rdtsc (void) {
    unsigned long long t;
    adeos_hw_tsc(t);
    return t;
}

#define rthal_cli()                     adeos_stall_pipeline_from(&rthal_domain)
#define rthal_sti()                     adeos_unstall_pipeline_from(&rthal_domain)
#define rthal_local_irq_save(x)         ((x) = !!adeos_test_and_stall_pipeline_from(&rthal_domain))
#define rthal_local_irq_restore(x)      adeos_restore_pipeline_from(&rthal_domain,(x))
#define rthal_local_irq_flags(x)        ((x) = !!adeos_test_pipeline_from(&rthal_domain))
#define rthal_local_irq_test()          (!!adeos_test_pipeline_from(&rthal_domain))
#define rthal_local_irq_sync(x)         ((x) = !!adeos_test_and_unstall_pipeline_from(&rthal_domain))

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

#if !defined(CONFIG_ADEOS_NOTHREADS)

/* Since real-time interrupt handlers are called on behalf of the RTAI
   domain stack, we cannot infere the "current" Linux task address
   using %esp. We must use the suspended Linux domain's stack pointer
   instead. */

static inline struct task_struct *rthal_get_root_current (int cpuid) {
    return ((struct thread_info *)(adp_root->esp[cpuid] & (~8191UL)))->task;
}

static inline struct task_struct *rthal_get_current (int cpuid)

{
    register unsigned long esp asm ("r1");
    /* FIXME: r2 should be ok or even __adeos_current_threadinfo() - offsetof(THREAD) */
    
    if (esp >= rthal_domain.estackbase[cpuid] && esp < rthal_domain.estackbase[cpuid] + 8192)
	return rthal_get_root_current(cpuid);

    return current;
}

#else /* CONFIG_ADEOS_NOTHREADS */

static inline struct task_struct *rthal_get_root_current (int cpuid) {
    return current;
}

static inline struct task_struct *rthal_get_current (int cpuid) {
    return current;
}

#endif /* !CONFIG_ADEOS_NOTHREADS */

static inline void rthal_set_timer_shot (unsigned long delay) {

    if (delay) {
#ifdef CONFIG_40x
	mtspr(SPRN_PIT,delay);
#else /* !CONFIG_40x */
	set_dec(delay);
#endif /* CONFIG_40x */
    }
}

    /* Private interface -- Internal use only */

unsigned long rthal_critical_enter(void (*synch)(void));

void rthal_critical_exit(unsigned long flags);

/* The following must be in sync w/ rthal_switch_context() in
   switch.S */
#define RTHAL_SWITCH_FRAME_SIZE  108

void rthal_switch_context(unsigned long *out_kspp,
			  unsigned long *in_kspp);

#ifdef CONFIG_RTAI_HW_FPU

typedef struct rthal_fpenv {
    
    /* This layout must follow exactely the definition of the FPU
       backup area in a PPC thread struct available from
       <asm-ppc/processor.h>. Specifically, fpr[] an fpscr words must
       be contiguous in memory (see arch/ppc/hal/fpu.S). */

    double fpr[32];
    unsigned long fpscr_pad;	/* <= Hi-word of the FPR used to */
    unsigned long fpscr;	/* retrieve the FPSCR. */

} rthal_fpenv_t;

void rthal_init_fpu(rthal_fpenv_t *fpuenv);

void rthal_save_fpu(rthal_fpenv_t *fpuenv);

void rthal_restore_fpu(rthal_fpenv_t *fpuenv);

#ifndef CONFIG_SMP
#define rthal_get_fpu_owner(cur) last_task_used_math
#else /* CONFIG_SMP */
#define rthal_get_fpu_owner(cur) ({                             \
    struct task_struct * _cur = (cur);                          \
    ((_cur->thread.regs && (_cur->thread.regs->msr & MSR_FP))   \
     ? _cur : NULL);                                            \
})
#endif /* CONFIG_SMP */
    
#define rthal_disable_fpu() ({                          \
    register long msr;                                  \
    __asm__ __volatile__ ( "mfmsr %0" : "=r"(msr) );    \
    __asm__ __volatile__ ( "mtmsr %0"                   \
                           : /* no output */            \
                           : "r"(msr & ~(MSR_FP))       \
                           : "memory" );                \
})

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
			   cpumask_t cpumask,
			   cpumask_t *oldmask);

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
