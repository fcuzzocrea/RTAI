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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <asm/system.h>
#include <rtai_version.h>

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <rtai_proc_fs.h>
#endif

#define NR_SYSRQS 32
#define TIMER_IRQ 2   

/* Prescaler=4 gives 6.25 MHz. This means a value of 62500 needs
   to be loaded in the cascaded counter to give an interrupt every
   10 ms. 62500dec=F424hex. F4hex=244dec, 24hex=36dec. */
#define RTAI_CASCADED_TIMER0_DIV 36
#define RTAI_CASCADED_TIMER1_DIV 244
#define RTAI_CASCADED_TIMER_PRESCALE 4

#include <asm/rtai.h>

/* 
--------------------------------------------------------------------------
*/

static struct global_rt_status {
	int pending_irqs;
	int pending_srqs; 
	unsigned int used_by_linux;
} volatile global;

static struct cpu_own_status {
	unsigned long intr_flag;
	unsigned long linux_intr_flag;
} volatile cpu;

static void* global_irq_handlers[NR_IRQS];

static struct sysrq_t {
	unsigned int label;
	void (*rtai_handler)(void);
	long long (*user_handler)(unsigned long whatever);
} sysrq[NR_SYSRQS];

static unsigned long irq_action_flags[NR_IRQS];
static int chained_to_linux[NR_IRQS];

static int rtai_mounted = 0;


/* The datastructures saved when taking control from Linux */

static struct rt_hal linux_rthal;               /* The original rthal-struct.*/
#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
static unsigned long saved_r_timer_ctrl_shadow; /* Saved timer shadow.*/
#endif
static void *saved_timer_action_handler;        /* Saved timer-action handler*/

/* 
--------------------------------------------------------------------------
*/
/* Scheduler related data */

unsigned int rtai_delay;     /* Current timer divide factor on timer0 */
unsigned long long rtai_tsc; /* timer0 ticks since we started counting */
unsigned int rtai_lastcount; /* Last read value on *R_TIMER0_DATA */

struct calibration_data tuned = {CPU_FREQ, FREQ_APIC, 0, 0, 0, {0}};

struct rt_times rt_times;

/* 
--------------------------------------------------------------------------
*/

/* Some prototypes */
long long dispatch_srq(int srq, unsigned long whatever);
#ifdef CONFIG_PROC_FS
static int rtai_proc_register(void);
static void rtai_proc_unregister(void);
#endif


/* Just some dummies to get rid of compiler warnings */
static long long do_nothing(int what, unsigned long ever) { return 0;}
static void do_nothing_uint(unsigned int whatever) { }

#ifdef CONFIG_ETRAX_WATCHDOG
extern void reset_watchdog(void);
#endif

/* 
--------------------------------------------------------------------------
*/

static inline void mask_irq(unsigned int irq_nr)
{
	*R_VECT_MASK_CLR = 1 << irq_nr;
}

static inline void unmask_irq(unsigned int irq_nr)
{
	*R_VECT_MASK_SET = 1 << irq_nr;
}

/* 
--------------------------------------------------------------------------
*/

unsigned int rt_startup_irq(unsigned int irq)
{
       	startup_irq(irq);
	return 0;
}

void rt_shutdown_irq(unsigned int irq)
{
	shutdown_irq(irq);
}

void rt_enable_irq(unsigned int irq)
{
	unmask_irq(irq);
}

void rt_disable_irq(unsigned int irq)
{
	mask_irq(irq);
}

/*
 * The ACK is not implemented on cris because no generic way to
 * do this exists. It is up to the RT-handler to ACK properly.
 */
void rt_mask_and_ack_irq(unsigned int irq)
{
	mask_irq(irq);
}

void rt_mask_irq(unsigned int irq)
{
	mask_irq(irq);
}

void rt_unmask_irq(unsigned int irq)
{
	unmask_irq(irq);
}

/*
 * The ACK is not implemented on cris because no generic way to
 * do this exists. It is up to the RT-handler to ACK properly.
 */
void rt_ack_irq(unsigned int irq)
{
}

/* 
--------------------------------------------------------------------------
*/

/* Only makes sense on smp */

#define send_ipi_shorthand(shorthand, irq)
#define send_ipi_logical(dest, irq)

/* 
--------------------------------------------------------------------------
*/

/*
 * Installs a RT-handler on the requested irq.
 */
int rt_request_global_irq(unsigned int irq, void (*handler)(void))
{
	unsigned long flags;

	if (irq >= NR_IRQS || !handler) {
		return -EINVAL;
	}

	flags = hard_lock_all();

	if (global_irq_handlers[irq]) {
		hard_unlock_all(flags);
		return -EBUSY;
	}

	global_irq_handlers[irq] = handler;
	
	hard_unlock_all(flags);

	return 0;
}

/*
 * Uninstalls the previously installed RT-handler on the requested irq.
 */
int rt_free_global_irq(unsigned int irq)
{
	unsigned long flags = hard_lock_all();

	if (irq >= NR_IRQS || !global_irq_handlers[irq]) {
		hard_unlock_all(flags);
		return -EINVAL;
	}

	global_irq_handlers[irq] = 0;

	hard_unlock_all(flags);

	return 0;
}

/*
 * Installs a new linux interrupt handler. Forces linux to share the
 * interrupt if necessary.
 */
int rt_request_linux_irq(unsigned int irq,
			 void (*linux_handler)(int irq, void *dev_id,
					       struct pt_regs *regs),
			 char *linux_handler_id, void *dev_id)
{
	unsigned long flags;

	if (irq >= NR_IRQS || !linux_handler) {
		return -EINVAL;
	}

	flags = hard_lock_all();
	/* Force linux to share the irq */
	/* Save the flags to restore them in rt_free_linux_irq */
	if (!chained_to_linux[irq]++) {
		if (irq_action[irq]) {
			irq_action_flags[irq] = irq_action[irq]->flags;
			irq_action[irq]->flags |= SA_SHIRQ;
		}
	}
	hard_unlock_all(flags);

	/* Call the standard linux function in irq.c */
	request_irq(irq, linux_handler, SA_SHIRQ, linux_handler_id, dev_id);

	return 0;
}

/*
 * Copied from irq.c with some small changes. Called from rt_free_linux_irq 
 * instead of free_irq in irq.c when there is a RT-handler installed on the 
 * same interrupt. Does not set the interrupt to the badhandler.
 */
static void linux_free_irq(unsigned int irq, void *dev_id)
{
	struct irqaction *action, **p;
	unsigned long flags;

	for (p = irq_action + irq; (action = *p) != NULL; p = &action->next) {
		if (action->dev_id != dev_id)
			continue;

		/* Found it - now free it */
		save_flags(flags);
		cli();
		*p = action->next;
		restore_flags(flags);

		kfree(action);
		return;
	}

}

/*
 * Uninstalls the previously installed linux handler on irq.
 */
int rt_free_linux_irq(unsigned int irq, void *dev_id)
{
	unsigned long flags;

	flags = hard_lock_all();
	if (irq >= NR_IRQS || !chained_to_linux[irq]) {
		hard_unlock_all(flags);
		return -EINVAL;
	}
	
	if (global_irq_handlers[irq]){
		hard_unlock_all(flags);
		/* Call our special function above. */
		linux_free_irq(irq, dev_id);
	}
	else{
		hard_unlock_all(flags);
		/* Call the standard linux function in irq.c */
		free_irq(irq, dev_id);
	}

	flags = hard_lock_all();
	if (!(--chained_to_linux[irq]) && irq_action[irq]) {
		irq_action[irq]->flags = irq_action_flags[irq];
	}
	hard_unlock_all(flags);

	return 0;
}


/*
 * Pend an interrupt to linux.
 * Called by a RT-handler if it wants to share the interrupt with linux.
 */
void rt_pend_linux_irq(unsigned int irq)
{
	set_bit_non_atomic(irq, (int*) &global.pending_irqs);
}

/*
 * The call mechanism for the user_handler is not yet implemented.
 */
int rt_request_srq(unsigned int label, void (*rtai_handler)(void), 
		   long long (*user_handler)(unsigned long whatever))
{
	unsigned long flags;
	int srq;
     	
	if (!rtai_handler) {
		return -EINVAL;
	}
	
	flags = hard_lock_all();

	/* srq 0 and 1 are reserved */
	for (srq = 2; srq < NR_SYSRQS; srq++) {
		if (!(sysrq[srq].rtai_handler)) {
			sysrq[srq].rtai_handler = rtai_handler;
			sysrq[srq].label = label;
			if (user_handler) {
				sysrq[srq].user_handler = user_handler;
				rthal.do_SRQ = dispatch_srq;
			}
			hard_unlock_all(flags);
			return srq;
		}
	}
	hard_unlock_all(flags);

	return -EBUSY;
}

int rt_free_srq(unsigned int srq)
{
	unsigned long flags;

	flags = hard_lock_all();
	
	if (srq < 2 || srq >= NR_SYSRQS || !sysrq[srq].rtai_handler) {
		hard_unlock_all(flags);
		return -EINVAL;
	}
	sysrq[srq].rtai_handler = 0;
	sysrq[srq].user_handler = 0;
	sysrq[srq].label = 0;
	
	for (srq = 2; srq < NR_SYSRQS; srq++) {
		if (sysrq[srq].user_handler) {
			hard_unlock_all(flags);
			return 0;
		}
	}
	rthal.do_SRQ = do_nothing;
		
	hard_unlock_all(flags);

	return 0;
}

void rt_pend_linux_srq(unsigned int srq)
{
	set_bit_non_atomic(srq, (int*) &global.pending_srqs);
}

/* 
--------------------------------------------------------------------------
*/

static void linux_cli(void)
{
	/* Soft-disable interrupts */
	cpu.intr_flag = 0;
}


static void linux_sti(void)
{
	static volatile int sti_lock = 0;
	static struct pt_regs dummy_regs;

	int irq;
	int irq_mask;

	/* Hard-disable interrupts */
	hard_cli(); 
	
	/* When one process is in sti, dispatching irqs, */
	/* another one should not be able to enter. */
	if (sti_lock) {
		cpu.intr_flag = 1;
		/* Need to enable interrupts since they were
		   enabled before calling this function. */
		hard_sti();
		return;
	}

	/* Lock */
	sti_lock = 1;

	/* Soft-disable interrupts */
	cpu.intr_flag = 0; 			

	irq = 0;
	irq_mask = 1;
		
	/* Process all pending IRQs and SRQs */
	while (global.pending_irqs || global.pending_srqs) {

		/* "irq" active? */
		if (irq_mask & global.pending_irqs) {

			/* Clear the irq */
			clear_bit_non_atomic(irq, (int*) &global.pending_irqs);

			/* Hard-enable interrupts (during handler) */
			hard_sti();
					
			/* Call standard Linux do_IRQ(). */
			linux_rthal.do_IRQ(irq, &dummy_regs); 

			/* Hard-disable interrupts */
			hard_cli();
			
			/* The interrupt had to remain masked until now
			 * because there is no generic ACK on cris.
			 * Interrupts with RT-handlers are unmasked
			 * immediately in dispatch_irq. */
			unmask_irq(irq); 

		}

		/* If a SRQ has been pended and there is a SRQ-handler */
		if ((irq_mask & global.pending_srqs) &&
		    sysrq[irq].rtai_handler) {

			/* Clear the srq */
			clear_bit_non_atomic(irq, (int*) &global.pending_srqs);

       			/* Hard-enable interrupts (during handler) */
			hard_sti();

			/* Call installed SRQ-handler */
			sysrq[irq].rtai_handler();

			/* Hard-disable interrupts */
			hard_cli();
		}

		/* The interrupts should be enabled here to reduce latency */
		hard_sti();
		irq_mask = irq_mask << 1;
		++irq;
		if (irq >= NR_IRQS) {
			irq = 0;
			irq_mask = 1;
		}
		hard_cli();

	}

	/* We are through, so release the lock */
	sti_lock = 0;
				
	/* Soft-enable interrupts */
	cpu.intr_flag = 1;

	/* Hard-enable interrupts */
	hard_sti();

}


static unsigned long linux_save_flags(void)
{	
	return cpu.intr_flag;
}

static void linux_restore_flags(unsigned long flags)
{
	/* check if interrupts were enabled when flag was saved */
	if (flags) {
		linux_sti();
	} else {
		cpu.intr_flag = 0;
	}
}

static unsigned long linux_save_flags_and_cli(void)
{
	unsigned long linux_flags;

	linux_flags = cpu.intr_flag;
    cpu.intr_flag = 0;
	return linux_flags;
}

unsigned long linux_save_flags_and_cli_cpuid(int cpuid)
{
	return linux_save_flags_and_cli();
}

void rtai_just_copy_back(unsigned long flags, int cpuid)
{
	cpu.intr_flag = flags;
}

/*
 *  hard_sti() is asm-defined as "ei" in system.h. This function
 *  is used at rthal.ei_if_rtai when RTAI has been mounted.
 */ 
static void do_hard_sti(void)
{
	hard_sti();
}


/*
 * Special fast version of rdtsc called from dispatch_timer_irq
 * with interrupts disabled.
 * When arriving here we _KNOW_ hat a timer interrupt has occured
 */
static inline void rdtsc_special(void)
{
#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
	unsigned int count = *R_TIMER0_DATA;	
#else
	unsigned int count = *R_TIMER01_DATA;
#endif
	
	/* We know there was a timer interrupt when this function was called */
	rtai_tsc += rtai_delay - rtai_lastcount + count;
	
	rtai_lastcount = count;
}


/*
 * This function is called as interrupt handler for most of the
 * interrupts when RTAI is mounted. See also dispatch_timer_irq.
 */
asmlinkage void dispatch_irq(int irq, struct pt_regs *regs)
{
	if (irq < 0 || irq >= NR_IRQS)
		return;
		
	/* Is there an RT-handler installed? */
	if (global_irq_handlers[irq]) { 

		/* The handler should:
		 * ACK the interrupt properly and immediately.
		 * Pend it to Linux if necessary. */
		((void (*)(int))global_irq_handlers[irq])(irq);

		/* Unmask ASAP since this is a RT interrupt. */
		unmask_irq(irq);
			
	} else {
		
		/* There is no RT-handler; pend the interrupt to Linux. */
		set_bit_non_atomic(irq, (int*) &global.pending_irqs);
			
	}

	/* Are interrupts enabled for Linux? */
	if (cpu.intr_flag) {
			
		/* Dispatch any pending interrupts to Linux. */
		linux_sti();

	}
} 


/*
 * This function is called as interrupt handler for the timer
 * interrupt when RTAI is mounted.
 */
asmlinkage void dispatch_timer_irq(int irq, struct pt_regs *regs)
{
	/* A minimum rdtsc (updates data used in scheduler) */
	rdtsc_special(); 

	/* ACK the timer interrupt */
	*R_TIMER_CTRL = r_timer_ctrl_shadow | IO_STATE(R_TIMER_CTRL, i0, clr);

	/* The timer irq is not masked and should not be so either. */

	/* The watchdog should preferably be disabled when running RTAI. */
#ifdef CONFIG_ETRAX_WATCHDOG
	reset_watchdog();
#endif

	/* Is there a RT-handler installed on the timer irq? */
	if (global_irq_handlers[TIMER_IRQ]) { 
				
		/* The handler pends the interrupt to Linux if necessary. */
		((void (*)(int))global_irq_handlers[TIMER_IRQ])(TIMER_IRQ);
	
	} else {
				
		/* There is no RT-handler so pend the interrupt to Linux */
		set_bit_non_atomic(TIMER_IRQ, (int*) &global.pending_irqs);

	}
	
	/* Are interrupts enabled for Linux? */
	if (cpu.intr_flag) {
		
		/* Dispatch any pending interrupts to Linux */
		linux_sti();

	}

	/* For speed (ei_if_rtai is done a little later anyway) */
	hard_sti();

} 

/* 
 * This function is a way to enter the kernel in our own way from user-space.
 *
 * FIX: This function is never called. A kernel patch has to be added in future
 * versions.  A file rtai_srq.h has to be added to the CRIS port before this
 * will work. Perhaps the break function could be used in a clever way.
 */
asmlinkage long long dispatch_srq(int srq, unsigned long whatever)
{
	if (srq > 1 && srq < NR_SYSRQS && sysrq[srq].user_handler) {
		return sysrq[srq].user_handler(whatever);
	}
	for (srq = 2; srq < NR_SYSRQS; srq++) {
		if (sysrq[srq].label == whatever) {
			return (long long) srq;
		}
	}
	return 0;
}

/* 
--------------------------------------------------------------------------
*/
/* Used by the scheduler */

int rt_is_linux(void)
{
	return global.used_by_linux;
}

void rt_switch_to_linux(int cpuid)
{
	global.used_by_linux = 1;
	cpu.intr_flag = cpu.linux_intr_flag;  /* Restore */

}

void rt_switch_to_real_time(int cpuid)
{
	if (global.used_by_linux)
		cpu.linux_intr_flag = cpu.intr_flag; /* Save */
	cpu.intr_flag = 0;
	global.used_by_linux = 0;
}

/* 
--------------------------------------------------------------------------
*/

/*
 * rt_request_timer set's the new timer latch value and 
 * free's + requests the global timer irq as a realtime interrupt.
 */
void rt_request_timer(void (*handler)(void), unsigned int tick, int unused)
{
	unsigned long flags;
	flags = hard_lock_all();
	
	/* Wait for a timer underflow and clear the int. bit.
	   Needs to wait since we pend the irq for Linux later.
	   Otherwise we could be 10ms off. */
	do {
		;
	} while ( !( *R_VECT_READ & IO_STATE(R_VECT_READ, timer0, active)) );
	*R_TIMER_CTRL = r_timer_ctrl_shadow | IO_STATE(R_TIMER_CTRL, i0, clr);

	/* set up rt_times structure */
	rt_times.linux_tick = LATCH;
	rt_times.periodic_tick = (tick > 0) ? tick : rt_times.linux_tick;
	rt_times.tick_time  = rdtsc();
	rt_times.intr_time  = rt_times.tick_time + rt_times.periodic_tick;
	rt_times.linux_time = rt_times.tick_time + (RTIME) rt_times.linux_tick;

	/* Set the new timer divide factor */
	rt_set_timer_delay(rt_times.periodic_tick);

	rt_free_global_irq(TIMER_IRQ);
	rt_request_global_irq(TIMER_IRQ, handler);
	
	/* pend linux timer irq to handle the current jiffie */
 	rt_pend_linux_irq(TIMER_IRQ);

	hard_unlock_all(flags);
}

void rt_free_timer(void){
	unsigned long flags;
	flags = hard_lock_all();

#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
	/* Clear the corresponding fields of the shadow. */
	r_timer_ctrl_shadow = r_timer_ctrl_shadow & (
		~IO_FIELD(R_TIMER_CTRL, timerdiv0, 255) &
		~IO_STATE(R_TIMER_CTRL, tm0, reserved));

        /* Stop the timer and load the original timerdiv0 */
        *R_TIMER_CTRL = r_timer_ctrl_shadow                   |
                IO_FIELD(R_TIMER_CTRL, timerdiv0, TIMER0_DIV) |
                IO_STATE(R_TIMER_CTRL, tm0, stop_ld);

        /* Restart the timer and save the changes to r_timer_ctrl_shadow */
        *R_TIMER_CTRL = r_timer_ctrl_shadow = r_timer_ctrl_shadow |
                IO_FIELD(R_TIMER_CTRL, timerdiv0, TIMER0_DIV)     |
                IO_STATE(R_TIMER_CTRL, tm0, run);
#else
	/* Configure the timers to operate in cascading mode. */
        *R_TIMER_CTRL =
                IO_FIELD( R_TIMER_CTRL, timerdiv1, RTAI_CASCADED_TIMER1_DIV) |
                IO_FIELD( R_TIMER_CTRL, timerdiv0, RTAI_CASCADED_TIMER0_DIV) |
                IO_STATE( R_TIMER_CTRL, i1, nop)           |
                IO_STATE( R_TIMER_CTRL, tm1, stop_ld)      |
                IO_STATE( R_TIMER_CTRL, clksel1, cascade0) |
                IO_STATE( R_TIMER_CTRL, i0, nop)           |
                IO_STATE( R_TIMER_CTRL, tm0, stop_ld)      |
                IO_STATE( R_TIMER_CTRL, clksel0, flexible);

        *R_TIMER_CTRL = r_timer_ctrl_shadow =
                IO_FIELD( R_TIMER_CTRL, timerdiv1, RTAI_CASCADED_TIMER1_DIV)  |
                IO_FIELD( R_TIMER_CTRL, timerdiv0, RTAI_CASCADED_TIMER0_DIV)  |
                IO_STATE( R_TIMER_CTRL, i1, nop)           |
                IO_STATE( R_TIMER_CTRL, tm1, run)          |
                IO_STATE( R_TIMER_CTRL, clksel1, cascade0) |
                IO_STATE( R_TIMER_CTRL, i0, nop)           |
                IO_STATE( R_TIMER_CTRL, tm0, run)          |
                IO_STATE( R_TIMER_CTRL, clksel0, flexible);

        *R_TIMER_PRESCALE = RTAI_CASCADED_TIMER_PRESCALE;
#endif
	rt_free_global_irq(TIMER_IRQ);
	hard_unlock_all(flags);
}


/* 
--------------------------------------------------------------------------
*/

extern void soft_timer_interrupt(int, void*, struct pt_regs*);

void __rt_mount_rtai(void)
{
	unsigned long flags;
	
    	flags = hard_lock_all();

	/* Save the old rthal struct */
	linux_rthal = rthal;
	
  	rthal.do_IRQ           	 = dispatch_irq;
	rthal.do_timer_IRQ       = dispatch_timer_irq;
  	rthal.do_SRQ           	 = do_nothing; /* Set later upon request */
	rthal.disint          	 = linux_cli;
	rthal.enint            	 = linux_sti;
  	rthal.getflags         	 = linux_save_flags;
  	rthal.setflags         	 = linux_restore_flags;
  	rthal.getflags_and_cli 	 = linux_save_flags_and_cli;
	rthal.ei_if_rtai         = do_hard_sti;
	rthal.unmask_if_not_rtai = do_nothing_uint;
	
	/* Save the timer handler */
	saved_timer_action_handler = irq_action[TIMER_IRQ]->handler;
	irq_action[TIMER_IRQ]->handler = soft_timer_interrupt;
	
    	hard_unlock_all(flags);
	
  	printk("\n***** RTAI MOUNTED *****\n\n");
}

extern void rt_printk_cleanup(void);
extern void rt_printk_init(void);


void __rt_umount_rtai(void)
{
  	unsigned long flags;

    	flags = hard_lock_all();

	/* Restore the old rthal struct */
	rthal = linux_rthal;

	/* Restore timer-interrupt handler */
	irq_action[TIMER_IRQ]->handler = saved_timer_action_handler;

	/* Remove SRQ for rt_printk etc. */
    	rt_printk_cleanup();

	hard_unlock_all(flags);
	
  	printk("\n***** RTAI UNMOUNTED *****\n\n");
}

static int init_rtai_cris(void)
{
	int i;
	
	global.pending_irqs = 0;

	cpu.intr_flag = 1;
	cpu.linux_intr_flag = 1;

	for (i = 0; i < NR_IRQS; i++) {
		global_irq_handlers[i] = 0;
		chained_to_linux[i] = 0;
	}
	
        for (i = 0; i < NR_SYSRQS; i++) {
		sysrq[i].rtai_handler = 0;
		sysrq[i].user_handler = 0;
		sysrq[i].label        = 0;
        }
	

	rt_printk_init();

#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
        /* Keep timers as they are but save the
	 * old timer control shadow. */
	saved_r_timer_ctrl_shadow = r_timer_ctrl_shadow;
#else
	/* Configure the timers to operate in cascading mode. */
        *R_TIMER_CTRL =
                IO_FIELD( R_TIMER_CTRL, timerdiv1, RTAI_CASCADED_TIMER1_DIV) |
                IO_FIELD( R_TIMER_CTRL, timerdiv0, RTAI_CASCADED_TIMER0_DIV) |
                IO_STATE( R_TIMER_CTRL, i1, nop)           |
                IO_STATE( R_TIMER_CTRL, tm1, stop_ld)      |
                IO_STATE( R_TIMER_CTRL, clksel1, cascade0) |
                IO_STATE( R_TIMER_CTRL, i0, nop)           |
                IO_STATE( R_TIMER_CTRL, tm0, stop_ld)      |
                IO_STATE( R_TIMER_CTRL, clksel0, flexible);

        *R_TIMER_CTRL = r_timer_ctrl_shadow =
                IO_FIELD( R_TIMER_CTRL, timerdiv1, RTAI_CASCADED_TIMER1_DIV) |
                IO_FIELD( R_TIMER_CTRL, timerdiv0, RTAI_CASCADED_TIMER0_DIV) |
                IO_STATE( R_TIMER_CTRL, i1, nop)           |
                IO_STATE( R_TIMER_CTRL, tm1, run)          |
                IO_STATE( R_TIMER_CTRL, clksel1, cascade0) |
                IO_STATE( R_TIMER_CTRL, i0, nop)           |
                IO_STATE( R_TIMER_CTRL, tm0, run)          |
                IO_STATE( R_TIMER_CTRL, clksel0, flexible);

        *R_TIMER_PRESCALE = RTAI_CASCADED_TIMER_PRESCALE;
#endif	
	
#ifdef CONFIG_PROC_FS
	rtai_proc_register();
#endif
	
	return 0;
}

static void cleanup_rtai_cris(void)
{

#ifdef CONFIG_PROC_FS
	rtai_proc_unregister();
#endif

	/* Release any timer-interrupt handler */
	rt_free_timer();
	
#ifdef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
	/* Restore timer control shadow */
	r_timer_ctrl_shadow = saved_r_timer_ctrl_shadow;
#endif
 
}

void rt_mount_rtai(void)
{
	if ( !(rtai_mounted++) ){
		init_rtai_cris();
		__rt_mount_rtai();
	}
}

void rt_umount_rtai(void)
{
	if ( !(--rtai_mounted) ) {
		__rt_umount_rtai();
		cleanup_rtai_cris();
	}
}

module_init(init_rtai_cris);

module_exit(cleanup_rtai_cris);

/* 
--------------------------------------------------------------------------
*/
/* Mostly copied from i386 */

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rtai_proc_root = NULL;

static int rtai_read_rtai(char *page, char **start, off_t off, int count,
                          int *eof, void *data)
{

	PROC_PRINT_VARS;
	int i;

	PROC_PRINT("\nRTAI Real Time Kernel, Version: %s\n\n", RTAI_RELEASE);
	PROC_PRINT("    RTAI mount count: %d\n", rtai_mounted);

	PROC_PRINT("    TIMER Frequency: %d\n", FREQ_8254);
	PROC_PRINT("    TIMER Latency: %d ns\n", LATENCY_8254);
	PROC_PRINT("    TIMER Setup: %d ns\n", SETUP_TIME_8254);
#ifndef CONFIG_ETRAX_DISABLE_CASCADED_TIMERS_IN_RTAI
	PROC_PRINT("\n    Using timers in cascade-mode.\n");
#endif
	PROC_PRINT("\nirqs used by RTAI: \n");
	for (i = 0; i < NR_GLOBAL_IRQS; i++) {
		if (global_irq_handlers[i]) {
			PROC_PRINT("%d ", i);
		}
	}
	PROC_PRINT("\nRTAI sysreqs in use: \n");
	for (i = 0; i < NR_GLOBAL_IRQS; i++) {
		if (sysrq[i].rtai_handler || sysrq[i].user_handler) {
			PROC_PRINT("%d ", i);
		}
	}
	PROC_PRINT("\n\n");

	PROC_PRINT_DONE;
}       /* End function - rtai_read_rtai */


static int rtai_proc_register(void)
{
	struct proc_dir_entry *ent;

	rtai_proc_root = create_proc_entry("rtai", S_IFDIR, 0);
	if (!rtai_proc_root) {
		printk("Unable to initialize /proc/rtai\n");
		return(-1);
	}
	/* rtai.o is compiled into the kernel */
	/* rtai_proc_root->owner = THIS_MODULE; */
	ent = create_proc_entry("rtai", S_IFREG|S_IRUGO|S_IWUSR, rtai_proc_root);
	if (!ent) {
		printk("Unable to initialize /proc/rtai/rtai\n");
		return(-1);
	}
	ent->read_proc = rtai_read_rtai;
	return(0);
}       /* End function - rtai_proc_register */


static void rtai_proc_unregister(void)
{
	remove_proc_entry("rtai", rtai_proc_root);
	remove_proc_entry("rtai", 0);
}       /* End function - rtai_proc_unregister */

#endif /* CONFIG_PROC_FS */

/* 
--------------------------------------------------------------------------
*/

EXPORT_SYMBOL(rt_mask_and_ack_irq);
EXPORT_SYMBOL(rt_mask_irq);
EXPORT_SYMBOL(rt_unmask_irq);
EXPORT_SYMBOL(rt_disable_irq);
EXPORT_SYMBOL(rt_enable_irq);
EXPORT_SYMBOL(rt_request_global_irq);
EXPORT_SYMBOL(rt_free_global_irq);
EXPORT_SYMBOL(rt_free_linux_irq);
EXPORT_SYMBOL(rt_free_srq);
EXPORT_SYMBOL(rt_free_timer);
EXPORT_SYMBOL(rt_mount_rtai); 
EXPORT_SYMBOL(rt_umount_rtai);
EXPORT_SYMBOL(rt_pend_linux_irq);
EXPORT_SYMBOL(rt_pend_linux_srq); 
EXPORT_SYMBOL(rt_printk);
EXPORT_SYMBOL(rt_request_linux_irq);
EXPORT_SYMBOL(rt_request_srq);
EXPORT_SYMBOL(rt_request_timer);
EXPORT_SYMBOL(rt_shutdown_irq);
EXPORT_SYMBOL(rt_startup_irq);
EXPORT_SYMBOL(rt_switch_to_linux);
EXPORT_SYMBOL(rt_switch_to_real_time);
EXPORT_SYMBOL(rt_is_linux);
EXPORT_SYMBOL(rt_times);
EXPORT_SYMBOL(tuned);
EXPORT_SYMBOL(dispatch_irq);
EXPORT_SYMBOL(dispatch_timer_irq);
EXPORT_SYMBOL(dispatch_srq);
EXPORT_SYMBOL(rtai_just_copy_back);
EXPORT_SYMBOL(rtai_delay);
EXPORT_SYMBOL(rtai_tsc);
EXPORT_SYMBOL(rtai_lastcount);
EXPORT_SYMBOL(rtai_proc_root);
EXPORT_SYMBOL(linux_save_flags_and_cli_cpuid);
