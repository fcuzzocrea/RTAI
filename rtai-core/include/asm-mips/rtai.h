/*
 * COPYRIGHT (C) 2001  Steve Papacharalambous (stevep@lineo.com)
 * COPYRIGHT (C) 2000  Paolo Mantegazza (mantegazza@aero.polimi.it)
 *  
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *   
 *   This library is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   Lesser General Public License for more details.
 *    
 *    You should have received a copy of the GNU Lesser General Public
 *    License along with this library; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 *
 */

/*
 * Modifications for MIPS port - Steve Papacharalambous (stevep@lineo.com)
 * 19Jun01
 */

/*
 * More MIPS stuff by Steven Seeger (sseeger@stellartec.com)
 * 
 */

#ifndef _RTAI_ASM_MIPS_RTAI_H_
#define _RTAI_ASM_MIPS_RTAI_H_

#include <rtai_types.h>

static inline unsigned long long ullmul(unsigned long m0, unsigned long m1)
{

	unsigned long long __res;

	__asm__ __volatile__ (
			"multu\t%2,%3\n\t"
			"mflo\t%0\n\t"
			"mfhi\t%1\n\t"
			: "=r" (((unsigned long *)&__res)[0]),
			  "=r" (((unsigned long *)&__res)[1])
			: "r" (m0), "r" (m1));

	return(__res);

} /* End function - ullmul */

// One of the silly thing of 32 bits MIPS, no 64 by 32 bits divide.
static inline unsigned long long ulldiv(unsigned long long ull,
				unsigned long uld, unsigned long *r)
{
	unsigned long long q, rf;
	unsigned long qh, rh, ql, qf;

	q = 0;
	rf = (unsigned long long)(0xFFFFFFFF - (qf = 0xFFFFFFFF / uld) * uld)
									+ 1ULL;

	while (ull >= uld) {
		((unsigned long *)&q)[1] +=
			(qh = ((unsigned long *)&ull)[1] / uld);

		rh = ((unsigned long *)&ull)[1] - qh * uld;
		q += rh * (unsigned long long)qf +
			(ql = ((unsigned long *)&ull)[0] / uld);

		ull = rh * rf + (((unsigned long *)&ull)[0] - ql * uld);
	}

	*r = ull;
	return(q);
}  /* End function - ulldiv */

static inline int imuldiv(int i, int mult, int div)
{
	unsigned long q, r;

	q = ulldiv(ullmul(i, mult), div, &r);

	return (r + r) > div ? q + 1 : q;
}  /* End function - imuldiv */

static inline unsigned long long llimd(unsigned long long ull,
				unsigned long mult, unsigned long div)
{
	unsigned long long low;
	unsigned long q, r;

	low  = ullmul(((unsigned long *)&ull)[0], mult);	
	q = ulldiv( ullmul(((unsigned long *)&ull)[1], mult) +
		((unsigned long *)&low)[1], div, (unsigned long *)&low);
	low = ulldiv(low, div, &r);
	((unsigned long *)&low)[1] += q;

	return (r + r) > div ? low + 1 : low;
}  /* End function - llimd */


#ifdef __KERNEL__

#ifndef __cplusplus
#include <linux/smp.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <asm/system.h>
#include <asm/bitops.h>
#include <asm/mipsregs.h>
#include <asm/rtai_atomic.h>
#endif /* !__cplusplus */

/*
 * Pending IRQ bit definitions.
 */
#define IRQ_s334 3
#define IRQ_EIC 5
#define IRQ_TIMER 7

/*
 * This needs to be fixed for the specific arch
 */
#define RTAI_NR_TRAPS 16

/*
 * CPU frequency calibration.
 */
#define CPU_FREQ (tuned.cpu_freq)
#define FREQ_DECR CPU_FREQ
#define CALIBRATED_CPU_FREQ     0 /* Use this if you know better than Linux! */

/*
 * Do not be messed up by macros names below, is a trick for keeping i386 code.
 */
#define FREQ_8254 CPU_FREQ
#define FREQ_APIC CPU_FREQ
#define LATENCY_8254 3000
#define SETUP_TIME_8254 500
#define TIMER_8254_IRQ IRQ_TIMER

#define IFLAG 9

#ifndef CLOCK_TICK_RATE
#define CLOCK_TICK_RATE 1193180	/* Ok, this makes no sense on MIPS */
#endif /* CLOCK_TICK_RATE */

/*
 * IE Bit position for MIPS is cp0 status register bit 0
 */
#define IFLAG_POS 0

#define NR_RT_CPUS 1

#define rt_write_timer_count(cnt)  do { \
		write_c0_count(cnt); } while(0)

#define rt_read_timer_countg() read_c0_count();

#define rt_write_timer_comp(cnt)  do { \
		write_c0_compare(cnt); } while(0)

#define rt_read_timer_comp()      do { \
		read_c0_compare(); } while(0)

#define rt_set_timer_incr(x)	do { \
		write_c0_compare((x)); } while(0)

#define hard_cpu_id() hard_smp_processor_id()

#define rt_spin_lock(lock)
#define rt_spin_unlock(lock)

#define rt_get_global_lock() hard_cli()
#define rt_release_global_lock()

#define save_fpenv(x)
#define restore_fpenv(x)

#define RT_TIME_END 0x7fffffffffffffffLL

struct apic_timer_setup_data;

struct global_rt_status {
	volatile unsigned int pending_irqs_l;
        volatile unsigned int pending_irqs_h;
	volatile unsigned int activ_irqs;
	volatile unsigned int pending_srqs;
	volatile unsigned int activ_srqs;
	volatile unsigned int cpu_in_sti;
	volatile unsigned int used_by_linux;
	volatile unsigned int locked_cpus;
	volatile unsigned int hard_nesting;
	volatile unsigned int hard_lock_all_service;
#ifdef CONFIG_X86_REMOTE_DEBUG
	volatile unsigned int used_by_gdbstub;
#endif
	spinlock_t hard_lock;
	spinlock_t data_lock;
};

/* grrr - this should have a arch-independent name */
struct apic_timer_setup_data {
	int mode;
	int count;
};



struct calibration_data {
	unsigned int cpu_freq;
	unsigned int apic_freq;
	int latency;
	int setup_time_TIMER_CPUNIT;
	int setup_time_TIMER_UNIT;
	int timers_tol[NR_RT_CPUS];
};


typedef struct mips_fpu_env { unsigned long fpu_reg[32]; } FPU_ENV;

/* unknown stuff */
#define save_cr0_and_clts(x)
#define restore_cr0(x)
#define enable_fpu()
#define DECLR_8254_TSC_EMULATION
#define TICK_8254_TSC_EMULATION
#define SETUP_8254_TSC_EMULATION
#define CLEAR_8254_TSC_EMULATION


extern unsigned volatile int *locked_cpus;

static inline void rt_spin_lock_irq(spinlock_t *lock)
{
	hard_cli();
	rt_spin_lock(lock);
}  /* End function - rt_spin_lock_irq */

static inline void rt_spin_unlock_irq(spinlock_t *lock)
{
	rt_spin_unlock(lock);
	hard_sti();
}  /* End function - rt_spin_unlock_irq */


/*
 * Note that the spinlock calling convention below for irqsave/restore is
 * slightly different from the one used in Linux. Done on purpose to get an
 * error if you use Linux spinlocks in real time applications as they do not
 * guarantee any protection because of the soft irq disable. Be careful and
 * sure to call the other spinlocks the right way, as they are compatible
 * with Linux.
 */

static inline unsigned int rt_spin_lock_irqsave(spinlock_t *lock)
{
	unsigned long flags;
	hard_save_flags_and_cli(flags);
	rt_spin_lock(lock);
	return flags;
} /* End function - rt_spin_lock_irqsave */

static inline void rt_spin_unlock_irqrestore(unsigned long flags,
							spinlock_t *lock)
{
	rt_spin_unlock(lock);
	hard_restore_flags(flags);
} /* End function - rt_spin_unlock_irqrestore */


/*
 * Global interrupts and flags control (simplified, and modified, version of
 * similar global stuff in Linux irq.c).
 */
 
static inline void rt_global_cli(void)
{
	rt_get_global_lock();
} /* End function - rt_global_cli */
 
static inline void rt_global_sti(void)
{
	rt_release_global_lock();
	hard_sti();
} /* End function - rt_global_sti */

 
static inline int rt_global_save_flags_and_cli(void)
{
	unsigned long flags;

	hard_save_flags_and_cli(flags);
	if (!test_and_set_bit(hard_cpu_id(), locked_cpus)) {
		while (test_and_set_bit(31, locked_cpus));
		return ((flags & (1 << IFLAG)) + 1);
	} else {
		return (flags & (1 << IFLAG));
	}
} /* End function - rt_global_save_flags_and_cli */


static inline void rt_global_save_flags(unsigned long *flags)
{
	unsigned long hflags, rflags;

	hard_save_flags_and_cli(hflags);
	hflags = hflags & (1 << IFLAG);
	rflags = hflags | !test_bit(hard_cpu_id(), locked_cpus);
	if (hflags) {
		hard_sti();
	}
	*flags = rflags;
} /* End function - rt_global_save_flags */


static inline void rt_global_restore_flags(unsigned long flags)
{
	switch (flags) {
	case (1 << IFLAG) | 1:  rt_release_global_lock();
		hard_sti();
		break;
	case (1 << IFLAG) | 0:  rt_get_global_lock();
		hard_sti();
		break;
	case (0 << IFLAG) | 1:  rt_release_global_lock();
		break;
	case (0 << IFLAG) | 0:  rt_get_global_lock();
		break;
	}
} /* End function - rt_global_restore_flags */


extern struct rt_times rt_times;
extern struct rt_times rt_smp_times[NR_RT_CPUS];
extern struct calibration_data tuned;


/*
 * This has been copied from the Linux kernel source for ffz and modified
 * to return the position of the first non zero bit. - Stevep
 *
 * ffnz = Find First Non Zero in word. Undefined if no one exists,
 * so code should check against ~0xffffffffUL first..
 *
 */
extern __inline__ unsigned long ffnz(unsigned long word)
{
	unsigned int    __res;
	unsigned int    __mask = 1;

	__asm__ (
		".set\tnoreorder\n\t"
		".set\tnoat\n\t"
		"move\t%0,$0\n"
		"1:\tand\t$1,%2,%1\n\t"
		"bnez\t$1,2f\n\t"
		"sll\t%1,1\n\t"
		"bnez\t%1,1b\n\t"
		"addiu\t%0,1\n\t"
		".set\tat\n\t"
		".set\treorder\n"
		"2:\n\t"
		: "=&r" (__res), "=r" (__mask)
		: "r" (word), "1" (__mask)
		: "$1");

	return __res;
} /* End function - ffnz */


static inline unsigned long long rdtsc(void)
{
	extern struct rt_hal rthal;
	unsigned long count;
	long flags;

	count = read_c0_count();
	hard_save_flags_and_cli(flags);
	rthal.tsc.hltsc[1] += (count < rthal.tsc.hltsc[0]);
	rthal.tsc.hltsc[0] = count;
	hard_restore_flags(flags);
	return rthal.tsc.tsc;

}  /* End function - rdtsc */

/*
 * Temporary section as include asm-generic/rtai.h causes building problems.
 * Stevep - 7Jan02
 */

int rt_request_global_irq(unsigned int irq, void (*handler)(void));
int rt_free_global_irq(unsigned int irq);
void rt_ack_irq(unsigned int irq);
void rt_mask_and_ack_irq(unsigned int irq);
void rt_unmask_irq(unsigned int irq);
unsigned int rt_startup_irq(unsigned int irq);
void rt_shutdown_irq(unsigned int irq);
void rt_enable_irq(unsigned int irq);
void rt_disable_irq(unsigned int irq);
int rt_request_linux_irq(unsigned int irq,
	void (*linux_handler)(int irq, void *dev_id, struct pt_regs *regs), 
	char *linux_handler_id, void *dev_id);
int rt_free_linux_irq(unsigned int irq, void *dev_id);
void rt_pend_linux_irq(unsigned int irq);
int rt_request_srq(unsigned int label, void (*rtai_handler)(void),
	long long (*user_handler)(unsigned int whatever));
int rt_free_srq(unsigned int srq);
void rt_pend_linux_srq(unsigned int srq);
int rt_request_cpu_own_irq(unsigned int irq, void (*handler)(void));
int rt_free_cpu_own_irq(unsigned int irq);
int rt_request_timer(void (*handler)(void), unsigned int tick, int apic);
int rt_free_timer(void);
void rt_request_apic_timers(void (*handler)(void),
		        struct apic_timer_setup_data *apic_timer_data);
void rt_free_apic_timers(void);
void rt_mount_rtai(void);
void rt_umount_rtai(void);
int rt_printk(const char *format, ...);
int rtai_print_to_screen(const char *format, ...);
extern void rt_switch_to_linux(int cpuid);
extern void rt_switch_to_real_time(int cpuid);

#define rt_assign_irq_to_cpu(irq, cpu)
#define rt_reset_irq_to_sym_mode(irq)

/*
 * NOTE: delay MUST be 0 if a periodic timer is being used.
 *       This must go below the inclusion of the generic rtai.h header
 *       file as it need the prototye for rt_enable_irq.
 */
static inline void rt_set_timer_delay(int delay)
{
	unsigned long flags;

	hard_save_flags_and_cli(flags);
	write_c0_compare(read_c0_count() +
			(delay ? delay : rt_times.periodic_tick));
	hard_restore_flags(flags);
	rt_enable_irq(TIMER_8254_IRQ);

}  /* End function - rt_set_timer_delay */

#endif /* __KERNEL__ */

#define RTAI_DEFAULT_TICK    200000
#define RTAI_DEFAULT_STACKSZ 1000

#endif // _RTAI_ASM_MIPS_RTAI_H_
