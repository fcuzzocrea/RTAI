/*
Copyright (C) 2002,2003 Axis Communications AB

Authors: Martin P Andersson (martin.andersson@linux.nu)
         Jens-Henrik Lindskov (mumrick@linux.nu)

This program is free software; you can redistribute it and/or modify
it under the terms of version 2 of the GNU General Public License as
published by the Free Software Foundation.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

--------------------------------------------------------------------------
Acknowledgements
- Paolo Mantegazza	(mantegazza@aero.polimi.it)
	creator of RTAI 
--------------------------------------------------------------------------
*/

#ifndef _RTAI_ASM_CRIS_RTAI_H_
#define _RTAI_ASM_CRIS_RTAI_H_

#include <rtai_types.h>

/* Just to make things compile */

#define smp_num_cpus 1
#define RTAI_1_IPI  -1
#define RTAI_2_IPI  -1
#define RTAI_3_IPI  -1
#define RTAI_4_IPI  -1

/* '8254' is used to maintain compatibility with generic code */

#define NR_RT_CPUS     1
#define hard_cpu_id()  0
#define TIMER_8254_IRQ 2 /* Timer irq == 2 on ETRAX */
#define RT_TIME_END 0x7fffffffffffffffLL

/*
 * unsigned long long multiplication
 */
static inline unsigned long long ullmul(unsigned long m0, unsigned long m1)
{
        unsigned long long res;

        res = (unsigned long long)m0 * (unsigned long long)m1;

        return res;
}

/*
 * unsigned long long division
 */
static inline unsigned long long ulldiv(unsigned long long ull, 
					unsigned long uld, unsigned long *r)
{
	unsigned long long q = ull/(unsigned long long) uld;

	*r = (unsigned long) (ull - q * (unsigned long long) uld);

	return q;	
}

/*
 * int multiplication and division
 */
static inline int imuldiv(unsigned long i, unsigned long mult, 
			  unsigned long div)
{
	unsigned long q , r;
	unsigned long long m;

	if ( mult == div )
		return i;

	m = ((unsigned long long) i * (unsigned long long) mult);
	q = (unsigned long) (m / (unsigned long long) div);
	r = (unsigned long) (m - (unsigned long long) q * 
			     (unsigned long long) div );

	return (r + r) < div ? q : q + 1;
}

/**
 * unsigned long long multiplication and division
 */
static inline unsigned long long llimd(unsigned long long ull, 
				       unsigned long mult, unsigned long div)
{
	unsigned long long low, high, q;
	unsigned long r;

	low  = ullmul(((unsigned long *)&ull)[0], mult);
	high = ullmul(((unsigned long *)&ull)[1], mult);
	q = ulldiv(high,div,&r) << 32;
	high = ((unsigned long long) r) << 32;
	q += ulldiv( high + low, div , &r);
	return (r + r) < div ? q : q + 1;
}

#ifdef __KERNEL__

#ifndef __cplusplus
#include <asm/irq.h>
#include <asm/timex.h>
#include <linux/kernel.h>
#include <asm/rtai_atomic.h>
#include <linux/spinlock.h>
#endif /* !__cplusplus */

extern unsigned long r_timer_ctrl_shadow;

/* Interrupt flag is bit 5 on CRIS */
#define IFLAG 5
#define IMASK (1 << IFLAG)

/* Both are needed by the generic code */
#define hard_save_flags_and_cli(x) hard_save_flags_cli((x))

#define rt_spin_lock(lock)
#define rt_spin_unlock(lock)

static inline unsigned long hard_lock_all(void)
{
	unsigned long flags;
	hard_save_flags_cli(flags);
	return flags;
}

#define hard_unlock_all(flags) hard_restore_flags((flags))

static inline unsigned int rt_spin_lock_irqsave(spinlock_t *lock)          
{
	unsigned long flags;
	hard_save_flags_cli(flags);
	return flags;
}

static inline void rt_spin_unlock_irqrestore(unsigned long flags,
					     spinlock_t *lock)
{
	hard_restore_flags(flags);
}

static inline void rt_spin_lock_irq(volatile spinlock_t *lock)          
{
	hard_cli(); 
	rt_spin_lock(lock);
}

static inline void rt_spin_unlock_irq(volatile spinlock_t *lock)
{
	rt_spin_unlock(lock);
	hard_sti();
}

static inline void rt_global_cli(void)
{
	hard_cli();
}

static inline void rt_global_sti(void)
{
	hard_sti();
}

/*
 * Save whether the IMASK was set or not in flags
 */
static inline void rt_global_save_flags(unsigned long *flags)
{
	unsigned long hflags;

	hard_save_flags(hflags);

	hflags = hflags & IMASK; /* The interrupts were enabled? */
	
	*flags = hflags;
}

/*
 * Do cli and return whether the IMASK was set or not before
 */
static inline int rt_global_save_flags_and_cli(void)
{
	unsigned long flags;

	hard_save_flags_cli(flags);

	return (flags & IMASK);
}

/*
 * If interrupts are enabled in flags then enable them again
 */
static inline void rt_global_restore_flags(unsigned long flags)
{
	if (flags & IMASK){
		hard_sti();
	}
	else{
		hard_cli();
	}
}

static __inline__ void set_bit_non_atomic(int nr, int* addr) 
{
	*addr |= (1 << nr);
}

static __inline__ void set_bit_atomic(int nr, int* addr) 
{
	unsigned long flags;
	hard_save_flags_cli(flags);
	*addr |= (1 << nr);
	hard_restore_flags(flags);
}

static __inline__ void clear_bit_non_atomic(int nr, int* addr)
{
	*addr &= ~(1 << nr);
}

static __inline__ void clear_bit_atomic(int nr, int* addr)
{
	unsigned long flags;
	hard_save_flags_cli(flags);
	*addr &= ~(1 << nr);
	hard_restore_flags(flags);
}

/* 
--------------------------------------------------------------------------
*/

extern unsigned int rtai_delay;     /* Current timer divide factor on timer0 */
extern unsigned long long rtai_tsc; /* timer0 ticks since we started counting*/
extern unsigned int rtai_lastcount; /* Last read value on *R_TIMER0_DATA */

/*
 * ETRAX 100LX lacks a cycle counter.
 * This does _NOT_ work if there is a process blocking the interrupts during 
 * more than one wrap of the timer.
 * This is a very short time and a potential source of problems.
 * ETRAX 100LX needs a bigger timer counter in hardware.
 */
static inline unsigned long long rdtsc(void)
{
	RTIME ts;
	unsigned long flags;
	unsigned int count, ticks;

#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
	/* Read the 8 bit count value. */
	count = *R_TIMER0_DATA;
#else
	/* Read the 16 bit count value. */
	count = *R_TIMER01_DATA;
#endif
	
	flags = hard_lock_all();

	/* No timer wrap, just a count down.
	 * Read the timer interrupt to make sure, because the interrupts 
	 * could be disabled at this moment.
	 * If the interrupts are disabled for to long this will fail. */
	if ( count < rtai_lastcount && 
	     !(*R_VECT_READ & IO_STATE(R_VECT_READ, timer0, active)) ){
		ticks = rtai_lastcount - count;
	}
	/* A timer wrap */
	else {
		ticks = rtai_delay - rtai_lastcount + count;
	}
	rtai_lastcount = count;
	rtai_tsc += (unsigned long long) ticks;

	ts = rtai_tsc;
	hard_unlock_all(flags);

	return ts;
}

/*
 * Set the timer divide factor to delay if x=!0
 * On cris we have a timer counter which wraps around by
 * itself so there is nothing to do when delay==0.
 * Some archs have to reprogram the timer every period,
 * therefore this function can be called with 0
 * We use both timers cascaded in this 
 */
static inline void rt_set_timer_delay(unsigned int delay)
{
	if (delay<=0)
		return;

#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
	/* 8 bit counter */
	if (delay>255)
		delay = 0;
#else
	/* 16 bit counter */
	if (delay>65535)
		delay = 0;
#endif
		
	/* So that no ticks are lost when the timer is reprogrammed. */
	rdtsc();

#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
	/* Clear the corresponding fields of the shadow. */
	r_timer_ctrl_shadow = r_timer_ctrl_shadow & (
		~IO_FIELD(R_TIMER_CTRL, timerdiv0, 255) &
		~IO_STATE(R_TIMER_CTRL, tm0, reserved));

	/* Stop the timers and load the new timerdiv0 and timerdiv1 */
	*R_TIMER_CTRL = r_timer_ctrl_shadow              | 
		IO_FIELD(R_TIMER_CTRL, timerdiv0, delay) | 
		IO_STATE(R_TIMER_CTRL, tm0, stop_ld);

	/* Restart the timer and save the changes to r_timer_ctrl_shadow */
	*R_TIMER_CTRL = r_timer_ctrl_shadow = r_timer_ctrl_shadow | 
		IO_FIELD(R_TIMER_CTRL, timerdiv0, delay)          | 
		IO_STATE(R_TIMER_CTRL, tm0, run);
#else
	/* Clear the corresponding fields of the shadow. */
	r_timer_ctrl_shadow = r_timer_ctrl_shadow & (
		~IO_FIELD(R_TIMER_CTRL, timerdiv1, 255) &
		~IO_STATE(R_TIMER_CTRL, tm1, reserved)  &
		~IO_FIELD(R_TIMER_CTRL, timerdiv0, 255) &
		~IO_STATE(R_TIMER_CTRL, tm0, reserved));

	/* Stop the timers and load the new timerdiv0 and timerdiv1 */
	*R_TIMER_CTRL = r_timer_ctrl_shadow                     | 
		IO_FIELD(R_TIMER_CTRL, timerdiv1, delay>>8)     | 
		IO_STATE(R_TIMER_CTRL, tm1, stop_ld)            |
		IO_FIELD(R_TIMER_CTRL, timerdiv0, 0xff & delay) | 
		IO_STATE(R_TIMER_CTRL, tm0, stop_ld);

	/* Restart the timer and save the changes to r_timer_ctrl_shadow */
	*R_TIMER_CTRL = r_timer_ctrl_shadow = r_timer_ctrl_shadow | 
		IO_FIELD(R_TIMER_CTRL, timerdiv1, delay>>8)       | 
		IO_STATE(R_TIMER_CTRL, tm1, run)                  |
		IO_FIELD(R_TIMER_CTRL, timerdiv0, 0xff & delay)   | 
		IO_STATE(R_TIMER_CTRL, tm0, run);

#endif
		
	/* Update the delays for use in rdtsc. */
	rtai_delay = delay;
	rtai_lastcount = delay;
}

#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
#define FREQ_8254 25000 /* 25 MHz with a prescale of 1000 (kernel >=2.4.19) */
#else /* !CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI */
#define FREQ_8254 6250000 /* 25 MHz with a prescale of 4 */
#endif /* CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI */

#define SETUP_TIME_8254 2800
#define LATENCY_8254    2800
#define CPU_FREQ        FREQ_8254
#define FREQ_APIC       FREQ_8254

#define NR_GLOBAL_IRQS  NR_IRQS
#define NR_CPU_OWN_IRQS NR_GLOBAL_IRQS

/* Floating point related stuff. There are */
/* no floating point registers on CRIS */
#define save_cr0_and_clts(x)
#define restore_cr0(x)
#define enable_fpu(x)
#define save_fpenv(x)
#define restore_fpenv(x)

typedef struct cris_fpu_env { unsigned long fpu_regs[1]; } FPU_ENV;

/* Need to be declared but can be empty */
#define DECLR_8254_TSC_EMULATION
#define TICK_8254_TSC_EMULATION
#define SETUP_8254_TSC_EMULATION
#define CLEAR_8254_TSC_EMULATION

extern struct calibration_data tuned;
struct calibration_data {
	unsigned int cpu_freq;
	unsigned int apic_freq;
	int latency;
	int setup_time_TIMER_CPUNIT;
	int setup_time_TIMER_UNIT;
	int timers_tol[NR_RT_CPUS];
};

struct apic_timer_setup_data {
	int mode;
	int count;
};

/* FIX: Trap handling. This is not implemented in RTAI/CRIS yet. */
#define RTAI_NR_TRAPS 32
static inline RT_TRAP_HANDLER rt_set_rtai_trap_handler(RT_TRAP_HANDLER handler)
{
        return (RT_TRAP_HANDLER) 0;
}

extern struct rt_times rt_times;

#define ffnz(ul) (ffs(ul)-1)

/* 
--------------------------------------------------------------------------
*/

#define rt_assign_irq_to_cpu(irq, cpu)
#define rt_reset_irq_to_sym_mode(irq)

extern int rt_request_global_irq(unsigned int irq, void (*handler)(void));
extern int rt_free_global_irq(unsigned int irq);
extern void rt_ack_irq(unsigned int irq);
extern void rt_mask_and_ack_irq(unsigned int irq);
extern void rt_unmask_irq(unsigned int irq);
extern unsigned int rt_startup_irq(unsigned int irq);
extern void rt_shutdown_irq(unsigned int irq);
extern void rt_enable_irq(unsigned int irq);
extern void rt_disable_irq(unsigned int irq);
extern int rt_request_linux_irq(unsigned int irq,
       void (*linux_handler)(int irq, void *dev_id, struct pt_regs *regs),
       char *linux_handler_id, void *dev_id);
extern int rt_free_linux_irq(unsigned int irq, void *dev_id);
extern int rt_request_srq(unsigned int label, void (*rtai_handler)(void), 
			  long long (*user_handler)(unsigned long whatever));
extern int rt_free_srq(unsigned int srq);
extern void rt_pend_linux_irq(unsigned int irq);
extern void rt_pend_linux_srq(unsigned int srq);
#define rt_request_cpu_own_irq(irq, handler) rt_request_global_irq((irq), (handler))
#define rt_free_cpu_own_irq(irq) rt_free_global_irq((irq))
extern void rt_request_timer(void (*handler)(void), unsigned int tick, 
			     int apic);
extern void rt_free_timer(void);
extern void rt_mount_rtai(void);
extern void rt_umount_rtai(void);
extern int rt_printk(const char *format, ...);
extern int rtai_print_to_screen(const char *format, ...);
extern void rt_switch_to_linux(int cpuid);
extern void rt_switch_to_real_time(int cpuid);
extern int rt_is_linux(void);

#endif /* __KERNEL__ */

#define RTAI_DEFAULT_TICK    200000
#define RTAI_DEFAULT_STACKSZ 1000

#endif /* !_RTAI_ASM_CRIS_RTAI_H_ */
