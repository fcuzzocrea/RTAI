/* arch/arm/rtai.c
COPYRIGHT (C) 2001 Paolo Mantegazza (mantegazza@aero.polimi.it)
COPYRIGHT (C) 2001 Alex Züpke, SYSGO RTS GmbH (azu@sysgo.de)
COPYRIGHT (C) 2002 Wolfgang Müller (wolfgang.mueller@dsa-ac.de)
COPYRIGHT (C) 2002 Guennadi Liakhovetski, DSA GmbH (gl@dsa-ac.de)
COPYRIGHT (C) 2002 Thomas Gleixner (gleixner@autronix.de)

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
*/
/*
--------------------------------------------------------------------------
Changelog

03-10-2002	TG	added support for trap handling.
			added function rt_is_linux.

11-07-2003	GL	First of a series of fixes, proposed by Thomas
			Gleixner. Disable interrupts on return from
			dispatch_[irq|srq]().

12-07-2003	GL	Move closer to Linux. Remove re-declaration
			of irq_desc, replace IBIT with Linux' I_BIT,
			remove unneeded rtai_irq_t type.

21-07-2003	GL	Fix a race in linux_sti(), created by making
			linux_sti() re-entrant.

28-07-2003	GL	Improve handling of pending interrupts to Linux,
			put debugging BUG() statement, idea of which
			belongs to Thomas Gleixner.

29-07-2003	GL	Initial support for 2.4.19-rmk7 for StrongARM.

28-08-2003	GL	Use native Linux masking for linux-only
			interrupts. Handle unmasking of RTAI-Linux
			shared interrupts, deliver them, if new
			ones arrived, while the interrupt line
			was masked by Linux.
*/

#include <linux/config.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/compiler.h>

#include <asm/system.h>
#include <asm/smp.h>
#include <asm/io.h>
#include <linux/bitops.h>
#include <asm/atomic.h>

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <rtai_proc_fs.h>
#endif

#include <asm/rtai.h>
#include <asm/rtai_srq.h>
#include <rtai_version.h>
#include <rtai_trace.h>

#undef CONFIG_RTAI_MOUNT_ON_LOAD

// proc filesystem additions.
#ifdef CONFIG_PROC_FS
static int rtai_proc_register(void);
static void rtai_proc_unregister(void);
#endif
// End of proc filesystem additions.

/* Some define */

#define NR_SYSRQS  	 	32
#define NR_TRAPS		32

/* these are prototypes for timer-handling abstraction */
extern void linux_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs);
extern void soft_timer_interrupt(int irq, void *dev_id, struct pt_regs *regs);

struct global_rt_status {
	volatile unsigned int used_by_linux;
	volatile unsigned int locked_cpus;
	volatile int irq_in, irq_out, lost_irqs;
	volatile rtai_irq_mask_t pending_srqs;
	volatile rtai_irq_mask_t active_srqs;
	struct list_head pending_linux_irq;
	spinlock_t data_lock;
	spinlock_t ic_lock;
};

static struct global_rt_status global __attribute__ ((aligned(32)));

volatile unsigned int *locked_cpus = &global.locked_cpus;

/* VERY IMPORTANT, since I saw no way to just ack, we mask_ack always, so	*/
/* it is likely we have to recall to set an arch dependent call to unmask	*/
/* in the scheduler timer handler. Other arch allow just to ack, maybe we'll	*/
/* we can get along as it is now, let's recall this point.			*/

#include <asm/mach/irq.h>

extern struct irqdesc irq_desc[];

#include <asm/rtai_irqops.h>

/* Most of our data */

static struct irq_handling {
	void (*handler)(int irq, void *dev_id, struct pt_regs *regs);
	void *dev_id;
	unsigned long count;
} global_irq[NR_IRQS] __attribute__ ((__aligned__(32)));

static struct irqdesc shadow_irq_desc[NR_IRQS];

static struct sysrq_t {
	unsigned int label;
	void (*rtai_handler)(void);
	long long (*user_handler)(unsigned int whatever);
} sysrq[NR_SYSRQS];

static RT_TRAP_HANDLER rtai_trap_handler[NR_TRAPS];

volatile unsigned long lxrt_hrt_flags;

// The main items to be saved-restored to make Linux our humble slave

static struct rt_hal linux_rthal;

static struct pt_regs rtai_regs;  // Dummy registers.

static void *saved_timer_action_handler; // Saved timer-action handler

static struct cpu_own_status {
	volatile unsigned int intr_flag;
	volatile unsigned int linux_intr_flag;
	volatile rtai_irq_mask_t pending_irqs;
	volatile rtai_irq_mask_t active_irqs;
} processor[NR_RT_CPUS];

void send_ipi_shorthand(unsigned int shorthand, unsigned int irq) { }

void send_ipi_logical(unsigned long dest, unsigned int irq) { }

//static void hard_lock_all_handler(void) { }

volatile union rtai_tsc rtai_tsc;

static void linux_cli(void)
{
	processor[hard_cpu_id()].intr_flag = 0;
}

static void (*ic_mask_ack_irq[NR_IRQS])(unsigned int irq);
static void (*ic_mask_irq[NR_IRQS])(unsigned int irq);
static void (*ic_unmask_irq[NR_IRQS])(unsigned int irq);

/**
 * linux_sti() must be re-entrant.
 */
static void linux_sti(void)
{
	unsigned int irq;
	unsigned long flags;
	unsigned long cpuid;
	struct cpu_own_status *cpu;

	cpu = processor + (cpuid = hard_cpu_id());

	hard_save_flags(flags);
	if (unlikely(flags & I_BIT)) {
		hard_sti();
		BUG();
	}

	rt_spin_lock_irq(&(global.data_lock));
	while (have_pending_irq() != NO_IRQ || have_pending_srq()) {
		cpu->intr_flag = 0; /* cli */
		while ((irq = have_pending_irq()) != NO_IRQ) {
			struct irqdesc *desc = irq_desc + irq;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,19)
			if (desc->pending || desc->disable_depth || ! list_empty(&desc->pend)) {
				printk(KERN_CRIT "IRQ %d: pending %d, disable_depth %d, running %d, list %sempty\n",
				       irq, desc->pending, desc->disable_depth, desc->running,
				       list_empty(&desc->pend) ? "" : "not ");
				BUG();
			}
#else
			if (! desc->enabled) {
				printk(KERN_CRIT "IRQ %d: disabled\n", irq);
				BUG();
			}
#endif

			clear_pending_irq(irq);
			/* Emulate Linux behaviour, i.e. serve multiplexed interrupts 1 at a time */
			if (isdemuxirq(irq))
				irq_desc[ARCH_MUX_IRQ].running = 1;
			rt_spin_unlock_irq(&(global.data_lock));

			// ** call old Linux do_IRQ() to handle IRQ
			linux_rthal.do_IRQ(irq, &rtai_regs);

			/* Unmasking is done in do_IRQ above - don't do twice */
			rt_spin_lock_irq(&(global.data_lock));
			if (isdemuxirq(irq))
				irq_desc[ARCH_MUX_IRQ].running = 0;
		}

		// Local IRQ Handling - missing here ... only on SMP

		cpu->intr_flag = I_BIT | (1 << cpuid); // sti()
		if (have_pending_srq()) {
			// SRQ Handling - same as on PPC
			irq = pending_srq();
			activate_srq(irq);
			clear_pending_srq(irq);
			rt_spin_unlock_irq(&(global.data_lock));
			if (sysrq[irq].rtai_handler) {
				sysrq[irq].rtai_handler();
			}
			rt_spin_lock_irq(&(global.data_lock));
			deactivate_srq(irq);
		}
	}
	rt_spin_unlock_irq(&(global.data_lock));
	cpu->intr_flag = I_BIT | (1 << cpuid);
}

/* we need to return faked, but real flags
 *
 * imagine a function calling our linux_save_flags() while rtai is loaded
 * and restoring flags when rtai is unloaded. CPSR is broken ...
 */
static unsigned int linux_save_flags(void)
{
	unsigned long flags;

	hard_save_flags( flags );
	/* check if we are in CLI, then set I bit in flags */
	return (flags & ~I_BIT) | ( processor[hard_cpu_id()].intr_flag ? 0 : I_BIT );
}

static void linux_restore_flags(unsigned int flags)
{
	/* check if CLI-bit is set */
	if (flags & I_BIT) {
		processor[hard_cpu_id()].intr_flag = 0;
	} else {
		linux_sti();
	}
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21)
static unsigned int linux_save_flags_sti(void)
{
	unsigned long flags;

	flags = linux_save_flags();
	linux_sti();
	return flags;
}
#endif

unsigned int linux_save_flags_and_cli(void)
{
	unsigned long flags;

	flags = linux_save_flags();
        processor[hard_cpu_id()].intr_flag = 0;
	return flags;
}

unsigned long linux_save_flags_and_cli_cpuid(int cpuid)
{
	return linux_save_flags_and_cli();
}

void rtai_just_copy_back(unsigned long flags, int cpuid)
{
	/* also check if CLI-bit is set and set up intr_flag accordingly */
	if (flags & I_BIT) {
	        processor[cpuid].intr_flag = 0;
	} else {
	        processor[cpuid].intr_flag = I_BIT | (1 << cpuid);
	}
}

// For the moment we just do mask_ack_unmask, maybe it has to be adjusted

static void do_nothing_picfun(unsigned int irq) { }
static void linux_mask_ack(unsigned int irq)
{
	irq_desc[irq].masked = 1;
}

unsigned int rt_startup_irq(unsigned int irq)
{
	unsigned int flags;
	struct irqdesc *irq_desc;

	if ((irq_desc = &shadow_irq_desc[irq])/* && irq_desc->unmask*/) {
		flags = rt_spin_lock_irqsave(&global.ic_lock);
		irq_desc->probing = 0;
		irq_desc->triggered = 0;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
		irq_desc->enabled = 1;
#else
		irq_desc->disable_depth = 0;
#endif
		irq_desc->unmask(irq);
		rt_spin_unlock_irqrestore(flags, &global.ic_lock);
	}
	return 0;
}

void rt_shutdown_irq(unsigned int irq)
{
	unsigned int flags;
	struct irqdesc *irq_desc;

	if ((irq_desc = &shadow_irq_desc[irq])) {
		flags = rt_spin_lock_irqsave(&global.ic_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
		irq_desc->enabled = 0;
#else
		irq_desc->disable_depth = (unsigned int)-1;
#endif
		irq_desc->mask(irq);
		rt_spin_unlock_irqrestore(flags, &global.ic_lock);
	}
}

void rt_enable_irq(unsigned int irq)
{
	unsigned int flags;
	struct irqdesc *irq_desc;

	if ((irq_desc = &shadow_irq_desc[irq])) {
		flags = rt_spin_lock_irqsave(&global.ic_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
		irq_desc->probing = 0;
		irq_desc->triggered = 0;
		irq_desc->enabled = 1;
		ic_unmask_irq[irq](irq);
#else
		if ( ! irq_desc->disable_depth ) {
			printk("enable_irq(%u) unbalanced from %p\n", irq, __builtin_return_address(0));
		} else if ( ! --irq_desc->disable_depth ) {
			irq_desc->probing = 0;
			ic_unmask_irq[irq](irq);
		}
#endif
		rt_spin_unlock_irqrestore(flags, &global.ic_lock);
	}
}

void rt_disable_irq(unsigned int irq)
{
	unsigned int flags;
	struct irqdesc *irq_desc;

	if ((irq_desc = &shadow_irq_desc[irq])) {
		flags = rt_spin_lock_irqsave(&global.ic_lock);
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,19)
		irq_desc->enabled = 0;
		ic_mask_irq[irq](irq);
#else
		if ( ! irq_desc->disable_depth++ )
			ic_mask_irq[irq](irq);
#endif
		rt_spin_unlock_irqrestore(flags, &global.ic_lock);
	}
}

void rt_mask_ack_irq(unsigned int irq)
{
	unsigned int flags;

	flags = rt_spin_lock_irqsave(&global.ic_lock);
	ic_mask_ack_irq[irq](irq);
	rt_spin_unlock_irqrestore(flags, &global.ic_lock);
}

void rt_mask_irq(unsigned int irq)
{
	unsigned int flags;

	flags = rt_spin_lock_irqsave(&global.ic_lock);
	ic_mask_irq[irq](irq);
	rt_spin_unlock_irqrestore(flags, &global.ic_lock);
}

void rt_unmask_irq(unsigned int irq)
{
	unsigned int flags;

	flags = rt_spin_lock_irqsave(&global.ic_lock);
	ic_unmask_irq[irq](irq);
	rt_spin_unlock_irqrestore(flags, &global.ic_lock);
}

/*
 * A real time handler must unmask ASAP. Especially important for the timer
 * ISR. When RTAI is mounted, this should be done in the macro
 * DO_TIMER_PROPER_OP(), called on entry to the rt_timer_handler().
 */
asmlinkage void dispatch_irq(int irq, struct pt_regs *regs)
{
	rt_spin_lock(&global.ic_lock);

	if (irq >= 0 && irq < NR_IRQS) {
		ic_mask_ack_irq[irq](irq);
		irq_desc[irq].masked = 0;
		rt_spin_unlock(&global.ic_lock);

		TRACE_RTAI_GLOBAL_IRQ_ENTRY(irq, !user_mode(regs));

		// We just care about our own RT-Handlers installed

		if (global_irq[irq].handler) {
			/* If this interrupt happened in Linux, in the rt-handler below
			   we might context-switch to rt, and then back to Linux, at which
			   point linux_sti() might be called - which is a bit early yet.
			   Prevent it. */
			unsigned long flags = linux_save_flags_and_cli();
			global_irq[irq].count++;
			global_irq[irq].handler(irq, global_irq[irq].dev_id, regs);

			rtai_just_copy_back(flags, hard_cpu_id());
			rt_spin_lock_irq(&(global.data_lock));
			/* The timer interrupt should be unmasked in the handler, due
			   to RTAI's prioritisation of RT-tasks. Linux has a handler-flag
			   to mark handlers, that want their interrupts to be unmasked
			   immediately (hint). */
			ic_unmask_irq[irq](irq);
		} else {
			rt_spin_lock(&(global.data_lock));
			rt_pend_linux_irq(irq);
		}

		if (! isdemuxirq(irq) && (global.used_by_linux & processor[hard_cpu_id()].intr_flag)) {
			linux_cli();
			rt_spin_unlock_irq(&(global.data_lock));
			linux_sti();
		} else {
			rt_spin_unlock(&(global.data_lock));
		}
		TRACE_RTAI_GLOBAL_IRQ_EXIT();
	} else {
		rt_spin_unlock(&global.ic_lock);
		printk(KERN_ERR "RTAI-IRQ: spurious interrupt 0x%02x\n", irq);
	}
	hard_cli();
}

#define MIN_IDT_VEC 0xF0
#define MAX_IDT_VEC 0xFF

static unsigned long long (*idt_table[MAX_IDT_VEC - MIN_IDT_VEC + 1])(unsigned int srq, unsigned long name);

asmlinkage long long dispatch_srq(int srq, unsigned long whatever)
{
	unsigned long vec;
	long long retval = -1;

	if (!(vec = srq >> 24)) {
		TRACE_RTAI_SRQ_ENTRY(srq, 0);
		if (srq > 1 && srq < NR_SYSRQS && sysrq[srq].user_handler) {
			retval = sysrq[srq].user_handler(whatever);
		} else {
			for (srq = 2; srq < NR_SYSRQS; srq++) {
				if (sysrq[srq].label == whatever) {
					retval = srq;
				}
			}
		}
		TRACE_RTAI_SRQ_EXIT();
	} else {
		if ((vec >= MIN_IDT_VEC) && (vec <= MAX_IDT_VEC) && (idt_table[vec - MIN_IDT_VEC])) {
			TRACE_RTAI_SRQ_ENTRY(srq, 0);
			retval = idt_table[vec - MIN_IDT_VEC](srq & 0xFFFFFF, whatever);
			TRACE_RTAI_SRQ_EXIT();
		} else {
			printk("RTAI SRQ DISPATCHER: bad srq (0x%0x)\n", (int) vec);
		}
	}
	hard_cli();
	return retval;
}

struct desc_struct rt_set_full_intr_vect(unsigned int vector, int type, int dpl, void *handler)
{
	struct desc_struct fun = { 0 };
	if (vector >= MIN_IDT_VEC && vector <= MAX_IDT_VEC) {
		fun.fun = idt_table[vector - MIN_IDT_VEC];
		idt_table[vector - MIN_IDT_VEC] = handler;
		if (!rthal.do_SRQ) {
			rthal.do_SRQ = dispatch_srq;
		}
	}
	return fun;
}

void rt_reset_full_intr_vect(unsigned int vector, struct desc_struct idt_element)
{
	if (vector >= MIN_IDT_VEC && vector <= MAX_IDT_VEC) {
		idt_table[vector - MIN_IDT_VEC] = idt_element.fun;
	}
}

/*
 *	Dispatch Traps like Illegal instruction, ....
 *	Keep call compatible to x386 
 */
asmlinkage int dispatch_trap(int vector, struct pt_regs *regs)
{
	if ( (vector < NR_TRAPS) && (rtai_trap_handler[vector]) ) 
		return rtai_trap_handler[vector](vector, vector, regs, NULL);

	return 1;	/* Let Linux do the job */
}

RT_TRAP_HANDLER rt_set_rtai_trap_handler(int trap, RT_TRAP_HANDLER handler)
{
        RT_TRAP_HANDLER old_handler = NULL;

	if (trap < NR_TRAPS) {
		old_handler = rtai_trap_handler[trap];
                rtai_trap_handler[trap] = handler;
        }
        return old_handler;
}

void rt_free_rtai_trap_handler(int trap)
{
	rtai_trap_handler[trap] = NULL;
}

/* Here are the trapped pic functions for Linux interrupt handlers. */

static void shared_unmask_irq(unsigned int irq)
{
	unsigned long flags;
	flags = rt_spin_lock_irqsave(&global.ic_lock);
	if (linux_irqs[irq].pend_count && (processor[hard_cpu_id()].intr_flag & I_BIT)) {
		rt_spin_unlock_irqrestore(flags, &global.ic_lock);
		linux_sti();
	} else
		rt_spin_unlock_irqrestore(flags, &global.ic_lock);
}

/* Request and free interrupts, system requests and interprocessors messages */
/* Request for regular Linux irqs also included. They are nicely chained to  */
/* Linux, forcing sharing with any already installed handler, so that we can */
/* have an echo from Linux for global handlers. We found that usefull during */
/* debug, but can be nice for a lot of other things, e.g. see the jiffies    */
/* recovery in rtai_sched.c, and the global broadcast to local apic timers.  */

static unsigned long irq_action_flags[NR_IRQS];
static int chained_to_linux[NR_IRQS];

int rt_request_global_irq_ext(unsigned int irq, void (*handler)(int, void *, struct pt_regs *), void *dev_id)
{
	unsigned long flags;

	if (irq >= NR_IRQS || !handler) {
		return -EINVAL;
	}
	if (global_irq[irq].handler != NULL) {
		return -EBUSY;
	}

	flags = hard_lock_all();

	irq_desc[irq].mask_ack	= do_nothing_picfun;
	irq_desc[irq].mask	= do_nothing_picfun;
	irq_desc[irq].unmask	= shared_unmask_irq;

	shadow_irq_desc[irq].disable_depth = 1;
	
	global_irq[irq].handler = handler;
	global_irq[irq].dev_id = dev_id;
	hard_unlock_all(flags);
	return 0;
}

/* We don't share IRQ's under RT, so, don't need the dev-pointer. */
int rt_free_global_irq(unsigned int irq)
{
	unsigned long flags;

	if (irq >= NR_IRQS || !global_irq[irq].handler) {
		return -EINVAL;
	}

	flags = hard_lock_all();

	irq_desc[irq].mask_ack	= linux_mask_ack;
	irq_desc[irq].mask	= ic_mask_irq[irq];
	irq_desc[irq].unmask	= ic_unmask_irq[irq];

	global_irq[irq].handler = NULL;
	hard_unlock_all(flags);

	return 0;
}

int rt_request_linux_irq(unsigned int irq,
	void (*linux_handler)(int irq, void *dev_id, struct pt_regs *regs),
	char *linux_handler_id, void *dev_id)
{
	unsigned long flags;

//##	
//	if (irq == TIMER_8254_IRQ) {
//		processor[0].trailing_irq_handler = linux_handler;
//		return 0;
//	}

	if (irq >= NR_IRQS || !linux_handler) {
		return -EINVAL;
	}

	save_flags_cli(flags);
	if (!chained_to_linux[irq]++) {
		if (irq_desc[irq].action) {
			irq_action_flags[irq] = irq_desc[irq].action->flags;
			irq_desc[irq].action->flags |= SA_SHIRQ;
		}
	}
	restore_flags(flags);
	request_irq(irq, linux_handler, SA_SHIRQ, linux_handler_id, dev_id);

	return 0;
}

int rt_free_linux_irq(unsigned int irq, void *dev_id)
{
	unsigned long flags;

//##
//	if (irq == TIMER_8254_IRQ) {
//		processor[0].trailing_irq_handler = 0;
//		return 0;
//	}

	if (irq >= NR_IRQS || !chained_to_linux[irq]) {
		return -EINVAL;
	}

	free_irq(irq, dev_id);
	save_flags_cli(flags);
	if (!(--chained_to_linux[irq]) && irq_desc[irq].action) {
		irq_desc[irq].action->flags = irq_action_flags[irq];
	}
	restore_flags(flags);

	return 0;
}

int rt_request_srq(unsigned int label, void (*rtai_handler)(void), long long (*user_handler)(unsigned int whatever))
{
	unsigned long flags;
	unsigned int srq;

	if (!rtai_handler) {
		return -EINVAL;
	}

	flags = rt_spin_lock_irqsave(&global.data_lock);
	for (srq = 2; srq < NR_SYSRQS; srq++) {
		if (!(sysrq[srq].rtai_handler)) {
			sysrq[srq].rtai_handler = rtai_handler;
			sysrq[srq].label = label;
			if (user_handler) {
				sysrq[srq].user_handler = user_handler;
				if (!rthal.do_SRQ) {
					rthal.do_SRQ = dispatch_srq;
				}
			}
			rt_spin_unlock_irqrestore(flags, &global.data_lock);
			return srq;
		}
	}
	rt_spin_unlock_irqrestore(flags, &global.data_lock);

	return -EBUSY;
}

int rt_free_srq(unsigned int srq)
{
	unsigned long flags;

	flags = rt_spin_lock_irqsave(&global.data_lock);
	if (srq < 2 || srq >= NR_SYSRQS || !sysrq[srq].rtai_handler) {
		rt_spin_unlock_irqrestore(flags, &global.data_lock);
		return -EINVAL;
	}
	sysrq[srq].rtai_handler = 0;
	sysrq[srq].user_handler = 0;
	sysrq[srq].label = 0;
	for (srq = 2; srq < NR_SYSRQS; srq++) {
		if (sysrq[srq].user_handler) {
			rt_spin_unlock_irqrestore(flags, &global.data_lock);
			return 0;
		}
	}
	if (rthal.do_SRQ) {
		rthal.do_SRQ = 0;
	}
	rt_spin_unlock_irqrestore(flags, &global.data_lock);

	return 0;
}

int rt_is_linux(void)
{
    return test_bit(hard_cpu_id(), (void *)&global.used_by_linux);
}

void rt_switch_to_linux(int cpuid)
{
	TRACE_RTAI_SWITCHTO_LINUX(cpuid);

	set_bit(cpuid, &global.used_by_linux);
	processor[cpuid].intr_flag = processor[cpuid].linux_intr_flag;
}

void rt_switch_to_real_time(int cpuid)
{
	TRACE_RTAI_SWITCHTO_RT(cpuid);

	if ( global.used_by_linux & (1<<cpuid) )
		processor[cpuid].linux_intr_flag = processor[cpuid].intr_flag;
	processor[cpuid].intr_flag = 0;
	clear_bit(cpuid, &global.used_by_linux);
}

/* RTAI mount-unmount functions to be called from the application to       */
/* initialise the real time application interface, i.e. this module, only  */
/* when it is required; so that it can stay asleep when it is not needed   */

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
#define rtai_mounted 1
#else
static int rtai_mounted;
#endif

// Trivial, but we do things carefully, the blocking part is relatively short,
// should cause no troubles in the transition phase.
// All the zeroings are strictly not required as mostly related to static data.
// Done esplicitly for emphasis. Simple, just lock and grab everything from
// Linux.

void __rt_mount_rtai(void)
{
     	unsigned long flags, i;

	flags = hard_lock_all();

	rthal.do_IRQ		= dispatch_irq;
	rthal.do_SRQ		= dispatch_srq;
	rthal.do_TRAP		= dispatch_trap;
	rthal.disint		= linux_cli;
	rthal.enint		= linux_sti;
	rthal.getflags		= linux_save_flags;
	rthal.setflags		= linux_restore_flags;
	rthal.getflags_and_cli	= linux_save_flags_and_cli;
	rthal.fdisint		= linux_cli;
	rthal.fenint		= linux_sti;
	rthal.copy_back		= rtai_just_copy_back;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,21)
	rthal.getflags_and_sti	= linux_save_flags_sti;
#endif

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].mask_ack	= linux_mask_ack;
		irq_desc[i].mask	= ic_mask_irq[i];
		irq_desc[i].unmask	= ic_unmask_irq[i];
		linux_irqs[i].pend_count = 0;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,4,19)
	rthal.c_do_IRQ = dispatch_irq;
#endif

	INIT_LIST_HEAD(&global.pending_linux_irq);

	arch_mount_rtai();

	saved_timer_action_handler = irq_desc[TIMER_8254_IRQ].action->handler;
	irq_desc[TIMER_8254_IRQ].action->handler = linux_timer_interrupt;

	hard_unlock_all(flags);
	printk("\n***** RTAI NEWLY MOUNTED (MOUNT COUNT %d) ******\n\n", rtai_mounted);
}

// Simple, now we can simply block other processors and copy original data back
// to Linux.

void __rt_umount_rtai(void)
{
	int i;
	unsigned long flags;

	flags = hard_lock_all();

	arch_umount_rtai();

        rthal = linux_rthal;

	for (i = 0; i < NR_IRQS; i++) {
		irq_desc[i].mask_ack	= ic_mask_ack_irq[i];
		irq_desc[i].mask	= ic_mask_irq[i];
		irq_desc[i].unmask	= ic_unmask_irq[i];
	}

	irq_desc[TIMER_8254_IRQ].action->handler = saved_timer_action_handler;

	hard_unlock_all(flags);
	printk("\n***** RTAI UNMOUNTED (MOUNT COUNT %d) ******\n\n", rtai_mounted);
}

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
void rt_mount_rtai(void) { }
void rt_umount_rtai(void) { }
#else
void rt_mount_rtai(void)
{
	rt_spin_lock(&rtai_mount_lock);
	MOD_INC_USE_COUNT;
	TRACE_RTAI_MOUNT();
	if(++rtai_mounted==1)
		__rt_mount_rtai();
	rt_spin_unlock(&rtai_mount_lock);
}

void rt_umount_rtai(void)
{
	rt_spin_lock(&rtai_mount_lock);
	MOD_DEC_USE_COUNT;
	TRACE_RTAI_UMOUNT();
	if(!--rtai_mounted)
		__rt_umount_rtai();
	rt_spin_unlock(&rtai_mount_lock);
}
#endif

/* module init-cleanup */

extern void rt_printk_sysreq_handler(void);

// Let's prepare our side without any problem, so that there remain just a few
// things to be done when mounting RTAI. All the zeroings are strictly not
// required as mostly related to static data. Done explicitly for emphasis.
static __init int init_rtai(void)
{
     	unsigned int i;

	tuned.cpu_freq = FREQ_SYS_CLK;

	linux_rthal = rthal;

	init_pending_srqs();
	global.active_srqs     = 0;
	global.used_by_linux   = ~(0xFFFFFFFF << smp_num_cpus);
	global.locked_cpus     = 0;
	spin_lock_init(&(global.data_lock));
	spin_lock_init(&(global.ic_lock));

	for (i = 0; i < NR_RT_CPUS; i++) {
		processor[i].intr_flag         = I_BIT | (1 << i);
		processor[i].linux_intr_flag   = I_BIT | (1 << i);
		processor[i].pending_irqs      = 0;
		processor[i].active_irqs       = 0;
	}
        for (i = 0; i < NR_SYSRQS; i++) {
		sysrq[i].rtai_handler = 0;
		sysrq[i].user_handler = 0;
		sysrq[i].label        = 0;
        }
	for (i = 0; i < NR_IRQS; i++) {
		global_irq[i].handler = 0;

		shadow_irq_desc[i] = irq_desc[i];
		ic_mask_ack_irq[i] = irq_desc[i].mask_ack;
		ic_mask_irq[i] = irq_desc[i].mask;
		ic_unmask_irq[i] = irq_desc[i].unmask;
	}

	sysrq[rt_printk_srq].rtai_handler = rt_printk_sysreq_handler;
	sysrq[rt_printk_srq].label = 0x1F0000F1;

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
	__rt_mount_rtai();
#endif

#ifdef CONFIG_PROC_FS
	rtai_proc_register();
#endif

	return 0;
}

static __exit void cleanup_rtai(void)
{

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
	__rt_umount_rtai();
#endif

#ifdef CONFIG_PROC_FS
	rtai_proc_unregister();
#endif

	return;
}

module_init(init_rtai);
module_exit(cleanup_rtai);

MODULE_LICENSE("GPL");

/* ----------------------< proc filesystem section >----------------------*/
#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rtai_proc_root = NULL;

static int rtai_read_rtai(char *page, char **start, off_t off, int count,
                          int *eof, void *data)
{
        PROC_PRINT_VARS;
        int i;

        PROC_PRINT("\nRTAI Real Time Kernel, Version: %s\n\n", RTAI_RELEASE);
        PROC_PRINT("    RTAI mount count: %d\n", rtai_mounted);
	PROC_PRINT("    Clock frequency: %d Hz\n", FREQ_SYS_CLK);
	PROC_PRINT("    Timer latency: %d ns\n", LATENCY_MATCH_REG);
	PROC_PRINT("    Timer setup: %d ns\n", SETUP_TIME_MATCH_REG);
        PROC_PRINT("\nGlobal irqs used by RTAI: \n");
	for (i = 0; i < NR_IRQS; i++) {
		if (global_irq[i].handler) {
			PROC_PRINT("%d\t%lu\n", i, global_irq[i].count);
		}
        }
        PROC_PRINT("\nRTAI sysreqs in use: \n");
        for (i = 0; i < NR_SYSRQS; i++) {
		if (sysrq[i].rtai_handler || sysrq[i].user_handler) {
			PROC_PRINT("%d ", i);
		}
        }
	if ( global.lost_irqs )
		PROC_PRINT( "### Lost IRQs: %d ###\n", global.lost_irqs );
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
	rtai_proc_root->owner = THIS_MODULE;
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
/* ------------------< end of proc filesystem section >------------------*/


/********** SOME TIMER FUNCTIONS TO BE LIKELY NEVER PUT ELSWHERE *************/

/* Real time timers. No oneshot, and related timer programming, calibration. */
/* Use the utility module. It is also to be decided if this stuff has to     */
/* stay here.                                                                */

struct calibration_data tuned;
struct rt_times rt_times;
struct rt_times rt_smp_times[NR_RT_CPUS];

/******** END OF SOME TIMER FUNCTIONS TO BE LIKELY NEVER PUT ELSWHERE *********/

// Our printk function, its use should be safe everywhere.
#include <linux/console.h>

int rtai_print_to_screen(const char *format, ...)
{
        static spinlock_t display_lock = SPIN_LOCK_UNLOCKED;
        static char display[25*80];
        unsigned long flags;
        struct console *c;
        va_list args;
        int len;

        flags = rt_spin_lock_irqsave(&display_lock);
        va_start(args, format);
        len = vsprintf(display, format, args);
        va_end(args);
        c = console_drivers;
        while(c) {
                if ((c->flags & CON_ENABLED) && c->write)
                        c->write(c, display, len);
                c = c->next;
	}
        rt_spin_unlock_irqrestore(flags, &display_lock);

	return len;
}

EXPORT_SYMBOL(rt_mask_ack_irq);
EXPORT_SYMBOL(rt_mask_irq);
EXPORT_SYMBOL(rt_disable_irq);
EXPORT_SYMBOL(rt_enable_irq);
EXPORT_SYMBOL(rt_free_global_irq);
EXPORT_SYMBOL(rt_free_linux_irq);
EXPORT_SYMBOL(rt_free_srq);
EXPORT_SYMBOL(rt_free_timer);
EXPORT_SYMBOL(rt_mount_rtai);
EXPORT_SYMBOL(rt_pend_linux_irq);
EXPORT_SYMBOL(rt_pend_linux_srq);
EXPORT_SYMBOL(rt_printk);
EXPORT_SYMBOL(rt_request_global_irq_ext);
EXPORT_SYMBOL(rt_request_linux_irq);
EXPORT_SYMBOL(rt_request_srq);
EXPORT_SYMBOL(rt_request_timer);
EXPORT_SYMBOL(rt_reset_full_intr_vect);
EXPORT_SYMBOL(rt_set_full_intr_vect);
EXPORT_SYMBOL(rt_shutdown_irq);
EXPORT_SYMBOL(rt_startup_irq);
EXPORT_SYMBOL(rt_switch_to_linux);
EXPORT_SYMBOL(rt_switch_to_real_time);
EXPORT_SYMBOL(rt_umount_rtai);
EXPORT_SYMBOL(rt_unmask_irq);
EXPORT_SYMBOL(rtai_proc_root);
EXPORT_SYMBOL(rt_smp_times);
EXPORT_SYMBOL(rt_times);
EXPORT_SYMBOL(tuned);
EXPORT_SYMBOL(locked_cpus);
EXPORT_SYMBOL(linux_save_flags_and_cli);
EXPORT_SYMBOL(linux_save_flags_and_cli_cpuid);
EXPORT_SYMBOL(rtai_just_copy_back);
EXPORT_SYMBOL(global);
EXPORT_SYMBOL(rtai_print_to_screen);
EXPORT_SYMBOL(rtai_tsc);
EXPORT_SYMBOL(rt_set_rtai_trap_handler);
EXPORT_SYMBOL(rt_free_rtai_trap_handler);
EXPORT_SYMBOL(rt_is_linux);
EXPORT_SYMBOL(up_task_sw);

#include <asm-arm/arch/rtai_exports.h>
