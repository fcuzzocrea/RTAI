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
#define ARTI_NR_CPUS  CONFIG_RTAI_CPUS
#else /* !CONFIG_SMP */
#define ARTI_NR_CPUS  1
#endif /* CONFIG_SMP */

static __inline__ unsigned long ffnz (unsigned long word) {
    /* Derived from bitops.h's ffs() */
    __asm__("bsfl %1, %0"
	    : "=r" (word)
	    : "r"  (word));
    return word;
}

static inline unsigned long long arti_ulldiv (unsigned long long ull,
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

    *r = p.ull;

    return q.ull;
}

static inline int arti_imuldiv (int i, int mult, int div) {

    /* Returns (int)i = (int)i*(int)(mult)/(int)div. */
    
    int dummy;

    __asm__ __volatile__ ( \
	"mull %%edx\t\n" \
	"div %%ecx\t\n" \
	: "=a" (i), "=d" (dummy)
       	: "a" (i), "d" (mult), "c" (div));

    return i;
}

static inline long long arti_llimd(long long ll, int mult, int div) {

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

#if defined(__KERNEL__) && !defined(__cplusplus)
#include <linux/sched.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/rtai_atomic.h>
#include <asm/rtai_fpu.h>
#ifdef __USE_APIC__
#include <asm/fixmap.h>
#include <asm/apic.h>
#endif /* __USE_APIC__ */
#include <rtai_trace.h>

#define ARTI_DOMAIN_ID  0x52544149
#define ARTI_NR_SRQS    32

#define ARTI_SMP_NOTIFY_VECTOR    RTAI_APIC3_VECTOR
#define ARTI_SMP_NOTIFY_IPI       RTAI_APIC3_IPI
#define ARTI_APIC_TIMER_VECTOR    RTAI_APIC4_VECTOR
#define ARTI_APIC_TIMER_IPI       RTAI_APIC4_IPI

#define ARTI_TIMER_8254_IRQ       0
#define ARTI_FREQ_8254            1193180
#define ARTI_APIC_ICOUNT	  ((ARTI_FREQ_APIC + HZ/2)/HZ)
#define ARTI_COUNTER_2_LATCH      0xfffe
#define ARTI_LATENCY_8254         CONFIG_RTAI_SCHED_8254_LATENCY
#define ARTI_SETUP_TIME_8254      2011 

#define ARTI_CALIBRATED_APIC_FREQ 0
#define ARTI_FREQ_APIC            (arti_tunables.apic_freq)
#define ARTI_LATENCY_APIC         CONFIG_RTAI_SCHED_APIC_LATENCY
#define ARTI_SETUP_TIME_APIC      1000

#define ARTI_TIME_LIMIT            0x7FFFFFFFFFFFFFFFLL

#define ARTI_IFLAG  9

#define arti_cli()                     adeos_stall_pipeline_from(&arti_domain)
#define arti_sti()                     adeos_unstall_pipeline_from(&arti_domain)
#define arti_local_irq_save(x)         ((x) = adeos_test_and_stall_pipeline_from(&arti_domain))
#define arti_local_irq_restore(x)      adeos_restore_pipeline_from(&arti_domain,(x))
#define arti_local_irq_flags(x)        ((x) = adeos_test_pipeline_from(&arti_domain))
#define arti_local_irq_test()          adeos_test_pipeline_from(&arti_domain)
#define arti_get_iflag_and_cli()       ((!adeos_test_and_stall_pipeline_from(&arti_domain)) << ARTI_IFLAG)
/* Use these ones when fiddling with the (local A)PIC */
#define arti_hw_lock(flags)            adeos_hw_local_irq_save(flags)
#define arti_hw_unlock(flags)          adeos_hw_local_irq_restore(flags)
#define arti_hw_enable()               adeos_hw_sti()
#define arti_hw_disable()              adeos_hw_cli()

typedef void (*rt_irq_handler_t)(unsigned irq,
				 void *cookie);
void arti_linux_cli(void);

void arti_linux_sti(void);

unsigned arti_linux_save_flags(void);

void arti_linux_restore_flags(unsigned flags);

void arti_linux_restore_flags_nosync(unsigned flags, int cpuid);

unsigned arti_linux_save_flags_and_cli(void);

/* Bits from arti_status. */
#define ARTI_USE_APIC  0

#ifdef CONFIG_X86_TSC

#define ARTI_CALIBRATED_CPU_FREQ   0
#define ARTI_CPU_FREQ             (arti_tunables.cpu_freq)

static inline unsigned long long arti_rdtsc (void) {
    unsigned long long t;
    __asm__ __volatile__( "rdtsc" : "=A" (t));
    return t;
}

#else  /* !CONFIG_X86_TSC */

#define ARTI_EMULATE_TSC
#define ARTI_CPU_FREQ             ARTI_FREQ_8254
#define ARTI_CALIBRATED_CPU_FREQ  ARTI_FREQ_8254

#define arti_rdtsc() rd_8254_ts()

#endif /* CONFIG_X86_TSC */

struct calibration_data {

    unsigned long cpu_freq;
    unsigned long apic_freq;
    int latency;
    int setup_time_TIMER_CPUNIT;
    int setup_time_TIMER_UNIT;
    int timers_tol[ARTI_NR_CPUS];
};

struct apic_timer_setup_data {

    int mode;
    int count;
};

extern struct rt_times rt_times;

extern struct rt_times rt_smp_times[ARTI_NR_CPUS];

extern struct calibration_data arti_tunables;

extern volatile unsigned long arti_status;

extern volatile unsigned long arti_cpu_realtime;

extern volatile unsigned long arti_cpu_lock;

extern volatile unsigned long arti_cpu_lxrt;

extern struct arti_switch_data {
    struct task_struct *oldtask;
    unsigned long oldflags;
} arti_linux_context[ARTI_NR_CPUS];

extern adomain_t arti_domain;

extern int arti_adeos_ptdbase;

/* arti_get_current() is Adeos-specific. Since real-time interrupt
   handlers are called on behalf of the RTAI domain stack, we cannot
   infere the "current" Linux task address using %esp. We must use the
   suspended Linux domain's stack pointer instead. */

static inline struct task_struct *arti_get_current (int cpuid)

{
    int *esp;

    __asm__ volatile("movl %%esp, %0" : "=r" (esp));

    if (esp >= arti_domain.estackbase[cpuid] && esp < arti_domain.estackbase[cpuid] + 2048)
	return (struct task_struct *)(((u_long)adp_root->esp[cpuid]) & (~8191UL));

    return get_current();
}

#define rt_spin_lock(lock)    spin_lock(lock)
#define rt_spin_unlock(lock)  spin_unlock(lock)

static inline void rt_spin_lock_irq(spinlock_t *lock) {

    arti_cli();
    rt_spin_lock(lock);
}

static inline void rt_spin_unlock_irq(spinlock_t *lock) {

    rt_spin_unlock(lock);
    arti_sti();
}

static inline unsigned long rt_spin_lock_irqsave(spinlock_t *lock) {

    unsigned long flags;
    arti_local_irq_save(flags);
    rt_spin_lock(lock);
    return flags;
}

static inline void rt_spin_unlock_irqrestore(unsigned long flags,
					     spinlock_t *lock) {
    rt_spin_unlock(lock);
    arti_local_irq_restore(flags);
}

#ifdef CONFIG_SMP

#define CPU_RELAX(x) \
do { \
   int i = 0; \
   do \
     cpu_relax(); \
   while (++i < x); \
} while(0)
#else /* !CONFIG_SMP */
#define CPU_RELAX(x)
#endif /* CONFIG_SMP */

void arti_broadcast_to_timers(int irq,
			      void *dev_id,
			      struct pt_regs *regs);

static inline void rt_get_global_lock(void) {

    adeos_declare_cpuid;

    arti_cli();

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (!test_and_set_bit(cpuid,&arti_cpu_lock))
	while (test_and_set_bit(31,&arti_cpu_lock))
	    CPU_RELAX(cpuid);
}

static inline void rt_release_global_lock(void) {

    adeos_declare_cpuid;

    arti_cli();

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (test_and_clear_bit(cpuid,&arti_cpu_lock)) {
	clear_bit(31,&arti_cpu_lock);
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
    arti_sti();
}

/**
 * Save CPU flags
 *
 * rt_global_save_flags_and_cli combines rt_global_save_flags() and
 * rt_global_cli().
 */
static inline int rt_global_save_flags_and_cli(void) {

    unsigned long flags = arti_get_iflag_and_cli();
    adeos_declare_cpuid;

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (!test_and_set_bit(cpuid,&arti_cpu_lock))
	{
	while (test_and_set_bit(31,&arti_cpu_lock))
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

    unsigned long hflags = arti_get_iflag_and_cli(), rflags;
    adeos_declare_cpuid;

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (test_bit(cpuid,&arti_cpu_lock))
	rflags = hflags;
    else
	rflags = hflags | 1;

    if (hflags)
	arti_sti();

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
	case (1 << ARTI_IFLAG) | 1:

	    rt_release_global_lock();
	    arti_sti();
	    break;

	case (1 << ARTI_IFLAG) | 0:

	    rt_get_global_lock();
	    arti_sti();
	    break;

	case (0 << ARTI_IFLAG) | 1:

	    rt_release_global_lock();
	    break;

	case (0 << ARTI_IFLAG) | 0:

	    rt_get_global_lock();
	    break;
	}
}

static inline void rt_switch_to_real_time(int cpuid) {

    TRACE_RTAI_SWITCHTO_RT(cpuid);
    arti_linux_context[cpuid].oldtask = arti_get_current(cpuid);
    arti_linux_context[cpuid].oldflags = arti_linux_save_flags_and_cli();
    set_bit(cpuid,&arti_cpu_realtime);
}

static inline void rt_switch_to_linux(int cpuid) {

    TRACE_RTAI_SWITCHTO_LINUX(cpuid);
    clear_bit(cpuid,&arti_cpu_realtime);
    arti_linux_restore_flags_nosync(arti_linux_context[cpuid].oldflags,cpuid);
    arti_linux_context[cpuid].oldtask = NULL;
}

static inline struct task_struct *rt_whoislinux(int cpuid) {

    return arti_linux_context[cpuid].oldtask;
}

static inline int rt_is_linux (void) {

    return !test_bit(adeos_processor_id(),&arti_cpu_realtime);
}

static inline int rt_is_lxrt (void) {

    return test_bit(adeos_processor_id(),&arti_cpu_lxrt);
}

static inline void rt_set_timer_delay (int delay) {

    if (delay) {
        unsigned long flags;
        arti_hw_lock(flags);
#ifdef __USE_APIC__
	apic_read(APIC_TMICT);
	apic_write(APIC_TMICT,delay);
#else /* !__USE_APIC__ */
	outb(delay & 0xff,0x40);
	outb(delay >> 8,0x40);
#endif /* __USE_APIC__ */
        arti_hw_unlock(flags);
    }
}

    /* Private interface -- Internal use only */

void arti_attach_lxrt(void);

void arti_detach_lxrt(void);

void arti_switch_linux_mm(struct task_struct *prev,
			  struct task_struct *next,
			  int cpuid);

int arti_calibrate_8254(void);

#endif /* __KERNEL__ && !__cplusplus */

    /* Public interface */

#ifdef __KERNEL__

#include <linux/kernel.h>

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
			 void (*handler)(int irq,
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

#define rt_printk printk /* This is safe over Adeos */

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
