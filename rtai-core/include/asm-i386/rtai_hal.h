/**
 *   @ingroup hal
 *   @file
 *
 *   ARTI -- RTAI-compatible Adeos-based Real-Time Interface. Based on
 *   the original RTAI layer for x86.
 *
 *   Original RTAI/x86 layer implementation: \n
 *   Copyright &copy; 2000 Paolo Mantegazza, \n
 *   Copyright &copy; 2000 Steve Papacharalambous, \n
 *   Copyright &copy; 2000 Stuart Hughes, \n
 *   and others.
 *
 *   RTAI/x86 rewrite over Adeos: \n
 *   Copyright &copy 2002 Philippe Gerum.
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

#ifndef _RTAI_ASM_I386_HAL_H
#define _RTAI_ASM_I386_HAL_H

#include <asm/rtai_vectors.h>
#include <rtai_types.h>

#ifdef CONFIG_SMP
#define RTAI_NR_CPUS  CONFIG_RTAI_CPUS
#else /* !CONFIG_SMP */
#define RTAI_NR_CPUS  1
#endif /* CONFIG_SMP */

static __inline__ unsigned long ffnz (unsigned long word) {
    /* Derived from bitops.h's ffs() */
    __asm__("bsfl %1, %0"
	    : "=r" (word)
	    : "r"  (word));
    return word;
}

static inline unsigned long long rtai_ulldiv (unsigned long long ull,
					      unsigned long uld,
					      unsigned long *r) {
    /*
     * Fixed by Marco Morandini <morandini@aero.polimi.it> to work
     * with the -fnostrict-aliasing and -O2 combination using GCC
     * 3.x.
     */

    unsigned long long qf, rf;
    unsigned long tq, rh;
    union { unsigned long long ull; unsigned long ul[2]; } p, q;

    p.ull = ull;
    q.ull = 0;
    rf = 0x100000000ULL - (qf = 0xFFFFFFFFUL / uld) * uld;

    while (p.ull >= uld) {
    	q.ul[1] += (tq = p.ul[1] / uld);
	rh = p.ul[1] - tq * uld;
	q.ull  += rh * qf + (tq = p.ul[0] / uld);
	p.ull   = rh * rf + (p.ul[0] - tq * uld);
    }

    if (r)
	*r = p.ull;

    return q.ull;
}

static inline int rtai_imuldiv (int i, int mult, int div) {

    /* Returns (int)i = (int)i*(int)(mult)/(int)div. */
    
    int dummy;

    __asm__ __volatile__ ( \
	"mull %%edx\t\n" \
	"div %%ecx\t\n" \
	: "=a" (i), "=d" (dummy)
       	: "a" (i), "d" (mult), "c" (div));

    return i;
}

static inline long long rtai_llimd(long long ll, int mult, int div) {

    /* Returns (long long)ll = (int)ll*(int)(mult)/(int)div. */

    __asm__ __volatile ( \
	"movl %%edx,%%ecx\t\n" \
	"mull %%esi\t\n" \
	"movl %%eax,%%ebx\n\t" \
	"movl %%ecx,%%eax\t\n" \
        "movl %%edx,%%ecx\t\n" \
        "mull %%esi\n\t" \
	"addl %%ecx,%%eax\t\n" \
	"adcl $0,%%edx\t\n" \
        "divl %%edi\n\t" \
        "movl %%eax,%%ecx\t\n" \
        "movl %%ebx,%%eax\t\n" \
	"divl %%edi\n\t" \
	"sal $1,%%edx\t\n" \
        "cmpl %%edx,%%edi\t\n" \
        "movl %%ecx,%%edx\n\t" \
	"jge 1f\t\n" \
        "addl $1,%%eax\t\n" \
        "adcl $0,%%edx\t\n" \
	"1:\t\n" \
	: "=A" (ll) \
	: "A" (ll), "S" (mult), "D" (div) \
	: "%ebx", "%ecx");

    return ll;
}

/*
 *  u64div32c.c is a helper function provided, 2003-03-03, by:
 *  Copyright (C) 2003 Nils Hagge <hagge@rts.uni-hannover.de>
 */

static inline unsigned long long rtai_u64div32c(unsigned long long a,
						unsigned long b,
						int *r) {
    __asm__ __volatile(
       "\n        movl    %%eax,%%ebx"
       "\n        movl    %%edx,%%eax"
       "\n        xorl    %%edx,%%edx"
       "\n        divl    %%ecx"
       "\n        xchgl   %%eax,%%ebx"
       "\n        divl    %%ecx"
       "\n        movl    %%edx,%%ecx"
       "\n        movl    %%ebx,%%edx"
       : "=a" (((unsigned long *)((void *)&a))[0]), "=d" (((unsigned long *)((void *)&a))[1])
       : "a" (((unsigned long *)((void *)&a))[0]), "d" (((unsigned long *)((void *)&a))[1]), "c" (b)
       : "%ebx"
       );

    return a;
}

#if defined(__KERNEL__) && !defined(__cplusplus)
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/rtai_atomic.h>
#include <asm/rtai_fpu.h>
#ifdef __USE_APIC__
#include <asm/fixmap.h>
#include <asm/apic.h>
#endif /* __USE_APIC__ */
#include <rtai_trace.h>

#define RTAI_DOMAIN_ID  0x52544149
#define RTAI_NR_SRQS    32

#define RTAI_SMP_NOTIFY_VECTOR    RTAI_APIC1_VECTOR
#define RTAI_SMP_NOTIFY_IPI       RTAI_APIC1_IPI
#define RTAI_APIC_TIMER_VECTOR    RTAI_APIC2_VECTOR
#define RTAI_APIC_TIMER_IPI       RTAI_APIC2_IPI

#define RTAI_TIMER_8254_IRQ       0
#define RTAI_FREQ_8254            1193180
#define RTAI_APIC_ICOUNT	  ((RTAI_FREQ_APIC + HZ/2)/HZ)
#define RTAI_COUNTER_2_LATCH      0xfffe
#define RTAI_LATENCY_8254         CONFIG_RTAI_SCHED_8254_LATENCY
#define RTAI_SETUP_TIME_8254      2011 

#define RTAI_CALIBRATED_APIC_FREQ 0
#define RTAI_FREQ_APIC            (rtai_tunables.apic_freq)
#define RTAI_LATENCY_APIC         CONFIG_RTAI_SCHED_APIC_LATENCY
#define RTAI_SETUP_TIME_APIC      1000

#define RTAI_TIME_LIMIT            0x7FFFFFFFFFFFFFFFLL

#define RTAI_IFLAG  9

#define rtai_cli()                     adeos_stall_pipeline_from(&rtai_domain)
#define rtai_sti()                     adeos_unstall_pipeline_from(&rtai_domain)
#define rtai_local_irq_save(x)         ((x) = adeos_test_and_stall_pipeline_from(&rtai_domain))
#define rtai_local_irq_restore(x)      adeos_restore_pipeline_from(&rtai_domain,(x))
#define rtai_local_irq_flags(x)        ((x) = adeos_test_pipeline_from(&rtai_domain))
#define rtai_local_irq_test()          adeos_test_pipeline_from(&rtai_domain)
#define rtai_get_iflag_and_cli()       ((!adeos_test_and_stall_pipeline_from(&rtai_domain)) << RTAI_IFLAG)
/* Use these ones when fiddling with the (local A)PIC */
#define rtai_hw_lock(flags)            adeos_hw_local_irq_save(flags)
#define rtai_hw_unlock(flags)          adeos_hw_local_irq_restore(flags)
#define rtai_hw_enable()               adeos_hw_sti()
#define rtai_hw_disable()              adeos_hw_cli()

#define rtai_linux_sti()                adeos_unstall_pipeline_from(adp_root)
#define rtai_linux_cli()                adeos_stall_pipeline_from(adp_root)
#define rtai_linux_local_irq_save(x)    ((x) = adeos_test_and_stall_pipeline_from(adp_root))
#define rtai_linux_local_irq_restore(x) adeos_restore_pipeline_from(adp_root,x)
#define rtai_linux_local_irq_restore_nosync(x,cpuid) adeos_restore_pipeline_nosync(adp_root,x,cpuid)

typedef void (*rt_irq_handler_t)(unsigned irq,
				 void *cookie);

/* Bits from rtai_status. */
#define RTAI_USE_APIC  0

#ifdef CONFIG_X86_TSC

#define RTAI_CALIBRATED_CPU_FREQ   0
#define RTAI_CPU_FREQ             (rtai_tunables.cpu_freq)

static inline unsigned long long rtai_rdtsc (void) {
    unsigned long long t;
    __asm__ __volatile__( "rdtsc" : "=A" (t));
    return t;
}

#else  /* !CONFIG_X86_TSC */

#define RTAI_CPU_FREQ             RTAI_FREQ_8254
#define RTAI_CALIBRATED_CPU_FREQ  RTAI_FREQ_8254

#define rtai_rdtsc() rd_8254_ts()

#endif /* CONFIG_X86_TSC */

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

extern volatile unsigned long rtai_status;

extern volatile unsigned long rtai_cpu_realtime;

extern volatile unsigned long rtai_cpu_lock;

extern struct rtai_switch_data {
    volatile unsigned long depth;
    volatile unsigned long oldflags;
} rtai_linux_context[RTAI_NR_CPUS];

extern adomain_t rtai_domain;

extern int rtai_adeos_ptdbase;

/* rtai_get_current() is Adeos-specific. Since real-time interrupt
   handlers are called on behalf of the RTAI domain stack, we cannot
   infere the "current" Linux task address using %esp. We must use the
   suspended Linux domain's stack pointer instead. */

static inline struct task_struct *rtai_get_root_current (int cpuid) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
    return (struct task_struct *)(((u_long)adp_root->esp[cpuid]) & (~8191UL));
#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */
    return ((struct thread_info *)(((u_long)adp_root->esp[cpuid]) & (~((THREAD_SIZE)-1))))->task;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */
}

static inline int rtai_adeos_stack_p (int cpuid)

{
    int *esp;
    __asm__ volatile("movl %%esp, %0" : "=r" (esp));
    return (esp >= rtai_domain.estackbase[cpuid] && esp < rtai_domain.estackbase[cpuid] + 2048);
}

static inline struct task_struct *rtai_get_current (int cpuid)

{
    if (rtai_adeos_stack_p(cpuid))
	return rtai_get_root_current(cpuid);

    return get_current();
}

irqreturn_t rtai_broadcast_to_local_timers(int irq,
					   void *dev_id,
					   struct pt_regs *regs);

#ifdef CONFIG_SMP

#define SCHED_VECTOR  RTAI_SMP_NOTIFY_VECTOR
#define SCHED_IPI     RTAI_SMP_NOTIFY_IPI

#define _send_sched_ipi(dest) \
do { \
        apic_wait_icr_idle(); \
        apic_write_around(APIC_ICR2, SET_APIC_DEST_FIELD(dest)); \
        apic_write_around(APIC_ICR, APIC_DEST_LOGICAL | SCHED_VECTOR); \
} while (0)

#ifdef CONFIG_PREEMPT
#define rt_spin_lock(lock)    _raw_spin_lock(lock)
#define rt_spin_unlock(lock)  _raw_spin_unlock(lock)
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
    rtai_local_irq_save(flags);
    rt_spin_lock(lock);
    return flags;
}

static inline void rt_spin_unlock_irqrestore(unsigned long flags,
					     spinlock_t *lock) {
    rt_spin_unlock(lock);
    rtai_local_irq_restore(flags);
}

#define CPU_RELAX(x) cpu_relax()

static inline void rt_get_global_lock(void) {

    adeos_declare_cpuid;

    rtai_cli();

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (!test_and_set_bit(cpuid,&rtai_cpu_lock))
	while (test_and_set_bit(31,&rtai_cpu_lock))
	    CPU_RELAX(cpuid);
}

static inline void rt_release_global_lock(void) {

    adeos_declare_cpuid;

    rtai_cli();

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (test_and_clear_bit(cpuid,&rtai_cpu_lock)) {
	test_and_clear_bit(31,&rtai_cpu_lock);
	CPU_RELAX(cpuid);
    }
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
static inline void rt_global_cli(void) {
    rt_get_global_lock();
}

/**
 * Enable interrupts across all CPUs
 *
 * rt_global_sti hard enables interrupts (sti) on the calling CPU and releases
 * the global lock.
 */
static inline void rt_global_sti(void) {
    rt_release_global_lock();
    rtai_sti();
}

/**
 * Save CPU flags
 *
 * rt_global_save_flags_and_cli combines rt_global_save_flags() and
 * rt_global_cli().
 */
static inline int rt_global_save_flags_and_cli(void) {

    unsigned long flags = rtai_get_iflag_and_cli();
    adeos_declare_cpuid;

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (!test_and_set_bit(cpuid,&rtai_cpu_lock))
	{
	while (test_and_set_bit(31,&rtai_cpu_lock))
	    CPU_RELAX(cpuid);
	return flags | 1;
	}

    return flags;
}

/**
 * Save CPU flags
 *
 * rt_global_save_flags saves the CPU interrupt flag (IF) bit 9 of @a flags and
 * ORs the global lock flag in the first 8 bits of flags. From that you can
 * rightly infer that RTAI does not support more than 8 CPUs.
 */
static inline void rt_global_save_flags(unsigned long *flags) {

    unsigned long hflags = rtai_get_iflag_and_cli(), rflags;
    adeos_declare_cpuid;

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (test_bit(cpuid,&rtai_cpu_lock))
	rflags = hflags;
    else
	rflags = hflags | 1;

    if (hflags)
	rtai_sti();

    *flags = rflags;
}

/**
 * Restore CPU flags
 *
 * rt_global_restore_flags restores the CPU hard interrupt flag (IF)
 * and the state of the global inter-CPU lock, according to the state
 * given by flags.
 */
static inline void rt_global_restore_flags(unsigned long flags) {

    switch (flags)
	{
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
}

#else /* !CONFIG_SMP */

#define _send_sched_ipi(dest)

#define rt_spin_lock(lock)
#define rt_spin_unlock(lock)

#define rt_spin_lock_irq(lock)    do { rtai_cli(); } while (0)
#define rt_spin_unlock_irq(lock)  do { rtai_sti(); } while (0)

static inline unsigned long rt_spin_lock_irqsave(spinlock_t *lock)
{
	unsigned long flags;
	rtai_local_irq_save(flags);
	return flags;
}
#define rt_spin_unlock_irqrestore(flags, lock)  do { rtai_local_irq_restore(flags); } while (0)

#define rt_get_global_lock()      do { rtai_cli(); } while (0)
#define rt_release_global_lock()

#define rt_global_cli()  do { rtai_cli(); } while (0)
#define rt_global_sti()  do { rtai_sti(); } while (0)

static inline unsigned long rt_global_save_flags_and_cli(void)
{
	unsigned long flags;
	rtai_local_irq_save(flags);
	return flags;
}
#define rt_global_restore_flags(flags)  do { rtai_local_irq_restore(flags); } while (0)

#define rt_global_save_flags(flags)     do { rtai_local_irq_flags(*flags); } while (0)

#define CPU_RELAX(x)

#endif

static inline void rt_switch_to_real_time(int cpuid)
{
	TRACE_RTAI_SWITCHTO_RT(cpuid);
	if (!rtai_linux_context[cpuid].depth++) {
		rtai_linux_local_irq_save(rtai_linux_context[cpuid].oldflags);
		test_and_set_bit(cpuid,&rtai_cpu_realtime);
	}
}

static inline int rt_switch_to_linux(int cpuid)
{
	TRACE_RTAI_SWITCHTO_LINUX(cpuid);
	if (rtai_linux_context[cpuid].depth) {
		if (!--rtai_linux_context[cpuid].depth) {
			test_and_clear_bit(cpuid,&rtai_cpu_realtime);
			rtai_linux_local_irq_restore_nosync(rtai_linux_context[cpuid].oldflags, cpuid);
		}
		return 0;
	}
	return 1;
}

static inline void rt_set_timer_delay (int delay) {

    if (delay) {
        unsigned long flags;
        rtai_hw_lock(flags);
#ifdef __USE_APIC__
	apic_read(APIC_TMICT);
	apic_write(APIC_TMICT,delay);
#else /* !__USE_APIC__ */
	outb(delay & 0xff,0x40);
	outb(delay >> 8,0x40);
#endif /* __USE_APIC__ */
        rtai_hw_unlock(flags);
    }
}

    /* Private interface -- Internal use only */

unsigned long rtai_critical_enter(void (*synch)(void));

void rtai_critical_exit(unsigned long flags);

int rtai_calibrate_8254(void);

void rtai_set_linux_task_priority(struct task_struct *task,
				  int policy,
				  int prio);

#endif /* __KERNEL__ && !__cplusplus */

    /* Public interface */

#ifdef __KERNEL__

#include <linux/kernel.h>

#define rt_printk             printk /* This is safe over Adeos */
#define rtai_print_to_screen  printk

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int rt_request_irq(unsigned irq,
		   void (*handler)(unsigned irq, void *cookie),
		   void *cookie);

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
			 irqreturn_t (*handler)(int irq,
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

void rt_request_apic_timers(void (*handler)(void),
			    struct apic_timer_setup_data *tmdata);

void rt_free_apic_timers(void);

int rt_request_timer(void (*handler)(void),
		     unsigned tick,
		     int use_apic);

void rt_free_timer(void);

RT_TRAP_HANDLER rt_set_trap_handler(RT_TRAP_HANDLER handler);

void rt_mount(void);

void rt_umount(void);

RTIME rd_8254_ts(void);

void rt_setup_8254_tsc(void);

void (*rt_set_ihook(void (*hookfn)(int)))(int);

/* Deprecated calls. */

static inline int rt_request_global_irq_ext(unsigned irq,
					    void (*handler)(void),
					    unsigned long cookie) {

    return rt_request_irq(irq,(void (*)(unsigned,void *))handler,(void *)cookie);
}

static inline int rt_request_global_irq(unsigned irq,
					void (*handler)(void)) {

    return rt_request_irq(irq,(void (*)(unsigned,void *))handler,0);
}

static inline void rt_set_global_irq_ext(unsigned irq,
					 int ext,
					 unsigned long cookie) {

    rt_set_irq_cookie(irq,(void *)cookie);
}

static inline int rt_free_global_irq(unsigned irq) {

    return rt_release_irq(irq);
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

#include <asm/rtai_oldnames.h>

#define RTAI_DEFAULT_TICK    100000
#ifdef CONFIG_RTAI_TRACE
#define RTAI_DEFAULT_STACKSZ 8192
#else /* !CONFIG_RTAI_TRACE */
#define RTAI_DEFAULT_STACKSZ 1024
#endif /* CONFIG_RTAI_TRACE */

/*@}*/

#endif /* !_RTAI_ASM_I386_HAL_H */
