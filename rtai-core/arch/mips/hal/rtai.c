/*
 * MIPS Port - Steve Papacharalambous (stevep@lineo.com) - 7 June 2001
 * COPYRIGHT (C) 2001 Steve Papacharalambous.
 * COPYRIGHT (C) 2001  Paolo Mantegazza (mantegazza@aero.polimi.it)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * ACKNOWLEDGMENTS: 
 * - Steve Papacharalambous (stevep@zentropix.com) has contributed an
 * informative proc filesystem procedure.
 * Stuart Hughes (sehughes@zentropix.com) has helped a lot in debugging the 
 * porting of this module to 2.4.xx.
 */

/*
 * Module to hook plain Linux up to do real time the way you like, hardware,
 * hopefully, fully trapped; the machine is in your hand, do what you want!
 */

/*
 * Modified for 2.4.18 and other various things by Steven Seeger
 * sseeger@stellartec.com 07/08/2002
 */

#include <linux/module.h>
#include <linux/version.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/init.h>
#include <linux/string.h>

#include <asm/system.h>
#include <asm/hw_irq.h>
#include <asm/io.h>

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include "rtai_proc_fs.h"
#endif

#include <asm/rtai.h>
#include <asm/rtai_srq.h>
#include <rtai_version.h>

MODULE_LICENSE("GPL");

/* proc filesystem additions. */
static int rtai_proc_register(void);
static void rtai_proc_unregister(void);
/* End of proc filesystem additions. */


/* Some defines. */

#undef CONFIG_RTAI_MOUNT_ON_LOAD
#define CONFIG_RTAI_MOUNT_ON_LOAD 1

#define NR_GLOBAL_IRQS          64

#define NR_RTAI_IRQS            64
#define NR_SYSRQS               32
#define LAST_GLOBAL_RTAI_IRQ    63

#define IRQ_DESC ((irq_desc_t *)rthal.irq_desc)

#define HINT_DIAG_LSTI
#define HINT_DIAG_ECHO
#define HINT_DIAG_LRSTF

#ifdef HINT_DIAG_ECHO
#define HINT_DIAG_MSG(x) x
#else
#define HINT_DIAG_MSG(x)
#endif

/* External definitions. */
extern void *set_except_vector(int n, void *addr);

extern unsigned char debug_pport;

/* Function prototypes. */
unsigned long linux_save_flags_and_cli(void);
void rtai_just_copy_back(unsigned long flags, int cpuid);

/* Data declarations. */
static void (*global_irq_handler[NR_GLOBAL_IRQS])(void);

static unsigned long linux_hz = 0;
static unsigned long rtai_hz = 0;
static unsigned int rt_timer_active;

extern unsigned long cycles_per_jiffy; //new to 2.4.18

static struct sysrq_t {
	unsigned int label;
	void (*rtai_handler)(void);
	long long (*user_handler)(unsigned int whatever);
} sysrq[NR_SYSRQS];


struct rt_hal linux_rthal;

static unsigned int (*linux_isr[NR_IRQS])(int irq, struct pt_regs *) = {0};

/*
 * Array of pointers to pt_regs structures.  This is used by the
 * dispatch_xxxx functions to save the pt_regs structure pointers
 * until the linux ISR is invoked.
 *
 * This is a horrible temporary hack.  - Stevep :-(
 */
struct pt_regs *rtai_regs[NR_IRQS];

static struct hw_interrupt_type *linux_irq_desc_handler[NR_IRQS];

static struct global_rt_status global;

volatile unsigned int *locked_cpus = &global.locked_cpus;


static struct cpu_own_status { 
	volatile unsigned int intr_flag;
	volatile unsigned int linux_intr_flag;
	volatile unsigned int pending_irqs_l;
	volatile unsigned int pending_irqs_h;   
	volatile unsigned int activ_irqs;
	volatile int irqs[NR_GLOBAL_IRQS];

} processor[NR_RT_CPUS];

static inline unsigned long hard_lock_all(void)
{
	unsigned long flags;

	hard_save_flags_and_cli(flags);
	return(flags);
} /* End function - hard_lock_all */

#define hard_unlock_all(flags) hard_restore_flags((flags))

/*
 * This is used in place of the standard Linux kernel function as the 
 * standard Linux assembler version doesn't work properly with the IDT
 * CPU (causes adel exceptions out of the wazoo) and the c version uses
 * cli which becomes a soft cli when rtai is mounted.  - Stevep
 */
static inline unsigned long rtai_xchg_u32(volatile int *m, unsigned long val)
{
	unsigned long flags, retval;

	hard_save_flags_and_cli(flags);
	retval = *m;
	*m = val;
	hard_restore_flags(flags);
	return(retval);
} /* End function - rtai_xchg_u32 */


/*
 * Functions to control Advanced-Programmable Interrupt Controllers (A-PIC).
 */
 
/*
 * Now Linux has a per PIC spinlock, as it has always been in RTAI.  So there
 * is more a need to duplicate them here.  Note however that they are not
 * safe since interrupts has just soft disabled, so we have to provide the
 * hard cli/sti.  Moreover we do not want to run Linux_sti uselessly so we
 * clear also the soft flag.
 */
static void (*internal_ic_ack_irq[NR_GLOBAL_IRQS]) (unsigned int irq);
static void (*ic_ack_irq[NR_GLOBAL_IRQS]) (unsigned int irq);
static void (*ic_end_irq[NR_GLOBAL_IRQS]) (unsigned int irq);
static void (*linux_end_irq[NR_GLOBAL_IRQS]) (unsigned int irq);

static void do_nothing_picfun(unsigned int irq) { }

unsigned int rt_startup_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile int *lflagp;
	int retval;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_save_flags_and_cli(flags);
	retval = linux_irq_desc_handler[irq]->startup(irq);
	hard_restore_flags(flags);
	*lflagp = lflags;
	return(retval);
} /* End function - rt_startup_irq */

void rt_shutdown_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_save_flags_and_cli(flags);
	linux_irq_desc_handler[irq]->shutdown(irq);
	hard_restore_flags(flags);
	*lflagp = lflags;
} /* End function - rt_shutdown_irq */


void rt_enable_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_save_flags_and_cli(flags);
	linux_irq_desc_handler[irq]->enable(irq);
	hard_restore_flags(flags);
	*lflagp = lflags;
} /* End function - rt_enable_irq */


void rt_disable_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_save_flags_and_cli(flags);
	linux_irq_desc_handler[irq]->disable(irq);
	hard_restore_flags(flags);
	*lflagp = lflags;
} /* End function - rt_disable_irq */


void rt_mask_and_ack_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_save_flags_and_cli(flags);
	ic_ack_irq[irq](irq);
	hard_restore_flags(flags);
	*lflagp = lflags;
} /* End function - rt_mask_and_ack_irq */


void rt_ack_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_save_flags_and_cli(flags);
	internal_ic_ack_irq[irq](irq);
	hard_restore_flags(flags);
	*lflagp = lflags;
} /* End function - rt_ack_irq */


void rt_unmask_irq(unsigned int irq)
{
	unsigned long flags, lflags;
	volatile int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
        hard_save_flags_and_cli(flags);
	ic_end_irq[irq](irq);
	hard_restore_flags(flags);
	*lflagp = lflags;
} /* End function - rt_unmask_irq */


/*
 * The functions below are the same as those above, except that we do not need
 * to save the hard flags as they have the interrupt bit set for sure.
 */

unsigned int trpd_startup_irq(unsigned int irq)
{
	unsigned int lflags;
	volatile unsigned int *lflagp;
	int retval;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	retval = linux_irq_desc_handler[irq]->startup(irq);
	hard_sti();
	*lflagp = lflags;
	return(retval);
} /* End function - trpd_startup_irq */


void trpd_shutdown_irq(unsigned int irq)
{
	unsigned int lflags;
	volatile unsigned int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_irq_desc_handler[irq]->shutdown(irq);
	hard_sti();
	*lflagp = lflags;
} /* End function - trpd_shutdown_irq */


void trpd_enable_irq(unsigned int irq)
{
	unsigned int lflags;
	volatile unsigned int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_irq_desc_handler[irq]->enable(irq);
	hard_sti();
	*lflagp = lflags;
} /* End function - trpd_enable_irq */


void trpd_disable_irq(unsigned int irq)
{
	unsigned int lflags;
	volatile unsigned int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_irq_desc_handler[irq]->disable(irq);
	hard_sti();
	*lflagp = lflags;
} /* End function - trpd_disable_irq */


void trpd_end_irq(unsigned int irq)
{
	unsigned int lflags;
	volatile unsigned int *lflagp;

	lflags = rtai_xchg_u32(lflagp = &processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_end_irq[irq](irq);
	hard_sti();
	*lflagp = lflags;
} /* End function - trpd_end_irq */


void trpd_set_affinity(unsigned int irq, unsigned long mask)
{
	unsigned int lflags;
	volatile unsigned int *lflagp;

	lflags = rtai_xchg_u32(lflagp = (int *)&processor[hard_cpu_id()].intr_flag, 0);
	hard_cli();
	linux_irq_desc_handler[irq]->set_affinity(irq, mask);
	hard_sti();
	*lflagp = lflags;
} /* End function - trpd_set_affinity */


static struct hw_interrupt_type trapped_linux_irq_type = {
	"RT SPVISD",
	trpd_startup_irq,
	trpd_shutdown_irq,
	trpd_enable_irq,
	trpd_disable_irq,
	do_nothing_picfun,
	trpd_end_irq,
	trpd_set_affinity };

static struct hw_interrupt_type real_time_irq_type = {
	"REAL TIME",
	(unsigned int (*)(unsigned int))do_nothing_picfun,
	do_nothing_picfun,
	do_nothing_picfun,
	do_nothing_picfun,
	do_nothing_picfun,
	do_nothing_picfun,
	(void (*)(unsigned int, unsigned long)) do_nothing_picfun };


void rt_switch_to_linux(int cpuid)
{
	set_bit(cpuid, &global.used_by_linux);

	processor[cpuid].intr_flag = processor[cpuid].linux_intr_flag;
} /* End function - rt_switch_to_linux */


void rt_switch_to_real_time(int cpuid)
{
	processor[cpuid].linux_intr_flag =
			xchg(&(processor[cpuid].intr_flag), 0);
	clear_bit(cpuid, &global.used_by_linux);
} /* End function - rt_switch_to_linux */


/*
 * Request and free interrupts, system requests and interprocessors messages
 * Request for regular Linux irqs also included. They are nicely chained to
 * Linux, forcing sharing with any already installed handler, so that we can
 * have an echo from Linux for global handlers. We found that usefull during
 * debug, but can be nice for a lot of other things, e.g. see the jiffies
 * recovery in rtai_sched.c, and the global broadcast to local apic timers.
 */

static unsigned long irq_action_flags[NR_GLOBAL_IRQS];
static int chained_to_linux[NR_GLOBAL_IRQS];


int rt_request_global_irq(unsigned int irq, void (*handler)(void))
{

	unsigned long flags;

	if(irq >= NR_GLOBAL_IRQS  || !handler) {
		return(-EINVAL);
	}

	if(global_irq_handler[irq]) {
		return(-EBUSY);
	}

	flags = hard_lock_all();
	IRQ_DESC[irq].handler = &real_time_irq_type;
        global_irq_handler[irq] = handler;
	linux_end_irq[irq] = do_nothing_picfun;
	hard_unlock_all(flags);
   
	return(0);

}  /* End function - rt_request_global_irq */


int rt_free_global_irq(unsigned int irq)
{

	unsigned long flags;

	if(irq >= NR_GLOBAL_IRQS || !global_irq_handler[irq]) {
		return(-EINVAL);
	}

	flags = hard_lock_all();
	IRQ_DESC[irq].handler = &trapped_linux_irq_type;
	global_irq_handler[irq] = 0;
	linux_end_irq[irq] = ic_end_irq[irq];
	hard_unlock_all(flags);
	return(0);

}  /* End function - rt_free_global_irq */


int rt_request_linux_irq(unsigned int irq,
	void (*linux_handler)(int irq, void *dev_id, struct pt_regs *regs),
	char *linux_handler_id, void *dev_id)
{

	unsigned long flags, lflags;

	if(irq >= NR_GLOBAL_IRQS || !linux_handler) {
		return(-EINVAL);
	}
	lflags = linux_save_flags_and_cli();
	spin_lock_irqsave(&(IRQ_DESC[irq].lock), flags);
	if(!chained_to_linux[irq]++) {
		if(IRQ_DESC[irq].action) {
			irq_action_flags[irq] = IRQ_DESC[irq].action->flags;
			IRQ_DESC[irq].action->flags |= SA_SHIRQ;
		}
	}
	spin_unlock_irqrestore(&(IRQ_DESC[irq].lock), flags);
	request_irq(irq, linux_handler, SA_SHIRQ, linux_handler_id, dev_id);
	rtai_just_copy_back(lflags, hard_cpu_id());
	return(0);

}  /* End function - rt_request_linux_irq */


int rt_free_linux_irq(unsigned int irq, void *dev_id)
{

	unsigned long flags, lflags;

	if(irq >= NR_GLOBAL_IRQS || !chained_to_linux[irq]) {
		return -EINVAL;
	}
	lflags = linux_save_flags_and_cli();
	free_irq(irq, dev_id);
	spin_lock_irqsave(&(IRQ_DESC[irq].lock), flags);
	if(!(--chained_to_linux[irq]) && IRQ_DESC[irq].action) {
		IRQ_DESC[irq].action->flags = irq_action_flags[irq];
	}
	spin_unlock_irqrestore(&(IRQ_DESC[irq].lock), flags);
	rtai_just_copy_back(lflags, hard_cpu_id());
	return(0);
}


void rt_pend_linux_irq(unsigned int irq)
{

	unsigned long flags;

	flags = hard_lock_all();
	processor[hard_cpu_id()].irqs[irq]++;
   set_bit(irq<32 ? irq : irq-32, irq<32 ? &global.pending_irqs_l : &global.pending_irqs_h);
	hard_unlock_all(flags);

}  /* End function - rt_pend_linux_irq */


int rt_request_srq(unsigned int label, void (*rtai_handler)(void),
			long long (*user_handler)(unsigned int whatever))
{

	unsigned long flags;
	int srq;

	flags = rt_spin_lock_irqsave(&global.data_lock);
	if(!rtai_handler) {
		rt_spin_unlock_irqrestore(flags, &global.data_lock);
		return(-EINVAL);
	}

	for(srq = 2; srq < NR_GLOBAL_IRQS; srq++) {
		if(!(sysrq[srq].rtai_handler)) {
			sysrq[srq].rtai_handler = rtai_handler;
			sysrq[srq].label = label;
			if(user_handler) {
				sysrq[srq].user_handler = user_handler;
			}
			rt_spin_unlock_irqrestore(flags, &global.data_lock);
			return(srq);
		}  /* End if - this srq slot is free. */
	}  /* End for loop - locate a free srq slot. */

	rt_spin_unlock_irqrestore(flags, &global.data_lock);
	return(-EBUSY);

}  /* End function - rt_request_srq */


int rt_free_srq(unsigned int srq)
{

	unsigned long flags;

	flags = rt_spin_lock_irqsave(&global.data_lock);
	if(srq < 2 || srq >= NR_GLOBAL_IRQS || !sysrq[srq].rtai_handler) {
		rt_spin_unlock_irqrestore(flags, &global.data_lock);
		return(-EINVAL);
	}
	sysrq[srq].rtai_handler = 0;
	sysrq[srq].user_handler = 0;
	sysrq[srq].label = 0;
	rt_spin_unlock_irqrestore(flags, &global.data_lock);
	return(0);

}  /* End function - rt_free_srq */


void rt_pend_linux_srq(unsigned int srq)
{

	set_bit(srq, &global.pending_srqs);

}  /* End function - rt_pend_linux_srq */



/*
 * Linux cli/sti emulation routines.
 */

static void linux_cli(void)
{
	processor[hard_cpu_id()].intr_flag = 0;
}


static unsigned long linux_save_flags(void)
{

	unsigned long flags;

	flags = processor[hard_cpu_id()].intr_flag;
	return(flags);
}


static void linux_sti(void)
{
   unsigned long irq, cpuid;
   struct cpu_own_status *cpu;
   
   /*
    * The cpu_in_sti makes sure that this part of the code is not
    * reentered from a linux interrupt handler which calls __sti().
    */
   
   if(!test_and_set_bit(cpuid = hard_cpu_id(), &global.cpu_in_sti)) {
      hard_sti();
      cpu = processor + cpuid;
      do {
	 
	 /*
	  * First dispatch pending Linux interrupts.
	  */
	 if((irq = global.pending_irqs_l)) {
	    irq = ffnz(irq);
	    hard_cli();
	    if (!(--processor[cpuid].irqs[irq])) {
	       clear_bit(irq, &global.pending_irqs_l);
	    }
	    cpu->intr_flag = 0; //possible race conditon if hard_sti() called before
	    hard_sti();
	    
	    linux_isr[irq](irq, rtai_regs[irq]);			   
	 } /* End if - There are pending linux interrupts. */
	 
	 if((irq = global.pending_irqs_h)) {
	    irq = ffnz(irq);			        
	    hard_cli();
	    if (!(--processor[cpuid].irqs[irq+32])) {
	       clear_bit(irq, &global.pending_irqs_h);
	    }
	    cpu->intr_flag = 0; //possible race conditon if hard_sti() called before
	    hard_sti();
	    
	    irq+=32;
	    linux_isr[irq](irq, rtai_regs[irq]);
	 } /* End if - There are pending linux interrupts. */
		   
	 /*
	  * Now dispatch pending srqs.
	  */
	 rt_spin_lock(&(global.data_lock));
	 if((irq = global.pending_srqs & ~global.activ_srqs)) {
	    irq = ffnz(irq);
	    set_bit(irq, &global.activ_srqs);
	    clear_bit(irq, &global.pending_srqs);
	    rt_spin_unlock(&(global.data_lock));
	    if(sysrq[irq].rtai_handler) {
	       sysrq[irq].rtai_handler();
	    }
	    clear_bit(irq, &global.activ_srqs);
	 } else {
	    rt_spin_unlock(&(global.data_lock));
	 }
	 
      } while(global.pending_irqs_l | global.pending_irqs_h | global.pending_srqs);
      /* End do loop - clear all pending linux interrupts. */
      
      cpu->intr_flag = (1 << IFLAG ) | (1 << cpuid);
      
      //clear_bit(cpuid, &global.cpu_in_sti);
      
      //the above line crashes on steve's board for some reason after
      //a few trips through linux_sti...so we fix it with the
      //SMP incompatible:
      
      global.cpu_in_sti = 0; //not smp safe, but we aren't smp anyway.
      
      return;
   } /* End if - Cannot enter if already called by some one else. */
   processor[cpuid].intr_flag = (1 << IFLAG ) | (1 << cpuid);
   
   return;
} /* End function - linux_sti */


static void linux_restore_flags(unsigned long flags)
{	 
	if(flags) {

		linux_sti();
	} else {
	processor[hard_cpu_id()].intr_flag = 0;
	}
} /* End function - linux_restore_flags */


unsigned long linux_save_flags_and_cli(void)
{
	return(rtai_xchg_u32((void *)(&(processor[hard_cpu_id()].intr_flag)), 0));
} /* End function - linux_save_flags_and_cli */


void rtai_just_copy_back(unsigned long flags, int cpuid)
{
	        processor[cpuid].intr_flag = flags;
} /* End function - rtai_just_copy_back */

asmlinkage unsigned int dispatch_mips_timer_interrupt(int irq,
							struct pt_regs *regs)
{
	unsigned long lflags;
	volatile unsigned int *lflagsp;
	struct cpu_own_status *cpu;

	cpu = processor + hard_cpu_id();
	lflags = rtai_xchg_u32(lflagsp = &cpu->intr_flag, 0);
	rt_spin_lock(&(global.data_lock));
	if(global_irq_handler[irq]) {
		internal_ic_ack_irq[irq](irq);
		rt_spin_unlock(&global.ic_lock);
		rtai_regs[irq] = regs;
		((void (*)(int))global_irq_handler[irq])(irq);
		rt_spin_lock(&global.ic_lock);
	} else {
		ic_ack_irq[irq](irq);
		rtai_regs[irq] = regs;
		cpu->irqs[irq]++;
		set_bit(irq, &(global.pending_irqs_l)); //assumes timer is in l word (7)
	}
	*lflagsp = lflags;
	if(global.used_by_linux & processor[hard_cpu_id()].intr_flag) {
		rt_spin_unlock(&(global.data_lock));
		linux_sti();
		return(1);		
	} else {
		rt_spin_unlock(&(global.data_lock));
		return(0);
	}

} /* End function - dispatch_mips_timer_interrupt */
//static int lame=0;
static asmlinkage unsigned int dispatch_mips_interrupt(int irq,
							struct pt_regs *regs)
{
	unsigned long lflags;
	volatile unsigned int *lflagsp;
	struct cpu_own_status *cpu;

	cpu = processor + hard_cpu_id();
	lflags = rtai_xchg_u32(lflagsp = &cpu->intr_flag, 0);
	rt_spin_lock(&(global.data_lock));

	if(global_irq_handler[irq]) {
		rt_spin_unlock(&global.ic_lock);
		rtai_regs[irq] = regs;
		((void (*)(int))global_irq_handler[irq])(irq);
	        rt_spin_lock(&global.ic_lock);
	} else {
	        ic_ack_irq[irq](irq);
	        rtai_regs[irq] = regs;
	        cpu->irqs[irq]++;
	   set_bit(irq < 32 ? irq : irq-32, irq < 32 ? &(global.pending_irqs_l) : &(global.pending_irqs_h));
	        *lflagsp = lflags;
	        if(global.used_by_linux & processor[hard_cpu_id()].intr_flag) {
		        rt_spin_unlock(&(global.data_lock));
		        linux_sti();
		        return(1);
	        } else {
		        rt_spin_unlock(&(global.data_lock));
		        return(0);
	        }	   
	}
        rt_spin_unlock(&(global.data_lock));
	*lflagsp = lflags;

        return 0;
} /* End function - dispatch_mips_interrupt */


/*
 * RTAI mount-unmount functions to be called from the application to
 * initialise the real time application interface, i.e. this module, only
 * when it is required; so that it can stay asleep when it is not needed
 */

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
#define rtai_mounted 1
#else
static int rtai_mounted;
#ifdef CONFIG_SMP
static spinlock_t rtai_mount_lock = SPIN_LOCK_UNLOCKED;
#endif
#endif


/*
 * Trivial, but we do things carefully, the blocking part is relatively short,
 * should cause no troubles in the transition phase.
 * All the zeroings are strictly not required as mostly related to static data.
 * Done esplicitly for emphasis. Simple, just lock and grab everything from
 * Linux.
 */

void __rt_mount_rtai(void)
{

	static void rt_printk_sysreq_handler(void);
	int i;
	unsigned long flags;

	flags = hard_lock_all();
	rthal.disint = linux_cli;
	rthal.enint = linux_sti;
	rthal.rtai_active = 0xffffffff;
	rthal.getflags = linux_save_flags;
	rthal.setflags = linux_restore_flags;
	rthal.getflags_and_cli = linux_save_flags_and_cli;
	rthal.mips_timer_interrupt = dispatch_mips_timer_interrupt;
	rthal.mips_interrupt = dispatch_mips_interrupt;
	rthal.tsc.tsc = 0;
	hard_unlock_all(flags);
	for(i = 0; i < NR_IRQS; i++) {
		IRQ_DESC[i].handler = &trapped_linux_irq_type;
	}
	sysrq[1].rtai_handler = rt_printk_sysreq_handler;

	printk("\n***** RTAI NEWLY MOUNTED (MOUNT COUNT %d) ******\n\n", rtai_mounted);

} /* End function - __rt_mount_rtai */



/*
 * Simple, now we can simply block other processors and copy original data back
 * to Linux.
 */

void __rt_umount_rtai(void)
{

	int i;
	unsigned long flags;

	flags = hard_lock_all();
	rthal = linux_rthal;
	for(i = 0; i < NR_IRQS; i++) {
		IRQ_DESC[i].handler = linux_irq_desc_handler[i];
	}

	hard_unlock_all(flags);
	printk("\n***** RTAI UNMOUNTED (MOUNT COUNT %d) ******\n\n", rtai_mounted);
} /* End function - __rt_umount_rtai */



#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
void rt_mount_rtai(void) {MOD_INC_USE_COUNT;}
void rt_umount_rtai(void) {MOD_DEC_USE_COUNT;}
#else
void rt_mount_rtai(void)
{

	rt_spin_lock(&rtai_mount_lock);
	rtai_mounted++;
	MOD_INC_USE_COUNT;
	if(rtai_mounted == 1) {
		__rtai_mount_rtai();
	}
	rt_spin_unlock(&rtai_mount_lock);
} /* End function - rt_mount_rtai */



void rt_umount_rtai(void)
{

	rt_spin_lock(&rtai_mount_lock);
	rtai_mounted--;
	MOD_DEC_USE_COUNT;
	if(!rtai_mounted) {
		__rtai_umount_rtai();
	}
	rt_spin_unlock(&rtai_mount_lock);

} /* End function - rt_umount_rtai */

#endif



/*
 * Module parameters to allow frequencies to be overriden via insmod.
 */
static int CpuFreq = CALIBRATED_CPU_FREQ;
MODULE_PARM(CpuFreq, "i");


/*
 * Module initialization and cleanup.
 */

/*
 * Let's prepare our side without any problem, so that there remain just a few
 * things to be done when mounting RTAI. All the zeroings are strictly not
 * required as mostly related to static data. Done esplicitly for emphasis.
 */
static int __init rtai_init(void)
{
   extern unsigned int mips_counter_frequency;
	unsigned int i;

        /*
	 * Passed in CPU frequency overides auto detected Linux value.
	 */
	if(CpuFreq == 0) {
		CpuFreq = mips_counter_frequency;
	}
	tuned.cpu_freq = CpuFreq;
	printk("rtai: Using cpu_freq %d\n", tuned.cpu_freq);

	linux_rthal = rthal;

	global.pending_irqs_l = 0;
   	global.pending_irqs_h = 0;
	global.activ_irqs = 0;
	global.pending_srqs = 0;
	global.activ_srqs = 0;
	global.cpu_in_sti = 0;
	global.used_by_linux = ~(0xFFFFFFFF << smp_num_cpus);
	global.locked_cpus = 0;
	global.hard_nesting = 0;
	spin_lock_init(&(global.data_lock));
	spin_lock_init(&(global.hard_lock));
	spin_lock_init(&global.global.ic_lock);

	for(i = 0; i < NR_RT_CPUS; i++) {
		processor[i].intr_flag = (1 << IFLAG) | (1 << i);
		processor[i].linux_intr_flag = (1 << IFLAG) | (1 << i);
		processor[i].pending_irqs_l = 0;
	        processor[i].pending_irqs_h = 0;
		processor[i].activ_irqs = 0;
	}

	for(i = 0; i < NR_IRQS; i++) {
		linux_irq_desc_handler[i] = IRQ_DESC[i].handler;
		linux_isr[i] = rthal.mips_interrupt; //steve's 2.4.21 kernel works right
	}
   
	for(i = 0; i < NR_GLOBAL_IRQS; i++) {
		global_irq_handler[i] = 0;
		ic_ack_irq[i] = internal_ic_ack_irq[i] =
				linux_irq_desc_handler[i]->ack;
		linux_end_irq[i] = ic_end_irq[i] =
				linux_irq_desc_handler[i]->end;
	}

	/*
	 * Initialize the Linux tick period.
	 */
	linux_hz = (long)(mips_counter_frequency) / cycles_per_jiffy;
	rt_timer_active = 0;

#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
	__rt_mount_rtai();
#endif

#ifdef CONFIG_PROC_FS
	rtai_proc_register();
#endif

	return(0);
} /* End function - rtai_init */


static void __exit rtai_end(void)
{
#ifdef CONFIG_RTAI_MOUNT_ON_LOAD
	__rt_umount_rtai();
#endif

#ifdef CONFIG_PROC_FS
	rtai_proc_unregister();
#endif

	return;
} /* End function - rtai_end */


module_init(rtai_init);
module_exit(rtai_end);
MODULE_DESCRIPTION("RTAI core services module.");
MODULE_AUTHOR("Paolo Mantegazza <mantegazza@aero.polimi.it>");

 
/* ----------------------< proc filesystem section >---------------------- */
#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rtai_proc_root = NULL;  

static int rtai_read_rtai(char *page, char **start, off_t off,
					int count, int *eof, void *data)
{
	
	PROC_PRINT_VARS;
	int i;

	PROC_PRINT("\nRTAI Real Time Kernel, Version: %s\n\n", RTAI_RELEASE);
	PROC_PRINT("    RTAI mount count: %d\n", rtai_mounted);
#ifdef CONFIG_SMP
	PROC_PRINT("    APIC Frequency: %d\n", tuned.apic_freq);
	PROC_PRINT("    APIC Latency: %d ns\n", LATENCY_APIC);
	PROC_PRINT("    APIC Setup: %d ns\n", SETUP_TIME_APIC);
#endif
	PROC_PRINT("\nGlobal irqs used by RTAI: \n");
	for (i = 0; i < NR_GLOBAL_IRQS; i++) {
		if (global_irq_handler[i]) {
			PROC_PRINT("%d ", i);
		}
	}

	PROC_PRINT("\nRTAI sysreqs in use: \n");
	for (i = 0; i < NR_GLOBAL_IRQS; i++) {
		if (sysrq[i].rtai_handler || sysrq[i].user_handler) {
			PROC_PRINT("%d ", i);
		}
	}

	PROC_PRINT("\nMIPS Timer controlled by: %s\n",
				rt_timer_active ? "RTAI" : "Linux");
	if(rt_timer_active) {
		PROC_PRINT("RTAI Tick Frequency: %ldHz\n", rtai_hz);
	} else {
		PROC_PRINT("Linux Tick Frequency: %ldHz\n", linux_hz);
	}

	PROC_PRINT("RTHAL TSC: 0x%.8lx%.8lx\n",
			rthal.tsc.hltsc[1], rthal.tsc.hltsc[0]);

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
/* ------------------< end of proc filesystem section >------------------ */

/* ---------------------------< Timer section >--------------------------- */

/********** SOME TIMER FUNCTIONS TO BE LIKELY NEVER PUT ELSWHERE *************/
 
/*
 * Real time timers. No oneshot, and related timer programming, calibration.
 * Use the utility module. It is also been decided that this stuff has to
 * stay here.
 */

struct calibration_data tuned;
struct rt_times rt_times = {0};



/*
 * Restore the MIPS timer to its Linux kernel value.
 */
void restore_mips_timer(unsigned long linux_tick)
{

	unsigned long flags;
	unsigned long cp0_compare = 0;  /* Counter value at next timer irq */

	flags = hard_lock_all();
	cp0_compare = ((unsigned long)read_c0_count());
	cp0_compare += linux_tick;
	write_c0_compare(cp0_compare);
	hard_unlock_all(flags);

}  /* End function - restore_mips_timer */


int rt_request_timer(void (*handler)(void), unsigned int tick, int apic)
{

	unsigned long flags;
	int r_c = 0;
	RTIME t;

	flags = hard_lock_all();

	t = rdtsc();

	rt_times.linux_tick = cycles_per_jiffy;
	rt_times.periodic_tick = tick > 0 && tick < cycles_per_jiffy ? tick :
							rt_times.linux_tick;
	rt_times.tick_time = t;
	rt_times.intr_time = rt_times.tick_time + rt_times.periodic_tick;
	rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
	rt_free_global_irq(IRQ_TIMER);
	if(rt_request_global_irq(IRQ_TIMER, handler) < 0) {
		r_c = -EINVAL;
		goto set_timer_exit;
	}

	linux_isr[IRQ_TIMER] = (unsigned int (*)(int,struct pt_regs *))rthal.linux_soft_mips_timer_intr;
	rt_set_timer_delay(rt_times.periodic_tick);
	if(rt_times.periodic_tick != 0) {
 		rtai_hz = tuned.cpu_freq / rt_times.periodic_tick;
	}
	rt_timer_active = 1;

set_timer_exit:
	hard_unlock_all(flags);
	return(r_c);

} /* End function - rt_request_timer */


int rt_free_timer(void)
{

	unsigned long flags;
	int r_c = 0;

	flags = hard_lock_all();
	clear_c0_status(IE_IRQ5);
	rt_timer_active = 0;
	linux_isr[IRQ_TIMER] = linux_rthal.mips_interrupt; //since handler set up through linux, don't do mips_timer_interrupt
	if(rt_free_global_irq(IRQ_TIMER) < 0) {
		r_c = -EINVAL;
	}

	restore_mips_timer(cycles_per_jiffy);
	set_c0_status(IE_IRQ5);
	processor[hard_cpu_id()].irqs[IRQ_TIMER] = 0;
	clear_bit(IRQ_TIMER, &global.pending_irqs_l);
	hard_unlock_all(flags);
	return(r_c);

}  /* End function - rt_free_timer */


/* ------------------------< End of timer section >----------------------- */

/* ---------------------------< printk section >--------------------------- */

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
		if((c->flags & CON_ENABLED) && c->write) {
			c->write(c, display, len);
		}
		c = c->next;
	}
	rt_spin_unlock_irqrestore(flags, &display_lock);

	return(len);

}  /* End function - rtai_print_to_screen */


/*
 * rt_printk.c, hacked from linux/kernel/printk.c.
 *
 * Modified for RT support, David Schleef.
 *
 * Adapted to RTAI, and restyled his way by Paolo Mantegazza. Now it has been
 * taken away from the fifos module and has become an integral part of the
 * basic RTAI module.
 */
 
#define PRINTK_BUF_LEN  (4096*2) /* Some programs generate much output. PC */
#define TEMP_BUF_LEN    (256)
 
static char rt_printk_buf[PRINTK_BUF_LEN];
static int buf_front, buf_back;
 
static char buf[TEMP_BUF_LEN];


int rt_printk(const char *fmt, ...)
{

	static spinlock_t display_lock = SPIN_LOCK_UNLOCKED;
	va_list args;
	int len, i;
	unsigned long flags, lflags; //steve added lflags at othersteve's suggestion

        lflags = linux_save_flags_and_cli();
	flags = rt_spin_lock_irqsave(&display_lock);
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);
	if(buf_front + len >= PRINTK_BUF_LEN) {
		i = PRINTK_BUF_LEN - buf_front;
		memcpy(rt_printk_buf + buf_front, buf, i);
		memcpy(rt_printk_buf, buf + i, len - i);
		buf_front = len - i;
	} else {
		memcpy(rt_printk_buf + buf_front, buf, len);
		buf_front += len;
	}
	rt_spin_unlock_irqrestore(flags, &display_lock);
        rtai_just_copy_back(lflags, hard_cpu_id());
	rt_pend_linux_srq(1);

	return len;
}  /* End function - rt_printk */


 
static void rt_printk_sysreq_handler(void)
{

	int tmp;

	while(1) {
		tmp = buf_front;
		if(buf_back  > tmp) {
			printk("%.*s", PRINTK_BUF_LEN - buf_back,
						rt_printk_buf + buf_back);
			buf_back = 0;
		}
		if(buf_back == tmp) {
			break;
		}
		printk("%.*s", tmp - buf_back, rt_printk_buf + buf_back);
		buf_back = tmp;
	}
}  /* End function - rt_printk_sysreq_handler */

/* ------------------------< End of printk section >----------------------- */

/* ----------------------------< End of File >--------------------------- */

