/**
 *   @ingroup hal
 *   @file
 *
 *   ARTI -- RTAI-compatible Adeos-based Real-Time Interface. Based on
 *   the original RTAI layer for PPC and the RTAI/x86 rewrite over ADEOS.
 *
 *   Original RTAI/PPC layer implementation: \n
 *   Copyright &copy; 2000 Paolo Mantegazza, \n
 *   Copyright &copy; 2001 David Schleef, \n
 *   Copyright &copy; 2001 Lineo, Inc, \n
 *   Copyright &copy; 2002 Wolfgang Grandegger. \n
 *
 *   RTAI/x86 rewrite over Adeos: \n
 *   Copyright &copy 2002 Philippe Gerum.
 *
 *   RTAI/PPC rewrite over Adeos: \n
 *   Copyright &copy 2004 Wolfgang Grandegger.
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

#include <asm/rtai_vectors.h>
#include <rtai_types.h>

#ifdef CONFIG_SMP
#define RTAI_NR_CPUS  CONFIG_RTAI_CPUS
#else /* !CONFIG_SMP */
#define RTAI_NR_CPUS  1
#endif /* CONFIG_SMP */

static inline int ffnz(unsigned long ul) {

	__asm__ __volatile__ ("cntlzw %0, %1" : "=r" (ul) : "r" (ul & (-ul)));

	return 31 - ul;
}

/* One of the silly thing of 32 bits PPCs, no 64 bits result for 32 bits mul. */
static inline unsigned long long rtai_ullmul(unsigned long m0, 
					     unsigned long m1) {

    unsigned long long res;
    
    __asm__ __volatile__ ("mulhwu %0, %1, %2"
			  : "=r" (((unsigned long *)(void *)&res)[0]) 
			  : "%r" (m0), "r" (m1));
    ((unsigned long *)(void *)&res)[1] = m0*m1;
    
    return res;
}

/* One of the silly thing of 32 bits PPCs, no 64 by 32 bits divide. */
static inline unsigned long long rtai_ulldiv(unsigned long long ull, 
					     unsigned long uld, 
					     unsigned long *r) {

    unsigned long long q, rf;
    unsigned long qh, rh, ql, qf;
    
    q = 0;
    rf = (unsigned long long)(0xFFFFFFFF - (qf = 0xFFFFFFFF / uld) * uld) + 1ULL;
    
    while (ull >= uld) 
	{
	((unsigned long *)(void *)&q)[0] += (qh = ((unsigned long *)(void *)&ull)[0] / uld);
	rh = ((unsigned long *)(void *)&ull)[0] - qh * uld;
	q += rh * (unsigned long long)qf + (ql = ((unsigned long *)(void *)&ull)[1] / uld);
	ull = rh * rf + (((unsigned long *)(void *)&ull)[1] - ql * uld);
	}
    
    *r = ull;
    return q;
}

static inline int rtai_imuldiv(int i, int mult, int div) {

    /* Returns (int)i = (int)i*(int)(mult)/(int)div. */

    unsigned long q, r;
    
    q = rtai_ulldiv(rtai_ullmul(i, mult), div, &r);
    
    return (r + r) > div ? q + 1 : q;
}

static inline unsigned long long rtai_llimd(unsigned long long ull, 
					    unsigned long mult, 
					    unsigned long div) {

    /* Returns (long long)ll = (int)ll*(int)(mult)/(int)div. */

    unsigned long long low;
    unsigned long q, r;
    
    low  = rtai_ullmul(((unsigned long *)(void *)&ull)[1], mult);	
    q = rtai_ulldiv(rtai_ullmul(((unsigned long *)(void *)&ull)[0], mult) + 
		((unsigned long *)(void *)&low)[0], div, (unsigned long *)(void *)&low);
    low = rtai_ulldiv(low, div, &r);
    ((unsigned long *)(void *)&low)[0] += q;
    
    return (r + r) > div ? low + 1 : low;
}


#if defined(__KERNEL__) && !defined(__cplusplus)
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/time.h>
#include <asm/rtai_atomic.h>
#include <asm/rtai_fpu.h>
#include <rtai_trace.h>

#define RTAI_DOMAIN_ID  0x52544149
#define RTAI_NR_SRQS    32

#ifdef FIXME
#define RTAI_SMP_NOTIFY_VECTOR    RTAI_APIC3_VECTOR
#define RTAI_SMP_NOTIFY_IPI       RTAI_APIC3_IPI
#define RTAI_APIC_TIMER_VECTOR    RTAI_APIC4_VECTOR
#define RTAI_APIC_TIMER_IPI       RTAI_APIC4_IPI
#endif

#define RTAI_TIMER_DECR_IRQ       IPIPE_VIRQ_BASE
#define RTAI_TIMER_8254_IRQ       RTAI_TIMER_DECR_IRQ
#define RTAI_FREQ_8254            (rtai_tunables.cpu_freq)
#ifdef FIXME
#define RTAI_APIC_ICOUNT	  ((RTAI_FREQ_APIC + HZ/2)/HZ)
#define RTAI_COUNTER_2_LATCH      0xfffe
#endif
#define RTAI_LATENCY_8254         CONFIG_RTAI_SCHED_8254_LATENCY
#define RTAI_SETUP_TIME_8254      500

#ifdef FIXME
#define RTAI_CALIBRATED_APIC_FREQ 0
#define RTAI_FREQ_APIC            (rtai_tunables.apic_freq)
#define RTAI_LATENCY_APIC         CONFIG_RTAI_SCHED_APIC_LATENCY
#define RTAI_SETUP_TIME_APIC      1000
#endif

#define RTAI_TIME_LIMIT           0x7FFFFFFFFFFFFFFFLL

#define RTAI_IFLAG  15

#define rtai_cpuid()  adeos_processor_id()
#define rtai_tskext   ptd

/* Use these to grant atomic protection when accessing the hardware */
#define rtai_hw_cli()                  adeos_hw_cli()
#define rtai_hw_sti()                  adeos_hw_sti()
#define rtai_hw_save_flags_and_cli(x)  adeos_hw_local_irq_save(flags)
#define rtai_hw_restore_flags(x)       adeos_hw_local_irq_restore(flags)
#define rtai_hw_save_flags(x)          adeos_hw_local_irq_flags(flags)

/* Use these to grant atomic protection in hard real time code */
#define rtai_cli()                  adeos_hw_cli()
#define rtai_sti()                  adeos_hw_sti()
#define rtai_save_flags_and_cli(x)  adeos_hw_local_irq_save(flags)
#define rtai_restore_flags(x)       adeos_hw_local_irq_restore(flags)
#define rtai_save_flags(x)          adeos_hw_local_irq_flags(flags)

/* The only Linux irq flags manipulation lacking in its system.h */
#define local_irq_restore_nosync(flags, cpuid)  do { adp_root->cpudata[cpuid].status = flags; } while (0)

extern unsigned long adeos_pended;

#define adeos_pend_uncond(irq, cpuid) \
do { \
        adp_root->cpudata[cpuid].irq_hits[irq]++; \
        __set_bit(irq & IPIPE_IRQ_IMASK, &adp_root->cpudata[cpuid].irq_pending_lo[irq >> IPIPE_IRQ_ISHIFT]); \
        __set_bit(irq >> IPIPE_IRQ_ISHIFT, &adp_root->cpudata[cpuid].irq_pending_hi); \
        __set_bit(cpuid, &adeos_pended); \
} while (0)

#ifdef CONFIG_PREEMPT
#define rtai_save_and_lock_preempt_count() \
        do { int *prcntp, prcnt; prcnt = xchg(prcntp = &preempt_count(), 1);
#define rtai_restore_preempt_count() \
             *prcntp = prcnt; } while (0)
#else
#define rtai_save_and_lock_preempt_count();
#define rtai_restore_preempt_count();
#endif

typedef int (*rt_irq_handler_t)(unsigned irq, void *cookie);

/* Bits from rtai_status. */
#define RTAI_USE_APIC  0

#define RTAI_CALIBRATED_CPU_FREQ   0
#define RTAI_CPU_FREQ             (rtai_tunables.cpu_freq)

static inline unsigned long long rtai_rdtsc (void) {

	unsigned long long ts;
	unsigned long chk;
	/* See Motorola reference manual for 32 bits PPCs. */
	__asm__ __volatile__ ("1: mftbu %0\n"
			      "   mftb %1\n"
			      "   mftbu %2\n"
			      "   cmpw %2,%0\n"
			      "   bne 1b\n"
			      : "=r" (((unsigned long *)&ts)[0]), 
			        "=r" (((unsigned long *)&ts)[1]), 
			        "=r" (chk));
	return ts;
}

struct calibration_data {

    unsigned long cpu_freq;
    unsigned long apic_freq;
    int latency;
    int setup_time_TIMER_CPUNIT;
    int setup_time_TIMER_UNIT;
    int timers_tol[RTAI_NR_CPUS];
};

struct apic_timer_setup_data {

    int mode;
    int count;
};

extern struct rt_times rt_times;

extern struct rt_times rt_smp_times[RTAI_NR_CPUS];

extern struct calibration_data rtai_tunables;

extern volatile unsigned long rtai_cpu_realtime;

extern volatile unsigned long rtai_cpu_lock;

extern struct rtai_switch_data {
    volatile unsigned long depth;
    volatile unsigned long oldflags;
} rtai_linux_context[RTAI_NR_CPUS];

extern adomain_t rtai_domain;

extern int rtai_adeos_ptdbase;

#ifdef CONFIG_SMP

#ifdef CONFIG_PREEMPT
#define rt_spin_lock(lock)    do { barrier(); _raw_spin_lock(lock); barrier(); } while (0)
#define rt_spin_unlock(lock)  do { barrier(); _raw_spin_unlock(lock); barrier(); } while (0)
#else /* !CONFIG_PREEMPT */
#define rt_spin_lock(lock)    spin_lock(lock)
#define rt_spin_unlock(lock)  spin_unlock(lock)
#endif /* CONFIG_PREEMPT */

static inline void rt_spin_lock_irq(spinlock_t *lock) {

    rtai_cli();
    rt_spin_lock(lock);
}

static inline void rt_spin_unlock_irq(spinlock_t *lock) {

    rt_spin_unlock(lock);
    rtai_sti();
}

static inline unsigned long rt_spin_lock_irqsave(spinlock_t *lock) {

    unsigned long flags;
    rtai_save_flags_and_cli(flags);
    rt_spin_lock(lock);
    return flags;
}

static inline void rt_spin_unlock_irqrestore(unsigned long flags, spinlock_t *lock) 
{
	rt_spin_unlock(lock);
	rtai_local_irq_restore(flags);
}

static inline void rt_get_global_lock(void)
{
        barrier();
        rtai_cli();
        if (!test_and_set_bit(adeos_processor_id(), &rtai_cpu_lock)) {
                while (test_and_set_bit(31, &rtai_cpu_lock)) {
                        cpu_relax();
                }
        }
        barrier();
}

static inline void rt_release_global_lock(void)
{
        barrier();
        rtai_cli();
        if (test_and_clear_bit(adeos_processor_id(), &rtai_cpu_lock)) {
                test_and_clear_bit(31, &rtai_cpu_lock);
                cpu_relax();
        }
        barrier();
}

/**
 * Disable interrupts across all CPUs
 *
 * rt_global_cli hard disables interrupts (cli) on the requesting CPU and
 * acquires the global spinlock to the calling CPU so that any other CPU
 * synchronized by this method is blocked. Nested calls to rt_global_cli within
 * the owner CPU will not cause a deadlock on the global spinlock, as it would
 * happen for a normal spinlock.
 *
 * rt_global_sti hard enables interrupts (sti) on the calling CPU and releases
 * the global lock.
 */
static inline void rt_global_cli(void) 
{
    rt_get_global_lock();
}

/**
 * Enable interrupts across all CPUs
 *
 * rt_global_sti hard enables interrupts (sti) on the calling CPU and releases
 * the global lock.
 */
static inline void rt_global_sti(void) 
{
    rt_release_global_lock();
    rtai_sti();
}

static volatile inline unsigned long rtai_save_flags_irqbit(void)
{
        unsigned long flags;
        rtai_save_flags(flags);
        return flags & (1 << RTAI_IFLAG);
}
static volatile inline unsigned long rtai_save_flags_irqbit_and_cli(void)
{
        unsigned long flags;
        rtai_save_flags_and_cli(flags);
        return flags & (1 << RTAI_IFLAG);
}

/**
 * Save CPU flags
 *
 * rt_global_save_flags_and_cli combines rt_global_save_flags() and
 * rt_global_cli().
 */
static inline int rt_global_save_flags_and_cli(void)
{
        barrier();
        unsigned long flags = rtai_save_flags_irqbit_and_cli();

        if (!test_and_set_bit(adeos_processor_id(), &rtai_cpu_lock)) {
                while (test_and_set_bit(31, &rtai_cpu_lock)) {
                        cpu_relax();
                }
                barrier();
                return flags | 1;
        }
        barrier();
        return flags;
}

/**
 * Save CPU flags
 *
 * rt_global_save_flags saves the CPU interrupt flag (IF) bit 9 of @a flags and
 * ORs the global lock flag in the first 8 bits of flags. From that you can
 * rightly infer that RTAI does not support more than 8 CPUs.
 */
static inline void rt_global_save_flags(unsigned long *flags)
{
        unsigned long hflags = rtai_save_flags_irqbit_and_cli();

        *flags = test_bit(adeos_processor_id(), &rtai_cpu_lock) ? hflags : hflags | 1;
        if (hflags) {
                rtai_sti();
        }
}

/**
 * Restore CPU flags
 *
 * rt_global_restore_flags restores the CPU hard interrupt flag (IF)
 * and the state of the global inter-CPU lock, according to the state
 * given by flags.
 */
static inline void rt_global_restore_flags(unsigned long flags)
{
        barrier();
        switch (flags & ((1 << RTAI_IFLAG) | 1)) {
                case (1 << RTAI_IFLAG) | 1:
                        rt_release_global_lock();
                        rtai_sti();
                        break;
                case (1 << RTAI_IFLAG) | 0:
                        rt_get_global_lock();
                        rtai_sti();
                        break;
                case (0 << RTAI_IFLAG) | 1:
                        rt_release_global_lock();
                        break;
                case (0 << RTAI_IFLAG) | 0:
                        rt_get_global_lock();
                        break;
        }
        barrier();
}

#else /* !CONFIG_SMP */

#define rt_spin_lock(lock)
#define rt_spin_unlock(lock)

#define rt_spin_lock_irq(lock)    do { rtai_cli(); } while (0)
#define rt_spin_unlock_irq(lock)  do { rtai_sti(); } while (0)

static inline unsigned long rt_spin_lock_irqsave(spinlock_t *lock)
{
        unsigned long flags;
        rtai_save_flags_and_cli(flags);
        return flags;
}
#define rt_spin_unlock_irqrestore(flags, lock)  do { rtai_restore_flags(flags); } while (0)

#define rt_get_global_lock()      do { rtai_cli(); } while (0)
#define rt_release_global_lock()

#define rt_global_cli()  do { rtai_cli(); } while (0)
#define rt_global_sti()  do { rtai_sti(); } while (0)

static inline unsigned long rt_global_save_flags_and_cli(void)
{
        unsigned long flags;
        rtai_save_flags_and_cli(flags);
        return flags;
}
#define rt_global_restore_flags(flags)  do { rtai_restore_flags(flags); } while (0)

#define rt_global_save_flags(flags)     do { rtai_save_flags(*flags); } while (0)

#endif

int rt_printk(const char *format, ...);

static inline void rt_switch_to_real_time(int cpuid)
{
        TRACE_RTAI_SWITCHTO_RT(cpuid);
        if (!rtai_linux_context[cpuid].depth++) {
                rtai_linux_context[cpuid].oldflags = xchg(&adp_root->cpudata[cpuid].status, (1 << IPIPE_STALL_FLAG));
                test_and_set_bit(cpuid, &rtai_cpu_realtime);
        }
}

static inline void rt_switch_to_linux(int cpuid)
{
        TRACE_RTAI_SWITCHTO_LINUX(cpuid);
        if (rtai_linux_context[cpuid].depth) {
                if (!--rtai_linux_context[cpuid].depth) {
                        test_and_clear_bit(cpuid, &rtai_cpu_realtime);
                        adp_root->cpudata[cpuid].status = rtai_linux_context[cpuid].oldflags;
                }
                return;
        }
        rt_printk("*** ERROR: EXCESS LINUX_UNLOCK ***\n");
}

#define in_hrt_mode(cpuid)  (test_bit(cpuid, &rtai_cpu_realtime))

static inline void rt_set_timer_delay (int delay) {

    /* NOTE: delay MUST be 0 if a periodic timer is being used. */
    if (delay == 0) 
	{
#ifdef CONFIG_40x
	return;
#else  /* !CONFIG_40x */
	if ((delay = rt_times.intr_time - rtai_rdtsc()) <= 0)
	    {
	    int lost = 0;
	    do 
		{
		lost++;
		rt_times.intr_time += (RTIME)rt_times.periodic_tick;
		}
	    while ((delay = rt_times.intr_time - rtai_rdtsc()) <= 0);
	    printk("%d timer interrupt(s) lost\n", lost);
	    }
#endif /* CONFIG_40x */
	}
#ifdef CONFIG_40x
    mtspr(SPRN_PIT, delay);
#else
    set_dec(delay);
#endif
}

    /* Private interface -- Internal use only */

#ifdef FIXME
void rtai_attach_lxrt(void);

void rtai_detach_lxrt(void);

void rtai_switch_linux_mm(struct task_struct *prev,
			  struct task_struct *next,
			  int cpuid);
#endif

#endif /* __KERNEL__ && !__cplusplus */

    /* Public interface */

#ifdef __KERNEL__

#include <linux/kernel.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int rt_request_irq(unsigned irq, int (*handler)(unsigned irq, void *cookie), void *cookie, int retmode);

int rt_release_irq(unsigned irq);

void rt_set_irq_cookie(unsigned irq,
		       void *cookie);

/**
 * @name Programmable Interrupt Controllers (PIC) management functions.
 *
 *@{*/
unsigned rt_startup_irq(unsigned irq);

void rt_shutdown_irq(unsigned irq);

void rt_enable_irq(unsigned irq);

void rt_disable_irq(unsigned irq);

void rt_mask_and_ack_irq(unsigned irq);

void rt_unmask_irq(unsigned irq);

void rt_ack_irq(unsigned irq);
/*@}*/

void rt_do_irq(unsigned irq);

int rt_request_linux_irq(unsigned irq,
			 int (*handler)(int irq,
			 void *dev_id,
			 struct pt_regs *regs), 
			 char *name,
			 void *dev_id);

int rt_free_linux_irq(unsigned irq,
		      void *dev_id);

void rt_pend_linux_irq(unsigned irq);

void rt_pend_linux_srq(unsigned srq);

int rt_request_srq(unsigned label,
		   void (*k_handler)(void),
		   long long (*u_handler)(unsigned));

int rt_free_srq(unsigned srq);

int rt_assign_irq_to_cpu(int irq,
			 unsigned long cpus_mask);

int rt_reset_irq_to_sym_mode(int irq);

void rt_request_timer_cpuid(void (*handler)(void),
			    unsigned tick,
			    int cpuid);

#ifdef FIXME
void rt_request_apic_timers(void (*handler)(void),
			    struct apic_timer_setup_data *tmdata);

void rt_free_apic_timers(void);
#endif

int rt_request_timer(void (*handler)(void),
		     unsigned tick,
		     int use_apic);

void rt_free_timer(void);

#ifdef FIXME
RT_TRAP_HANDLER rt_set_trap_handler(RT_TRAP_HANDLER handler);
#endif

void rt_mount(void);

void rt_umount(void);

void (*rt_set_ihook(void (*hookfn)(int)))(int);

#define rt_printk printk /* This is safe over Adeos */

unsigned long rtai_critical_enter(void (*synch)(void));

void rtai_critical_exit(unsigned long flags);

void rtai_set_linux_task_priority(struct task_struct *task, int policy, int prio);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#include <asm/rtai_oldnames.h>

#define RTAI_DEFAULT_TICK    100000
#ifdef CONFIG_RTAI_TRACE
#define RTAI_DEFAULT_STACKSZ 8192
#else /* !CONFIG_RTAI_TRACE */
#define RTAI_DEFAULT_STACKSZ 4092
#endif /* CONFIG_RTAI_TRACE */

/*@}*/

#endif /* !_RTAI_ASM_PPC_HAL_H */
