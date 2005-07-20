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
 * @defgroup hal RTAI services functions.
 *
 * This module defines some functions that can be used by RTAI tasks, for
 * managing interrupts and communication services with Linux processes.
 *
 *@{*/


#define DONT_DISPATCH_CORE_IRQS  1
#define CHECK_STACK_IN_IRQ  0

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <asm/system.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/desc.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#ifdef CONFIG_X86_LOCAL_APIC
#include <asm/fixmap.h>
#include <asm/bitops.h>
#include <asm/mpspec.h>
#ifdef CONFIG_X86_IO_APIC
#include <asm/io_apic.h>
#endif /* CONFIG_X86_IO_APIC */
#include <asm/apic.h>
#endif /* CONFIG_X86_LOCAL_APIC */
#define __RTAI_HAL__
#include <asm/rtai_hal.h>
#include <asm/rtai_lxrt.h>
#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <rtai_proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

MODULE_LICENSE("GPL");

static unsigned long rtai_cpufreq_arg = RTAI_CALIBRATED_CPU_FREQ;
MODULE_PARM(rtai_cpufreq_arg,"i");

#define RTAI_NR_IRQS  IPIPE_NR_XIRQS

#ifdef CONFIG_X86_LOCAL_APIC

static unsigned long rtai_apicfreq_arg = RTAI_CALIBRATED_APIC_FREQ;

MODULE_PARM(rtai_apicfreq_arg, "i");

static inline void rtai_setup_periodic_apic (unsigned count, unsigned vector)
{
	apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, APIC_LVT_TIMER_PERIODIC | vector);
	apic_read(APIC_TMICT);
	apic_write(APIC_TMICT, count);
}

static inline void rtai_setup_oneshot_apic (unsigned count, unsigned vector)
{
	apic_read(APIC_LVTT);
	apic_write(APIC_LVTT, vector);
	apic_read(APIC_TMICT);
	apic_write(APIC_TMICT, count);
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,9)
#define __ack_APIC_irq  ack_APIC_irq
#endif

#else /* !CONFIG_X86_LOCAL_APIC */

#define rtai_setup_periodic_apic(count, vector)

#define rtai_setup_oneshot_apic(count, vector)

#define __ack_APIC_irq()

#endif /* CONFIG_X86_LOCAL_APIC */

struct { volatile int locked, rqsted; } rt_scheduling[RTAI_NR_CPUS];

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
static void (*rtai_isr_hook)(int cpuid);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

extern struct desc_struct idt_table[];

adomain_t rtai_domain;

struct {
	int (*handler)(unsigned irq, void *cookie);
	void *cookie;
	int retmode;
	int cpumask;
} rtai_realtime_irq[RTAI_NR_IRQS];

static struct {
	unsigned long flags;
	int count;
} rtai_linux_irq[RTAI_NR_IRQS];

static struct {
	void (*k_handler)(void);
	long long (*u_handler)(unsigned);
	unsigned label;
} rtai_sysreq_table[RTAI_NR_SRQS];

static unsigned rtai_sysreq_virq;

static unsigned long rtai_sysreq_map = 1; /* srq 0 is reserved */

static unsigned long rtai_sysreq_pending;

static unsigned long rtai_sysreq_running;

static spinlock_t rtai_lsrq_lock = SPIN_LOCK_UNLOCKED;

static volatile int rtai_sync_level;

static atomic_t rtai_sync_count = ATOMIC_INIT(1);

static int rtai_last_8254_counter2;

static RTIME rtai_ts_8254;

static struct desc_struct rtai_sysvec;

static RT_TRAP_HANDLER rtai_trap_handler;

struct rt_times rt_times;

struct rt_times rt_smp_times[RTAI_NR_CPUS];

struct rtai_switch_data rtai_linux_context[RTAI_NR_CPUS];

struct calibration_data rtai_tunables;

volatile unsigned long rtai_cpu_realtime;

volatile unsigned long rtai_cpu_lock;

unsigned long rtai_critical_enter (void (*synch)(void))
{
	unsigned long flags;

	flags = adeos_critical_enter(synch);
	if (atomic_dec_and_test(&rtai_sync_count)) {
		rtai_sync_level = 0;
	} else if (synch != NULL) {
		printk(KERN_INFO "RTAI[hal]: warning: nested sync will fail.\n");
	}
	return flags;
}

void rtai_critical_exit (unsigned long flags)
{
	atomic_inc(&rtai_sync_count);
	adeos_critical_exit(flags);
}

static unsigned long IsolCpusMask = 0;
MODULE_PARM(IsolCpusMask, "i");

int rt_request_irq (unsigned irq, int (*handler)(unsigned irq, void *cookie), void *cookie, int retmode)
{
	unsigned long flags;

	if (handler == NULL || irq >= RTAI_NR_IRQS) {
		return -EINVAL;
	}
	if (rtai_realtime_irq[irq].handler != NULL) {
		return -EBUSY;
	}
	flags = rtai_critical_enter(NULL);
	rtai_realtime_irq[irq].handler = (void *)handler;
	rtai_realtime_irq[irq].cookie  = cookie;
	rtai_realtime_irq[irq].retmode = retmode ? 1 : 0;
	rtai_critical_exit(flags);
	if (IsolCpusMask && irq < IPIPE_NR_XIRQS) {
		rtai_realtime_irq[irq].cpumask = rt_assign_irq_to_cpu(irq, IsolCpusMask);
	}
	return 0;
}

int rt_release_irq (unsigned irq)
{
	unsigned long flags;
	if (irq >= RTAI_NR_IRQS || !rtai_realtime_irq[irq].handler) {
		return -EINVAL;
	}
	flags = rtai_critical_enter(NULL);
	rtai_realtime_irq[irq].handler = NULL;
	rtai_critical_exit(flags);
	if (IsolCpusMask && irq < IPIPE_NR_XIRQS) {
		rt_assign_irq_to_cpu(irq, rtai_realtime_irq[irq].cpumask);
	}
	return 0;
}

void rt_set_irq_cookie (unsigned irq, void *cookie)
{
	if (irq < RTAI_NR_IRQS) {
		rtai_realtime_irq[irq].cookie = cookie;
	}
}

void rt_set_irq_retmode (unsigned irq, int retmode)
{
	if (irq < RTAI_NR_IRQS) {
		rtai_realtime_irq[irq].retmode = retmode ? 1 : 0;
	}
}

extern unsigned long io_apic_irqs;

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,9)

#define rtai_irq_desc(irq) (irq_desc[irq].handler)
#define BEGIN_PIC()
#define END_PIC()
#undef __adeos_lock_irq
#undef __adeos_unlock_irq
#define __adeos_lock_irq(x, y, z)
#define __adeos_unlock_irq(x, y)

#else

extern struct hw_interrupt_type __adeos_std_irq_dtype[];
#define rtai_irq_desc(irq) (&__adeos_std_irq_dtype[irq])

#define BEGIN_PIC() \
do { \
        unsigned long flags, pflags, cpuid; \
	rtai_save_flags_and_cli(flags); \
	cpuid = rtai_cpuid(); \
	pflags = xchg(&adp_root->cpudata[cpuid].status, 1 << IPIPE_STALL_FLAG); \
	rtai_save_and_lock_preempt_count()

#define END_PIC() \
	rtai_restore_preempt_count(); \
	adp_root->cpudata[cpuid].status = pflags; \
	rtai_restore_flags(flags); \
} while (0)

#endif

/**
 * start and initialize the PIC to accept interrupt request irq.
 *
 * The above function allow you to manipulate the PIC at hand, but you must
 * know what you are doing. Such a duty does not pertain to this manual and
 * you should refer to your PIC datasheet.
 *
 * Note that Linux has the same functions, but they must be used only for its
 * interrupts. Only the above ones can be safely used in real time handlers.
 *
 * It must also be remarked that when you install a real time interrupt handler,
 * RTAI already calls either rt_mask_and_ack_irq(), for level triggered
 * interrupts, or rt_ack_irq(), for edge triggered interrupts, before passing
 * control to you interrupt handler. hus generally you should just call
 * rt_unmask_irq() at due time, for level triggered interrupts, while nothing
 * should be done for edge triggered ones. Recall that in the latter case you
 * allow also any new interrupts on the same request as soon as you enable
 * interrupts at the CPU level.
 * 
 * Often some of the above functions do equivalent things. Once more there is no
 * way of doing it right except by knowing the hardware you are manipulating.
 * Furthermore you must also remember that when you install a hard real time
 * handler the related interrupt is usually disabled, unless you are overtaking
 * one already owned by Linux which has been enabled by it.   Recall that if
 * have done it right, and interrupts do not show up, it is likely you have just
 * to rt_enable_irq() your irq.
 */
unsigned rt_startup_irq (unsigned irq)
{
        int retval;

	BEGIN_PIC();
	__adeos_unlock_irq(adp_root, irq);
	retval = rtai_irq_desc(irq)->startup(irq);
	END_PIC();
        return retval;
}

/**
 * Shut down an IRQ source.
 *
 * No further interrupt request irq can be accepted.
 *
 * The above function allow you to manipulate the PIC at hand, but you must
 * know what you are doing. Such a duty does not pertain to this manual and
 * you should refer to your PIC datasheet.
 *
 * Note that Linux has the same functions, but they must be used only for its
 * interrupts. Only the above ones can be safely used in real time handlers.
 *
 * It must also be remarked that when you install a real time interrupt handler,
 * RTAI already calls either rt_mask_and_ack_irq(), for level triggered
 * interrupts, or rt_ack_irq(), for edge triggered interrupts, before passing
 * control to you interrupt handler. hus generally you should just call
 * rt_unmask_irq() at due time, for level triggered interrupts, while nothing
 * should be done for edge triggered ones. Recall that in the latter case you
 * allow also any new interrupts on the same request as soon as you enable
 * interrupts at the CPU level.
 * 
 * Often some of the above functions do equivalent things. Once more there is no
 * way of doing it right except by knowing the hardware you are manipulating.
 * Furthermore you must also remember that when you install a hard real time
 * handler the related interrupt is usually disabled, unless you are overtaking
 * one already owned by Linux which has been enabled by it.   Recall that if
 * have done it right, and interrupts do not show up, it is likely you have just
 * to rt_enable_irq() your irq.
 */
void rt_shutdown_irq (unsigned irq)
{
	BEGIN_PIC();
	rtai_irq_desc(irq)->shutdown(irq);
	__adeos_clear_irq(adp_root, irq);
	END_PIC();
}

static inline void _rt_enable_irq (unsigned irq)
{
	BEGIN_PIC();
	__adeos_unlock_irq(adp_root, irq);
	rtai_irq_desc(irq)->enable(irq);
	END_PIC();
}

/**
 * Enable an IRQ source.
 *
 * The above function allow you to manipulate the PIC at hand, but you must
 * know what you are doing. Such a duty does not pertain to this manual and
 * you should refer to your PIC datasheet.
 *
 * Note that Linux has the same functions, but they must be used only for its
 * interrupts. Only the above ones can be safely used in real time handlers.
 *
 * It must also be remarked that when you install a real time interrupt handler,
 * RTAI already calls either rt_mask_and_ack_irq(), for level triggered
 * interrupts, or rt_ack_irq(), for edge triggered interrupts, before passing
 * control to you interrupt handler. hus generally you should just call
 * rt_unmask_irq() at due time, for level triggered interrupts, while nothing
 * should be done for edge triggered ones. Recall that in the latter case you
 * allow also any new interrupts on the same request as soon as you enable
 * interrupts at the CPU level.
 * 
 * Often some of the above functions do equivalent things. Once more there is no
 * way of doing it right except by knowing the hardware you are manipulating.
 * Furthermore you must also remember that when you install a hard real time
 * handler the related interrupt is usually disabled, unless you are overtaking
 * one already owned by Linux which has been enabled by it.   Recall that if
 * have done it right, and interrupts do not show up, it is likely you have just
 * to rt_enable_irq() your irq.
 */
void rt_enable_irq (unsigned irq)
{
	_rt_enable_irq(irq);
}

/**
 * Disable an IRQ source.
 *
 * The above function allow you to manipulate the PIC at hand, but you must
 * know what you are doing. Such a duty does not pertain to this manual and
 * you should refer to your PIC datasheet.
 *
 * Note that Linux has the same functions, but they must be used only for its
 * interrupts. Only the above ones can be safely used in real time handlers.
 *
 * It must also be remarked that when you install a real time interrupt handler,
 * RTAI already calls either rt_mask_and_ack_irq(), for level triggered
 * interrupts, or rt_ack_irq(), for edge triggered interrupts, before passing
 * control to you interrupt handler. hus generally you should just call
 * rt_unmask_irq() at due time, for level triggered interrupts, while nothing
 * should be done for edge triggered ones. Recall that in the latter case you
 * allow also any new interrupts on the same request as soon as you enable
 * interrupts at the CPU level.
 * 
 * Often some of the above functions do equivalent things. Once more there is no
 * way of doing it right except by knowing the hardware you are manipulating.
 * Furthermore you must also remember that when you install a hard real time
 * handler the related interrupt is usually disabled, unless you are overtaking
 * one already owned by Linux which has been enabled by it.   Recall that if
 * have done it right, and interrupts do not show up, it is likely you have just
 * to rt_enable_irq() your irq.
 */
void rt_disable_irq (unsigned irq)
{
	BEGIN_PIC();
	rtai_irq_desc(irq)->disable(irq);
	__adeos_lock_irq(adp_root, cpuid, irq);
	END_PIC();
}

/**
 * Mask and acknowledge and IRQ source.
 *
 * No  * other interrupts can be accepted, once also the CPU will enable
 * interrupts, which ones depends on the PIC at hand and on how it is
 * programmed.
 *
 * The above function allow you to manipulate the PIC at hand, but you must
 * know what you are doing. Such a duty does not pertain to this manual and
 * you should refer to your PIC datasheet.
 *
 * Note that Linux has the same functions, but they must be used only for its
 * interrupts. Only the above ones can be safely used in real time handlers.
 *
 * It must also be remarked that when you install a real time interrupt handler,
 * RTAI already calls either rt_mask_and_ack_irq(), for level triggered
 * interrupts, or rt_ack_irq(), for edge triggered interrupts, before passing
 * control to you interrupt handler. hus generally you should just call
 * rt_unmask_irq() at due time, for level triggered interrupts, while nothing
 * should be done for edge triggered ones. Recall that in the latter case you
 * allow also any new interrupts on the same request as soon as you enable
 * interrupts at the CPU level.
 * 
 * Often some of the above functions do equivalent things. Once more there is no
 * way of doing it right except by knowing the hardware you are manipulating.
 * Furthermore you must also remember that when you install a hard real time
 * handler the related interrupt is usually disabled, unless you are overtaking
 * one already owned by Linux which has been enabled by it.   Recall that if
 * have done it right, and interrupts do not show up, it is likely you have just
 * to rt_enable_irq() your irq.
 */
void rt_mask_and_ack_irq (unsigned irq)
{
        irq_desc[irq].handler->ack(irq);
}

static inline void _rt_end_irq (unsigned irq)
{
	BEGIN_PIC();
	if (
#ifdef CONFIG_X86_IO_APIC
	    !IO_APIC_IRQ(irq) ||
#endif /* CONFIG_X86_IO_APIC */
	    !(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		__adeos_unlock_irq(adp_root, irq);
	}
	rtai_irq_desc(irq)->end(irq);
	END_PIC();
}

/**
 * Unmask and IRQ source.
 *
 * The related request can then interrupt the CPU again, provided it has also
 * been acknowledged.
 *
 * The above function allow you to manipulate the PIC at hand, but you must
 * know what you are doing. Such a duty does not pertain to this manual and
 * you should refer to your PIC datasheet.
 *
 * Note that Linux has the same functions, but they must be used only for its
 * interrupts. Only the above ones can be safely used in real time handlers.
 *
 * It must also be remarked that when you install a real time interrupt handler,
 * RTAI already calls either rt_mask_and_ack_irq(), for level triggered
 * interrupts, or rt_ack_irq(), for edge triggered interrupts, before passing
 * control to you interrupt handler. hus generally you should just call
 * rt_unmask_irq() at due time, for level triggered interrupts, while nothing
 * should be done for edge triggered ones. Recall that in the latter case you
 * allow also any new interrupts on the same request as soon as you enable
 * interrupts at the CPU level.
 * 
 * Often some of the above functions do equivalent things. Once more there is no
 * way of doing it right except by knowing the hardware you are manipulating.
 * Furthermore you must also remember that when you install a hard real time
 * handler the related interrupt is usually disabled, unless you are overtaking
 * one already owned by Linux which has been enabled by it.   Recall that if
 * have done it right, and interrupts do not show up, it is likely you have just
 * to rt_enable_irq() your irq.
 */
void rt_unmask_irq (unsigned irq)
{
	_rt_end_irq(irq);
}

/**
 * Acknowledge an IRQ source.
 *
 * The related request can then interrupt the CPU again, provided it has not
 * been masked.
 *
 * The above function allow you to manipulate the PIC at hand, but you must
 * know what you are doing. Such a duty does not pertain to this manual and
 * you should refer to your PIC datasheet.
 *
 * Note that Linux has the same functions, but they must be used only for its
 * interrupts. Only the above ones can be safely used in real time handlers.
 *
 * It must also be remarked that when you install a real time interrupt handler,
 * RTAI already calls either rt_mask_and_ack_irq(), for level triggered
 * interrupts, or rt_ack_irq(), for edge triggered interrupts, before passing
 * control to you interrupt handler. hus generally you should just call
 * rt_unmask_irq() at due time, for level triggered interrupts, while nothing
 * should be done for edge triggered ones. Recall that in the latter case you
 * allow also any new interrupts on the same request as soon as you enable
 * interrupts at the CPU level.
 * 
 * Often some of the above functions do equivalent things. Once more there is no
 * way of doing it right except by knowing the hardware you are manipulating.
 * Furthermore you must also remember that when you install a hard real time
 * handler the related interrupt is usually disabled, unless you are overtaking
 * one already owned by Linux which has been enabled by it.   Recall that if
 * have done it right, and interrupts do not show up, it is likely you have just
 * to rt_enable_irq() your irq.
 */
void rt_ack_irq (unsigned irq)
{
	_rt_enable_irq(irq);
}

void rt_end_irq (unsigned irq)
{
	_rt_end_irq(irq);
}

/**
 * Install shared Linux interrupt handler.
 *
 * rt_request_linux_irq installs function @a handler as a standard Linux
 * interrupt service routine for IRQ level @a irq forcing Linux to share the IRQ
 * with other interrupt handlers, even if it does not want. The handler is
 * appended to any already existing Linux handler for the same irq and is run by
 * Linux irq as any of its handler. In this way a real time application can
 * monitor Linux interrupts handling at its will. The handler appears in
 * /proc/interrupts.
 *
 * @param handler pointer on the interrupt service routine to be installed.
 *
 * @param name is a name for /proc/interrupts.
 *
 * @param dev_id is to pass to the interrupt handler, in the same way as the
 * standard Linux irq request call.
 *
 * The interrupt service routine can be uninstalled with rt_free_linux_irq().
 *
 * @retval 0 on success.
 * @retval EINVAL if @a irq is not a valid IRQ number or handler is @c NULL.
 * @retval EBUSY if there is already a handler of interrupt @a irq.
 */
int rt_request_linux_irq (unsigned irq, irqreturn_t (*handler)(int irq, void *dev_id, struct pt_regs *regs), char *name, void *dev_id)
{
	unsigned long flags;

	if (irq >= RTAI_NR_IRQS || !handler) {
		return -EINVAL;
	}

	rtai_save_flags_and_cli(flags);
	spin_lock(&irq_desc[irq].lock);
	if (rtai_linux_irq[irq].count++ == 0 && irq_desc[irq].action) {
		rtai_linux_irq[irq].flags = irq_desc[irq].action->flags;
		irq_desc[irq].action->flags |= SA_SHIRQ;
	}
	spin_unlock(&irq_desc[irq].lock);
	rtai_restore_flags(flags);

	request_irq(irq, handler, SA_SHIRQ, name, dev_id);

	return 0;
}

/**
 * Uninstall shared Linux interrupt handler.
 *
 * @param dev_id is to pass to the interrupt handler, in the same way as the
 * standard Linux irq request call.
 *
 * @param irq is the IRQ level of the interrupt handler to be freed.
 *
 * @retval 0 on success.
 * @retval EINVAL if @a irq is not a valid IRQ number.
 */
int rt_free_linux_irq (unsigned irq, void *dev_id)
{
	unsigned long flags;

	if (irq >= RTAI_NR_IRQS || rtai_linux_irq[irq].count == 0) {
		return -EINVAL;
	}

	rtai_save_flags_and_cli(flags);
	free_irq(irq,dev_id);
	spin_lock(&irq_desc[irq].lock);
	if (--rtai_linux_irq[irq].count == 0 && irq_desc[irq].action) {
		irq_desc[irq].action->flags = rtai_linux_irq[irq].flags;
	}
	spin_unlock(&irq_desc[irq].lock);
	rtai_restore_flags(flags);

	return 0;
}

volatile unsigned long adeos_pended;

/**
 * Pend an IRQ to Linux.
 *
 * rt_pend_linux_irq appends a Linux interrupt irq for processing in Linux IRQ
 * mode, i.e. with hardware interrupts fully enabled.
 *
 * @note rt_pend_linux_irq does not perform any check on @a irq.
 */
void rt_pend_linux_irq (unsigned irq)
{
	unsigned long flags;
	rtai_save_flags_and_cli(flags);
	adeos_pend_uncond(irq, rtai_cpuid());
	rtai_restore_flags(flags);
}

/**
 * Install a system request handler
 *
 * rt_request_srq installs a two way RTAI system request (srq) by assigning
 * @a u_handler, a function to be used when a user calls srq from user space,
 * and @a k_handler, the function to be called in kernel space following its
 * activation by a call to rt_pend_linux_srq(). @a k_handler is in practice
 * used to request a service from the kernel. In fact Linux system requests
 * cannot be used safely from RTAI so you can setup a handler that receives real
 * time requests and safely executes them when Linux is running.
 *
 * @param u_handler can be used to effectively enter kernel space without the
 * overhead and clumsiness of standard Unix/Linux protocols.   This is very
 * flexible service that allows you to personalize your use of  RTAI.
 *
 * @return the number of the assigned system request on success.
 * @retval EINVAL if @a k_handler is @c NULL.
 * @retval EBUSY if no free srq slot is available.
 */
int rt_request_srq (unsigned label, void (*k_handler)(void), long long (*u_handler)(unsigned))
{
	unsigned long flags;
	int srq;

	if (k_handler == NULL) {
		return -EINVAL;
	}

	rtai_save_flags_and_cli(flags);
	if (rtai_sysreq_map != ~0) {
		set_bit(srq  = ffz(rtai_sysreq_map), &rtai_sysreq_map);
		rtai_sysreq_table[srq].k_handler = k_handler;
		rtai_sysreq_table[srq].u_handler = u_handler;
		rtai_sysreq_table[srq].label = label;
	} else {
		srq = -EBUSY;
	}
	rtai_restore_flags(flags);

	return srq;
}

/**
 * Uninstall a system request handler
 *
 * rt_free_srq uninstalls the specified system call @a srq, returned by
 * installing the related handler with a previous call to rt_request_srq().
 *
 * @retval EINVAL if @a srq is invalid.
 */
int rt_free_srq (unsigned srq)
{
	return  (srq < 1 || srq >= RTAI_NR_SRQS || !test_and_clear_bit(srq, &rtai_sysreq_map)) ? -EINVAL : 0;
}

/**
 * Append a Linux IRQ.
 *
 * rt_pend_linux_srq appends a system call request srq to be used as a service
 * request to the Linux kernel.
 *
 * @param srq is the value returned by rt_request_srq.
 *
 * @note rt_pend_linux_srq does not perform any check on irq.
 */
void rt_pend_linux_srq (unsigned srq)
{
	if (srq > 0 && srq < RTAI_NR_SRQS) {
		unsigned long flags;
		set_bit(srq, &rtai_sysreq_pending);
		rtai_save_flags_and_cli(flags);
		adeos_pend_uncond(rtai_sysreq_virq, rtai_cpuid());
		rtai_restore_flags(flags);
	}
}

#ifdef CONFIG_X86_LOCAL_APIC

irqreturn_t rtai_broadcast_to_local_timers (int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;

	rtai_hw_save_flags_and_cli(flags);
	apic_wait_icr_idle();
	apic_write_around(APIC_ICR,APIC_DM_FIXED|APIC_DEST_ALLINC|LOCAL_TIMER_VECTOR);
	rtai_hw_restore_flags(flags);

	return RTAI_LINUX_IRQ_HANDLED;
} 

#define REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS()  rt_request_linux_irq(RTAI_TIMER_8254_IRQ, &rtai_broadcast_to_local_timers, "rtai_broadcast", &rtai_broadcast_to_local_timers)

#define FREE_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS()     rt_free_linux_irq(RTAI_TIMER_8254_IRQ, &rtai_broadcast_to_local_timers)

#else

irqreturn_t rtai_broadcast_to_local_timers (int irq, void *dev_id, struct pt_regs *regs)
{
	return RTAI_LINUX_IRQ_HANDLED;
} 

#define REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS()  0

#define FREE_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();

#endif

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
#define RTAI_SCHED_ISR_LOCK() \
	do { \
		if (!rt_scheduling[cpuid].locked++) { \
			rt_scheduling[cpuid].rqsted = 0; \
		} \
	} while (0)
#define RTAI_SCHED_ISR_UNLOCK() \
	do { \
		rtai_cli(); \
		if (rt_scheduling[cpuid].locked && !(--rt_scheduling[cpuid].locked)) { \
			if (rt_scheduling[cpuid].rqsted > 0 && rtai_isr_hook) { \
				rtai_isr_hook(cpuid); \
        		} \
		} \
	} while (0)
#else  /* !CONFIG_RTAI_SCHED_ISR_LOCK */
#define RTAI_SCHED_ISR_LOCK() \
	do {             } while (0)
#define RTAI_SCHED_ISR_UNLOCK() \
	do { rtai_cli(); } while (0)
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
#define ADEOS_TICK_REGS __adeos_tick_regs[cpuid]
#else
#define ADEOS_TICK_REGS __adeos_tick_regs
#endif

#if DONT_DISPATCH_CORE_IRQS

#define SET_INTR_GATE(vector, handler, save) \
	do { save = rtai_set_gate_vector(vector, 14, 0, handler); } while (0)
#define RESET_INTR_GATE(vector, save) \
	do { rtai_reset_gate_vector(vector, save); } while (0)

#ifdef CONFIG_SMP
int _rtai_sched_on_ipi_handler(void)
{
	unsigned long cpuid = rtai_cpuid();
	rt_switch_to_real_time(cpuid);
	RTAI_SCHED_ISR_LOCK();
	__ack_APIC_irq();
//	adp_root->irqs[SCHED_IPI].acknowledge(SCHED_IPI);
	((void (*)(void))rtai_realtime_irq[SCHED_IPI].handler)();
	RTAI_SCHED_ISR_UNLOCK();
	rt_switch_to_linux(cpuid);
	if (!test_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status)) {
		rtai_sti();
		if (adp_root->cpudata[cpuid].irq_pending_hi != 0) {
			rtai_cli();
			__adeos_sync_stage(IPIPE_IRQMASK_ANY);
		}
#if defined(CONFIG_SMP) &&  LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		set_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status);
#endif
		return 1;
        }
	return 0;
}

void rtai_sched_on_ipi_handler (void);
	__asm__ ( \
        "\n" __ALIGN_STR"\n\t" \
        SYMBOL_NAME_STR(rtai_sched_on_ipi_handler) ":\n\t" \
        "pushl $-1\n\t" \
	"cld\n\t" \
        "pushl %es\n\t" \
        "pushl %ds\n\t" \
        "pushl %eax\n\t" \
        "pushl %ebp\n\t" \
        "pushl %edi\n\t" \
        "pushl %esi\n\t" \
        "pushl %edx\n\t" \
        "pushl %ecx\n\t" \
        "pushl %ebx\n\t" \
	__LXRT_GET_DATASEG(ecx) \
        "movl %ecx, %ds\n\t" \
        "movl %ecx, %es\n\t" \
        "call "SYMBOL_NAME_STR(_rtai_sched_on_ipi_handler)"\n\t" \
        "testl %eax,%eax\n\t" \
        "jnz  ret_from_intr\n\t" \
        "popl %ebx\n\t" \
        "popl %ecx\n\t" \
        "popl %edx\n\t" \
        "popl %esi\n\t" \
        "popl %edi\n\t" \
        "popl %ebp\n\t" \
        "popl %eax\n\t" \
        "popl %ds\n\t" \
        "popl %es\n\t" \
        "addl $4,%esp\n\t" \
        "iret");

static struct desc_struct rtai_sched_on_ipi_sysvec;

void rt_set_sched_ipi_gate(void)
{
	SET_INTR_GATE(SCHED_VECTOR, rtai_sched_on_ipi_handler, rtai_sched_on_ipi_sysvec);
}

void rt_reset_sched_ipi_gate(void)
{
	RESET_INTR_GATE(SCHED_VECTOR, rtai_sched_on_ipi_sysvec);
}
#endif

int _rtai_apic_timer_handler(void)
{
	unsigned long cpuid = rtai_cpuid();
	rt_switch_to_real_time(cpuid);
	RTAI_SCHED_ISR_LOCK();
	__ack_APIC_irq();
//	adp_root->irqs[RTAI_APIC_TIMER_IPI].acknowledge(RTAI_APIC_TIMER_IPI);
	((void (*)(void))rtai_realtime_irq[RTAI_APIC_TIMER_IPI].handler)();
	RTAI_SCHED_ISR_UNLOCK();
	rt_switch_to_linux(cpuid);
	if (!test_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status)) {
		rtai_sti();
		if (adp_root->cpudata[cpuid].irq_pending_hi != 0) {
			rtai_cli();
			__adeos_sync_stage(IPIPE_IRQMASK_ANY);
		}
#if defined(CONFIG_SMP) &&  LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		set_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status);
#endif
		return 1;
        }
	return 0;
}

void rtai_apic_timer_handler (void);
	__asm__ ( \
        "\n" __ALIGN_STR"\n\t" \
        SYMBOL_NAME_STR(rtai_apic_timer_handler) ":\n\t" \
        "pushl $-1\n\t" \
	"cld\n\t" \
        "pushl %es\n\t" \
        "pushl %ds\n\t" \
        "pushl %eax\n\t" \
        "pushl %ebp\n\t" \
        "pushl %edi\n\t" \
        "pushl %esi\n\t" \
        "pushl %edx\n\t" \
        "pushl %ecx\n\t" \
        "pushl %ebx\n\t" \
	__LXRT_GET_DATASEG(ecx) \
        "movl %ecx, %ds\n\t" \
        "movl %ecx, %es\n\t" \
        "call "SYMBOL_NAME_STR(_rtai_apic_timer_handler)"\n\t" \
        "testl %eax,%eax\n\t" \
        "jnz  ret_from_intr\n\t" \
        "popl %ebx\n\t" \
        "popl %ecx\n\t" \
        "popl %edx\n\t" \
        "popl %esi\n\t" \
        "popl %edi\n\t" \
        "popl %ebp\n\t" \
        "popl %eax\n\t" \
        "popl %ds\n\t" \
        "popl %es\n\t" \
        "addl $4,%esp\n\t" \
        "iret");

static struct desc_struct rtai_apic_timer_sysvec;

/* this can be a prototy for the cse of a handler pending something for Linux */
int _rtai_8254_timer_handler(struct pt_regs regs)
{
	unsigned long cpuid = rtai_cpuid();
	rt_switch_to_real_time(cpuid);
	RTAI_SCHED_ISR_LOCK();
	adp_root->irqs[RTAI_TIMER_8254_IRQ].acknowledge(RTAI_TIMER_8254_IRQ);
	((void (*)(void))rtai_realtime_irq[RTAI_TIMER_8254_IRQ].handler)();
	RTAI_SCHED_ISR_UNLOCK();
	rt_switch_to_linux(cpuid);
	if (test_and_clear_bit(cpuid, &adeos_pended) && !test_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status)) {
		rtai_sti();
/* specific for the Linux tick, do not cre in a generic handler */
		ADEOS_TICK_REGS.eflags = regs.eflags;
		ADEOS_TICK_REGS.eip    = regs.eip;
		ADEOS_TICK_REGS.xcs    = regs.xcs;
#if defined(CONFIG_SMP) && defined(CONFIG_FRAME_POINTER)
		ADEOS_TICK_REGS.ebp    = regs.ebp;
#endif /* CONFIG_SMP && CONFIG_FRAME_POINTER */
		if (adp_root->cpudata[cpuid].irq_pending_hi != 0) {
			rtai_cli();
			__adeos_sync_stage(IPIPE_IRQMASK_ANY);
		}
#if defined(CONFIG_SMP) &&  LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		set_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status);
#endif
		return 1;
        }
	return 0;
}

void rtai_8254_timer_handler (void);
	__asm__ ( \
        "\n" __ALIGN_STR"\n\t" \
        SYMBOL_NAME_STR(rtai_8254_timer_handler) ":\n\t" \
        "pushl $-256\n\t" \
	"cld\n\t" \
        "pushl %es\n\t" \
        "pushl %ds\n\t" \
        "pushl %eax\n\t" \
        "pushl %ebp\n\t" \
        "pushl %edi\n\t" \
        "pushl %esi\n\t" \
        "pushl %edx\n\t" \
        "pushl %ecx\n\t" \
        "pushl %ebx\n\t" \
	__LXRT_GET_DATASEG(ecx) \
        "movl %ecx, %ds\n\t" \
        "movl %ecx, %es\n\t" \
        "call "SYMBOL_NAME_STR(_rtai_8254_timer_handler)"\n\t" \
        "testl %eax,%eax\n\t" \
        "jnz  ret_from_intr\n\t" \
        "popl %ebx\n\t" \
        "popl %ecx\n\t" \
        "popl %edx\n\t" \
        "popl %esi\n\t" \
        "popl %edi\n\t" \
        "popl %ebp\n\t" \
        "popl %eax\n\t" \
        "popl %ds\n\t" \
        "popl %es\n\t" \
        "addl $4,%esp\n\t" \
        "iret");

static struct desc_struct rtai_8254_timer_sysvec;

#else

#define SET_INTR_GATE(vector, handler, save) 
#define RESET_INTR_GATE(vector, save)

void rt_set_sched_ipi_gate(void) { }
void rt_reset_sched_ipi_gate(void) { }

#endif

#ifdef CONFIG_SMP

static unsigned long rtai_old_irq_affinity[IPIPE_NR_XIRQS];
static int rtai_orig_irq_affinity[IPIPE_NR_XIRQS];

static spinlock_t rtai_iset_lock = SPIN_LOCK_UNLOCKED;

static long long rtai_timers_sync_time;

static struct apic_timer_setup_data rtai_timer_mode[RTAI_NR_CPUS];

static void rtai_critical_sync (void)
{
	struct apic_timer_setup_data *p;

	switch (rtai_sync_level) {
		case 1: {
	    		p = &rtai_timer_mode[rtai_cpuid()];
			while (rtai_rdtsc() < rtai_timers_sync_time);
			if (p->mode) {
				rtai_setup_periodic_apic(p->count, RTAI_APIC_TIMER_VECTOR);
			} else {
				rtai_setup_oneshot_apic(p->count, RTAI_APIC_TIMER_VECTOR);
			}
	    		break;
		}
		case 2: {
			rtai_setup_oneshot_apic(0, RTAI_APIC_TIMER_VECTOR);
			break;
		}
		case 3: {
			rtai_setup_periodic_apic(RTAI_APIC_ICOUNT, LOCAL_TIMER_VECTOR);
			break;
		}
	}
}

/**
 * Install a local APICs timer interrupt handler
 *
 * rt_request_apic_timers requests local APICs timers and defines the mode and
 * count to be used for each local APIC timer. Modes and counts can be chosen
 * arbitrarily for each local APIC timer.
 *
 * @param apic_timer_data is a pointer to a vector of structures
 * @code struct apic_timer_setup_data { int mode, count; }
 * @endcode sized with the number of CPUs available.
 *
 * Such a structure defines:
 * - mode: 0 for a oneshot timing, 1 for a periodic timing.
 * - count: is the period in nanoseconds you want to use on the corresponding
 * timer, not used for oneshot timers.  It is in nanoseconds to ease its
 * programming when different values are used by each timer, so that you do not
 * have to care converting it from the CPU on which you are calling this
 * function.
 *
 * The start of the timing should be reasonably synchronized.   You should call
 * this function with due care and only when you want to manage the related
 * interrupts in your own handler.   For using local APIC timers in pacing real
 * time tasks use the usual rt_start_timer(), which under the MUP scheduler sets
 * the same timer policy on all the local APIC timers, or start_rt_apic_timers()
 * that allows you to use @c struct @c apic_timer_setup_data directly.
 */
void rt_request_apic_timers (void (*handler)(void), struct apic_timer_setup_data *tmdata)
{
	volatile struct rt_times *rtimes;
	struct apic_timer_setup_data *p;
	unsigned long flags;
	int cpuid;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_REQUEST_APIC,handler,0);

	flags = rtai_critical_enter(rtai_critical_sync);
	rtai_sync_level = 1;
	rtai_timers_sync_time = rtai_rdtsc() + rtai_imuldiv(LATCH, rtai_tunables.cpu_freq, RTAI_FREQ_8254);
	for (cpuid = 0; cpuid < RTAI_NR_CPUS; cpuid++) {
		p = &rtai_timer_mode[cpuid];
		*p = tmdata[cpuid];
		rtimes = &rt_smp_times[cpuid];
		if (p->mode) {
			rtimes->linux_tick = RTAI_APIC_ICOUNT;
			rtimes->tick_time = rtai_llimd(rtai_timers_sync_time, RTAI_FREQ_APIC, rtai_tunables.cpu_freq);
			rtimes->periodic_tick = rtai_imuldiv(p->count, RTAI_FREQ_APIC, 1000000000);
			p->count = rtimes->periodic_tick;
	    } else {
			rtimes->linux_tick = rtai_imuldiv(LATCH, rtai_tunables.cpu_freq, RTAI_FREQ_8254);
			rtimes->tick_time = rtai_timers_sync_time;
			rtimes->periodic_tick = rtimes->linux_tick;
			p->count = RTAI_APIC_ICOUNT;
		}
		rtimes->intr_time = rtimes->tick_time + rtimes->periodic_tick;
		rtimes->linux_time = rtimes->tick_time + rtimes->linux_tick;
	}

	p = &rtai_timer_mode[rtai_cpuid()];
	while (rtai_rdtsc() < rtai_timers_sync_time) ;

	if (p->mode) {
		rtai_setup_periodic_apic(p->count,RTAI_APIC_TIMER_VECTOR);
	} else {
		rtai_setup_oneshot_apic(p->count,RTAI_APIC_TIMER_VECTOR);
	}

	rt_release_irq(RTAI_APIC_TIMER_IPI);
	rt_request_irq(RTAI_APIC_TIMER_IPI, (rt_irq_handler_t)handler, NULL, 0);
	SET_INTR_GATE(RTAI_APIC_TIMER_VECTOR, rtai_apic_timer_handler, rtai_apic_timer_sysvec);

	REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();

	for (cpuid = 0; cpuid < RTAI_NR_CPUS; cpuid++) {
		p = &tmdata[cpuid];
		if (p->mode) {
			p->count = rtai_imuldiv(p->count,RTAI_FREQ_APIC,1000000000);
		} else {
			p->count = rtai_imuldiv(p->count,rtai_tunables.cpu_freq,1000000000);
		}
	}

	rtai_critical_exit(flags);
}

/**
 * Uninstall a local APICs timer interrupt handler
 */
void rt_free_apic_timers(void)
{
	unsigned long flags;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_APIC_FREE,0,0);

	FREE_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();
	flags = rtai_critical_enter(rtai_critical_sync);
	rtai_sync_level = 3;
	rtai_setup_periodic_apic(RTAI_APIC_ICOUNT,LOCAL_TIMER_VECTOR);
	RESET_INTR_GATE(RTAI_APIC_TIMER_VECTOR, rtai_apic_timer_sysvec);
	rt_release_irq(RTAI_APIC_TIMER_IPI);
	rtai_critical_exit(flags);
}

/**
 * Set IRQ->CPU assignment
 *
 * rt_assign_irq_to_cpu forces the assignment of the external interrupt @a irq
 * to the CPUs of an assigned mask.
 *
 * @the mask of the interrupts routing before its call.
 * @-EINVAL if @a irq is not a valid IRQ number or some internal data
 * inconsistency is found.
 *
 * @note This functions has effect only on multiprocessors systems.
 * @note With Linux 2.4.xx such a service has finally been made available
 * natively within the raw kernel. With such Linux releases
 * rt_reset_irq_to_sym_mode() resets the original Linux delivery mode, or
 * deliver affinity as they call it. So be warned that such a name is kept
 * mainly for compatibility reasons, as for such a kernel the reset operation
 * does not necessarily implies a symmetric external interrupt delivery.
 */
int rt_assign_irq_to_cpu (int irq, unsigned long cpumask)
{
	if (irq >= IPIPE_NR_XIRQS) {
		return -EINVAL;
	} else {
		unsigned long oldmask, flags;

		rtai_save_flags_and_cli(flags);
		spin_lock(&rtai_iset_lock);
		if ((oldmask = CPUMASK(adeos_set_irq_affinity(irq, CPUMASK_T(cpumask))))) {
			rtai_old_irq_affinity[irq] = oldmask;
		}
		spin_unlock(&rtai_iset_lock);
		rtai_restore_flags(flags);

		return oldmask;
	}
}

/**
 * reset IRQ->CPU assignment
 *
 * rt_reset_irq_to_sym_mode resets the interrupt irq to the symmetric interrupts
 * management, whatever that means, existing before the very first use of RTAI 
 * rt_assign_irq_to_cpu. This function applies to external interrupts only.
 *
 * @the mask of the interrupts routing before its call.
 * @-EINVAL if @a irq is not a valid IRQ number or some internal data
 * inconsistency is found.
 *
 * @note This function has effect only on multiprocessors systems.
 * @note With Linux 2.4.xx such a service has finally been made available
 * natively within the raw kernel. With such Linux releases
 * rt_reset_irq_to_sym_mode() resets the original Linux delivery mode, or
 * deliver affinity as they call it. So be warned that such a name is kept
 * mainly for compatibility reasons, as for such a kernel the reset operation
 * does not necessarily implies a symmetric external interrupt delivery.
 */
int rt_reset_irq_to_sym_mode (int irq)
{
	unsigned long oldmask, flags;

	if (irq >= IPIPE_NR_XIRQS) {
		return -EINVAL;
	} else {
		rtai_save_flags_and_cli(flags);
		spin_lock(&rtai_iset_lock);
		if (rtai_old_irq_affinity[irq] == 0) {
			spin_unlock(&rtai_iset_lock);
			rtai_restore_flags(flags);
			return -EINVAL;
		}
		oldmask = CPUMASK(adeos_set_irq_affinity(irq, CPUMASK_T(0)));
		if (rtai_old_irq_affinity[irq]) {
	        	adeos_set_irq_affinity(irq, CPUMASK_T(rtai_old_irq_affinity[irq]));
	        	rtai_old_irq_affinity[irq] = 0;
        	}
		spin_unlock(&rtai_iset_lock);
		rtai_restore_flags(flags);

		return oldmask;
	}
}

#else  /* !CONFIG_SMP */

#define rtai_critical_sync NULL

void rt_request_apic_timers (void (*handler)(void), struct apic_timer_setup_data *tmdata)
{
	return;
}

void rt_free_apic_timers(void) 
{
	rt_free_timer();
}

int rt_assign_irq_to_cpu (int irq, unsigned long cpus_mask)
{
    return 0;
}

int rt_reset_irq_to_sym_mode (int irq)
{
	return 0;
}

#endif /* CONFIG_SMP */

/**
 * Install a timer interrupt handler.
 *
 * rt_request_timer requests a timer of period tick ticks, and installs the
 * routine @a handler as a real time interrupt service routine for the timer.
 *
 * Set @a tick to 0 for oneshot mode (in oneshot mode it is not used).
 * If @a apic has a nonzero value the local APIC timer is used.   Otherwise
 * timing is based on the 8254.
 *
 */
static int unsigned long used_apic;

int rt_request_timer (void (*handler)(void), unsigned tick, int use_apic)
{
	unsigned long flags;
	int retval;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_REQUEST,handler,tick);

	used_apic = use_apic;
	rtai_save_flags_and_cli(flags);
	rt_times.tick_time = rtai_rdtsc();
    	if (tick > 0) {
		rt_times.linux_tick = use_apic ? RTAI_APIC_ICOUNT : LATCH;
		rt_times.tick_time = ((RTIME)rt_times.linux_tick)*(jiffies + 1);
		rt_times.intr_time = rt_times.tick_time + tick;
		rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.periodic_tick = tick;

		if (use_apic) {
			rt_release_irq(RTAI_APIC_TIMER_IPI);
			rt_request_irq(RTAI_APIC_TIMER_IPI, (rt_irq_handler_t)handler, NULL, 0);
			SET_INTR_GATE(RTAI_APIC_TIMER_VECTOR, rtai_apic_timer_handler, rtai_apic_timer_sysvec);
			rtai_setup_periodic_apic(tick,RTAI_APIC_TIMER_VECTOR);
			retval = REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();
		} else {
			outb(0x34, 0x43);
			outb(tick & 0xff, 0x40);
			outb(tick >> 8, 0x40);
			rt_release_irq(RTAI_TIMER_8254_IRQ);
		    	retval = rt_request_irq(RTAI_TIMER_8254_IRQ, (rt_irq_handler_t)handler, NULL, 0);
			SET_INTR_GATE(FIRST_EXTERNAL_VECTOR + RTAI_TIMER_8254_IRQ, rtai_8254_timer_handler, rtai_8254_timer_sysvec);
		}
	} else {
		rt_times.linux_tick = rtai_imuldiv(LATCH,rtai_tunables.cpu_freq,RTAI_FREQ_8254);
		rt_times.intr_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.periodic_tick = rt_times.linux_tick;

		if (use_apic) {
			rt_release_irq(RTAI_APIC_TIMER_IPI);
			rt_request_irq(RTAI_APIC_TIMER_IPI, (rt_irq_handler_t)handler, NULL, 0);
			SET_INTR_GATE(RTAI_APIC_TIMER_VECTOR, rtai_apic_timer_handler, rtai_apic_timer_sysvec);
			rtai_setup_oneshot_apic(RTAI_APIC_ICOUNT,RTAI_APIC_TIMER_VECTOR);
    			retval = REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();
		} else {
			outb(0x30, 0x43);
			outb(LATCH & 0xff, 0x40);
			outb(LATCH >> 8, 0x40);
			rt_release_irq(RTAI_TIMER_8254_IRQ);
			retval = rt_request_irq(RTAI_TIMER_8254_IRQ, (rt_irq_handler_t)handler, NULL, 0);
			SET_INTR_GATE(FIRST_EXTERNAL_VECTOR + RTAI_TIMER_8254_IRQ, rtai_8254_timer_handler, rtai_8254_timer_sysvec);
		}
	}
	rtai_restore_flags(flags);
	return retval;
}

/**
 * Uninstall a timer interrupt handler.
 *
 * rt_free_timer uninstalls a timer previously set by rt_request_timer().
 */
void rt_free_timer (void)
{
	unsigned long flags;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_FREE,0,0);

	rtai_save_flags_and_cli(flags);
	if (used_apic) {
		FREE_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();
		rtai_setup_periodic_apic(RTAI_APIC_ICOUNT, LOCAL_TIMER_VECTOR);
		RESET_INTR_GATE(RTAI_APIC_TIMER_VECTOR, rtai_apic_timer_sysvec);
		rt_release_irq(RTAI_APIC_TIMER_IPI);
		used_apic = 0;
	} else {
		outb(0x34, 0x43);
		outb(LATCH & 0xff, 0x40);
		outb(LATCH >> 8,0x40);
		if (!rt_release_irq(RTAI_TIMER_8254_IRQ)) {
			RESET_INTR_GATE(FIRST_EXTERNAL_VECTOR + RTAI_TIMER_8254_IRQ, rtai_8254_timer_sysvec);
		}
	}
	rtai_restore_flags(flags);
}

RT_TRAP_HANDLER rt_set_trap_handler (RT_TRAP_HANDLER handler)
{
	return (RT_TRAP_HANDLER)xchg(&rtai_trap_handler, handler);
}

RTIME rd_8254_ts (void)
{
	unsigned long flags;
	int inc, c2;
	RTIME t;

	rtai_hw_save_flags_and_cli(flags);
	outb(0xD8, 0x43);
	c2 = inb(0x42);
	inc = rtai_last_8254_counter2 - (c2 |= (inb(0x42) << 8));
	rtai_last_8254_counter2 = c2;
	t = (rtai_ts_8254 += (inc > 0 ? inc : inc + RTAI_COUNTER_2_LATCH));
	rtai_hw_restore_flags(flags);

	return t;
}

void rt_setup_8254_tsc (void)
{
	unsigned long flags;
	int c;

	flags = rtai_critical_enter(NULL);
	outb_p(0x00, 0x43);
	c = inb_p(0x40);
	c |= inb_p(0x40) << 8;
	outb_p(0xB4, 0x43);
	outb_p(RTAI_COUNTER_2_LATCH & 0xff, 0x42);
	outb_p(RTAI_COUNTER_2_LATCH >> 8, 0x42);
	rtai_ts_8254 = c + ((RTIME)LATCH)*jiffies;
	rtai_last_8254_counter2 = 0; 
	outb_p((inb_p(0x61) & 0xFD) | 1, 0x61);
	rtai_critical_exit(flags);
}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
#define CHECK_KERCTX() \
	if (CHECK_STACK_IN_IRQ) { \
		long esp; \
		__asm__ __volatile__("andl %%esp, %0" : "=r" (esp) : "0" (THREAD_SIZE - 1)); \
		if (unlikely(esp < (sizeof(struct thread_info) + STACK_WARN))) { \
			rt_printk("APPROACHING STACK OVERFLOW (%ld)?\n", esp - sizeof(struct thread_info)); \
		} \
	}
#else
#define CHECK_KERCTX();
#endif

static int rtai_hirq_dispatcher (struct pt_regs *regs)
{
	unsigned long cpuid, irq = regs->orig_eax & 0xFF;

	CHECK_KERCTX();

	rt_switch_to_real_time(cpuid = rtai_cpuid());
	adp_root->irqs[irq].acknowledge(irq); mb();
	if (rtai_realtime_irq[irq].handler) {
		RTAI_SCHED_ISR_LOCK();
		if (rtai_realtime_irq[irq].retmode && rtai_realtime_irq[irq].handler(irq, rtai_realtime_irq[irq].cookie)) {
			RTAI_SCHED_ISR_UNLOCK();
			rt_switch_to_linux(cpuid);
			return 0;
                } else {
			rtai_realtime_irq[irq].handler(irq, rtai_realtime_irq[irq].cookie);
			RTAI_SCHED_ISR_UNLOCK();
		}
	} else {
		adeos_pend_uncond(irq, cpuid);
	}
	rt_switch_to_linux(cpuid);

	if (test_and_clear_bit(cpuid, &adeos_pended) && !test_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status)) {
		rtai_sti();
		if (irq == __adeos_tick_irq) {
			ADEOS_TICK_REGS.eflags = regs->eflags;
			ADEOS_TICK_REGS.eip    = regs->eip;
			ADEOS_TICK_REGS.xcs    = regs->xcs;
#if defined(CONFIG_SMP) && defined(CONFIG_FRAME_POINTER)
			ADEOS_TICK_REGS.ebp    = regs->ebp;
#endif /* CONFIG_SMP && CONFIG_FRAME_POINTER */
        	}
		if (adp_root->cpudata[cpuid].irq_pending_hi != 0) {
			rtai_cli();
			__adeos_sync_stage(IPIPE_IRQMASK_ANY);
		}
#if defined(CONFIG_SMP) &&  LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
		set_bit(IPIPE_STALL_FLAG, &adp_root->cpudata[cpuid].status);
#endif
		return 1;
        }
	return 0;
}

//#define HINT_DIAG_ECHO
//#define HINT_DIAG_TRAPS

#ifdef HINT_DIAG_ECHO
#define HINT_DIAG_MSG(x) x
#else
#define HINT_DIAG_MSG(x)
#endif

#ifdef UNWRAPPED_CATCH_EVENT
static int rtai_trap_fault (unsigned event, void *evdata)
{
#ifdef HINT_DIAG_TRAPS
	static unsigned long traps_in_hard_intr = 0;
        do {
                unsigned long flags;
                rtai_save_flags_and_cli(flags);
                if (!test_bit(RTAI_IFLAG, &flags)) {
                        if (!test_and_set_bit(evinfo->event, &traps_in_hard_intr)) {
                                HINT_DIAG_MSG(rt_printk("TRAP %d HAS INTERRUPT DISABLED (TRAPS PICTURE %lx).\n", evinfo->event, traps_in_hard_intr););
                        }
                }
        } while (0);
#endif

	static const int trap2sig[] = {
    		SIGFPE,         //  0 - Divide error
		SIGTRAP,        //  1 - Debug
		SIGSEGV,        //  2 - NMI (but we ignore these)
		SIGTRAP,        //  3 - Software breakpoint
		SIGSEGV,        //  4 - Overflow
		SIGSEGV,        //  5 - Bounds
		SIGILL,         //  6 - Invalid opcode
		SIGSEGV,        //  7 - Device not available
		SIGSEGV,        //  8 - Double fault
		SIGFPE,         //  9 - Coprocessor segment overrun
		SIGSEGV,        // 10 - Invalid TSS
		SIGBUS,         // 11 - Segment not present
		SIGBUS,         // 12 - Stack segment
		SIGSEGV,        // 13 - General protection fault
		SIGSEGV,        // 14 - Page fault
		0,              // 15 - Spurious interrupt
		SIGFPE,         // 16 - Coprocessor error
		SIGBUS,         // 17 - Alignment check
		SIGSEGV,        // 18 - Reserved
		SIGFPE,         // 19 - XMM fault
		0,0,0,0,0,0,0,0,0,0,0,0
	};

	TRACE_RTAI_TRAP_ENTRY(evinfo->event, 0);

    /* Notes:

    1) GPF needs to be propagated downstream whichever domain caused
    it. This is required so that we don't spuriously raise a fatal
    error when some fixup code is available to solve the error
    condition. For instance, Linux always attempts to reload the %gs
    segment register when switching a process in (__switch_to()),
    regardless of its value. It is then up to Linux's GPF handling
    code to search for a possible fixup whenever some exception
    occurs. In the particular case of the %gs register, such an
    exception could be raised for an exiting process if a preemption
    occurs inside a short time window, after the process's LDT has
    been dropped, but before the kernel lock is taken.  The same goes
    for LXRT switching back a Linux thread in non-RT mode which
    happens to have been preempted inside do_exit() after the MM
    context has been dropped (thus the LDT too). In such a case, %gs
    could be reloaded with what used to be the TLS descriptor of the
    exiting thread, but unfortunately after the LDT itself has been
    dropped. Since the default LDT is only 5 entries long, any attempt
    to refer to an LDT-indexed descriptor above this value would cause
    a GPF.
    2) NMI is not pipelined by Adeos. */

	if (!in_hrt_mode(rtai_cpuid())) {
		goto propagate;
	}

	if (event == 7)	{ /* (FPU) Device not available. */
	/* A trap must come from a well estabilished Linux task context; from
	   anywhere else it is a bug to fix and not a hal.c problem */
		struct task_struct *linux_task = current;

	/* We need to keep this to avoid going through Linux in case users
	   do not set the FPU, for hard real time operations, either by 
	   calling the appropriate LXRT function or by doing any FP operation
	   before going to hard mode. Notice that after proper initialization
	   LXRT anticipate restoring the hard FP context at any task switch.
	   So just the initialisation should be needed, but we do what Linux
	   does in math_state_restore anyhow, to stay on the safe side. 
	   In any case we inform the user. */
		rtai_hw_cli(); /* in task context, so we can be preempted */
		if (lnxtsk_uses_fpu(linux_task)) {
			restore_fpu(linux_task);
			rt_printk("\nUNEXPECTED FPU TRAP FROM HARD PID = %d\n", linux_task->pid);
		} else {
			init_hard_fpu(linux_task);
			rt_printk("\nUNEXPECTED FPU INITIALIZATION FROM HARD PID = %d\n", linux_task->pid);
		}
		rtai_hw_sti();
		goto endtrap;
	}

#if ADEOS_RELEASE_NUMBER >= 0x02060601
	if (event == 14) {	/* Page fault. */
		struct pt_regs *regs = evdata;
		unsigned long address;

	/* Handle RTAI-originated faults in kernel space caused by
	   on-demand virtual memory mappings. We can specifically
	   process this case through the Linux fault handler since we
	   know that it is context-agnostic and does not wreck the
	   determinism. Any other case would lead us to panicking. */

		rtai_hw_cli();
		__asm__("movl %%cr2,%0":"=r" (address));
		if (address >= TASK_SIZE && !(regs->orig_eax & 5)) { /* i.e. trap error code. */
			asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code);
			do_page_fault(regs,regs->orig_eax);
			goto endtrap;
		}
		 rtai_hw_sti();
	}
#endif /* ADEOS_RELEASE_NUMBER >= 0x02060601 */

	if (rtai_trap_handler && rtai_trap_handler(event, trap2sig[event], (struct pt_regs *)evdata, NULL)) {
		goto endtrap;
	}
propagate:
	return 0;
endtrap:
	TRACE_RTAI_TRAP_EXIT();
	return 1;
}
#else
static void rtai_trap_fault (adevinfo_t *evinfo)
{
#ifdef HINT_DIAG_TRAPS
	static unsigned long traps_in_hard_intr = 0;
        do {
                unsigned long flags;
                rtai_save_flags_and_cli(flags);
                if (!test_bit(RTAI_IFLAG, &flags)) {
                        if (!test_and_set_bit(evinfo->event, &traps_in_hard_intr)) {
                                HINT_DIAG_MSG(rt_printk("TRAP %d HAS INTERRUPT DISABLED (TRAPS PICTURE %lx).\n", evinfo->event, traps_in_hard_intr););
                        }
                }
        } while (0);
#endif

	static const int trap2sig[] = {
    		SIGFPE,         //  0 - Divide error
		SIGTRAP,        //  1 - Debug
		SIGSEGV,        //  2 - NMI (but we ignore these)
		SIGTRAP,        //  3 - Software breakpoint
		SIGSEGV,        //  4 - Overflow
		SIGSEGV,        //  5 - Bounds
		SIGILL,         //  6 - Invalid opcode
		SIGSEGV,        //  7 - Device not available
		SIGSEGV,        //  8 - Double fault
		SIGFPE,         //  9 - Coprocessor segment overrun
		SIGSEGV,        // 10 - Invalid TSS
		SIGBUS,         // 11 - Segment not present
		SIGBUS,         // 12 - Stack segment
		SIGSEGV,        // 13 - General protection fault
		SIGSEGV,        // 14 - Page fault
		0,              // 15 - Spurious interrupt
		SIGFPE,         // 16 - Coprocessor error
		SIGBUS,         // 17 - Alignment check
		SIGSEGV,        // 18 - Reserved
		SIGFPE,         // 19 - XMM fault
		0,0,0,0,0,0,0,0,0,0,0,0
	};

	TRACE_RTAI_TRAP_ENTRY(evinfo->event, 0);

    /* Notes:

    1) GPF needs to be propagated downstream whichever domain caused
    it. This is required so that we don't spuriously raise a fatal
    error when some fixup code is available to solve the error
    condition. For instance, Linux always attempts to reload the %gs
    segment register when switching a process in (__switch_to()),
    regardless of its value. It is then up to Linux's GPF handling
    code to search for a possible fixup whenever some exception
    occurs. In the particular case of the %gs register, such an
    exception could be raised for an exiting process if a preemption
    occurs inside a short time window, after the process's LDT has
    been dropped, but before the kernel lock is taken.  The same goes
    for LXRT switching back a Linux thread in non-RT mode which
    happens to have been preempted inside do_exit() after the MM
    context has been dropped (thus the LDT too). In such a case, %gs
    could be reloaded with what used to be the TLS descriptor of the
    exiting thread, but unfortunately after the LDT itself has been
    dropped. Since the default LDT is only 5 entries long, any attempt
    to refer to an LDT-indexed descriptor above this value would cause
    a GPF.
    2) NMI is not pipelined by Adeos. */

	if (!in_hrt_mode(rtai_cpuid())) {
		goto propagate;
	}

	if (evinfo->event == 7)	{ /* (FPU) Device not available. */
	/* A trap must come from a well estabilished Linux task context; from
	   anywhere else it is a bug to fix and not a hal.c problem */
		struct task_struct *linux_task = current;

	/* We need to keep this to avoid going through Linux in case users
	   do not set the FPU, for hard real time operations, either by 
	   calling the appropriate LXRT function or by doing any FP operation
	   before going to hard mode. Notice that after proper initialization
	   LXRT anticipate restoring the hard FP context at any task switch.
	   So just the initialisation should be needed, but we do what Linux
	   does in math_state_restore anyhow, to stay on the safe side. 
	   In any case we inform the user. */
		rtai_hw_cli(); /* in task context, so we can be preempted */
		if (lnxtsk_uses_fpu(linux_task)) {
			restore_fpu(linux_task);
			rt_printk("\nUNEXPECTED FPU TRAP FROM HARD PID = %d\n", linux_task->pid);
		} else {	
			init_hard_fpu(linux_task);
			rt_printk("\nUNEXPECTED FPU INITIALIZATION FROM HARD PID = %d\n", linux_task->pid);
		}
		rtai_hw_sti();
		goto endtrap;
	}

#if ADEOS_RELEASE_NUMBER >= 0x02060601
	if (evinfo->event == 14) {	/* Page fault. */
		struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
		unsigned long address;

	/* Handle RTAI-originated faults in kernel space caused by
	   on-demand virtual memory mappings. We can specifically
	   process this case through the Linux fault handler since we
	   know that it is context-agnostic and does not wreck the
	   determinism. Any other case would lead us to panicking. */

		rtai_hw_cli();
		__asm__("movl %%cr2,%0":"=r" (address));
		if (address >= TASK_SIZE && !(regs->orig_eax & 5)) { /* i.e. trap error code. */
			asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code);
			do_page_fault(regs,regs->orig_eax);
			goto endtrap;
		}
		 rtai_hw_sti();
	}
#endif /* ADEOS_RELEASE_NUMBER >= 0x02060601 */

	if (rtai_trap_handler && rtai_trap_handler(evinfo->event, trap2sig[evinfo->event], (struct pt_regs *)evinfo->evdata, NULL)) {
		goto endtrap;
	}
propagate:
	adeos_propagate_event(evinfo);
endtrap:
	TRACE_RTAI_TRAP_EXIT();
}
#endif

static void rtai_lsrq_dispatcher (unsigned virq)
{
	unsigned long pending, srq;

	spin_lock(&rtai_lsrq_lock);
	while ((pending = rtai_sysreq_pending & ~rtai_sysreq_running)) {
		set_bit(srq = ffnz(pending), &rtai_sysreq_running);
		clear_bit(srq, &rtai_sysreq_pending);
		spin_unlock(&rtai_lsrq_lock);
		if (test_bit(srq, &rtai_sysreq_map)) {
			rtai_sysreq_table[srq].k_handler();
		}
		clear_bit(srq, &rtai_sysreq_running);
		spin_lock(&rtai_lsrq_lock);
	}
	spin_unlock(&rtai_lsrq_lock);
}

static inline long long rtai_usrq_dispatcher (unsigned srq, unsigned label)
{
	TRACE_RTAI_SRQ_ENTRY(srq);
	if (srq > 0 && srq < RTAI_NR_SRQS && test_bit(srq, &rtai_sysreq_map) && rtai_sysreq_table[srq].u_handler) {
		return rtai_sysreq_table[srq].u_handler(label);
	} else {
		for (srq = 1; srq < RTAI_NR_SRQS; srq++) {
			if (test_bit(srq, &rtai_sysreq_map) && rtai_sysreq_table[srq].label == label) {
				return (long long)srq;
			}
		}
	}
	TRACE_RTAI_SRQ_EXIT();
	return 0LL;
}

#include <asm/rtai_usi.h>
long long (*rtai_lxrt_dispatcher)(unsigned long, unsigned long, void *);

asmlinkage void rtai_syscall_dispatcher (long bx, unsigned long cx_args, long long *dx_retval, long si, long di, long bp, unsigned long ax_srq, long ds, long es, long orig_eax, long eip, long cs, unsigned long eflags)
{
#ifdef USI_SRQ_MASK
	IF_IS_A_USI_SRQ_CALL_IT();
#endif
	*dx_retval = ax_srq > RTAI_NR_SRQS ? rtai_lxrt_dispatcher(ax_srq, cx_args, &bx) : rtai_usrq_dispatcher(ax_srq, cx_args);
	if (!in_hrt_mode(rtai_cpuid())) {
		local_irq_enable();
		if (need_resched()) {
			schedule();
		}
	}
}

void rtai_uvec_handler (void);
	__asm__ ( \
        "\n" __ALIGN_STR"\n\t" \
        SYMBOL_NAME_STR(rtai_uvec_handler) ":\n\t" \
	"pushl $0\n\t" \
	"cld\n\t" \
        "pushl %es\n\t" \
        "pushl %ds\n\t" \
        "pushl %eax\n\t" \
        "pushl %ebp\n\t" \
	"pushl %edi\n\t" \
        "pushl %esi\n\t" \
        "pushl %edx\n\t" \
        "pushl %ecx\n\t" \
	"pushl %ebx\n\t" \
	__LXRT_GET_DATASEG(ebx) \
        "movl %ebx, %ds\n\t" \
        "movl %ebx, %es\n\t" \
        "call "SYMBOL_NAME_STR(rtai_syscall_dispatcher)"\n\t" \
        "popl %ebx\n\t" \
        "popl %ecx\n\t" \
        "popl %edx\n\t" \
        "popl %esi\n\t" \
	"popl %edi\n\t" \
        "popl %ebp\n\t" \
        "popl %eax\n\t" \
        "popl %ds\n\t" \
        "popl %es\n\t" \
	"addl $4, %esp\n\t" \
        "iret");

struct desc_struct rtai_set_gate_vector (unsigned vector, int type, int dpl, void *handler)
{
	struct desc_struct e = idt_table[vector];
	idt_table[vector].a = (__KERNEL_CS << 16) | ((unsigned)handler & 0x0000FFFF);
	idt_table[vector].b = ((unsigned)handler & 0xFFFF0000) | (0x8000 + (dpl << 13) + (type << 8));
	return e;
}

void rtai_reset_gate_vector (unsigned vector, struct desc_struct e)
{
	idt_table[vector] = e;
}

static void rtai_install_archdep (void)
{
	unsigned long flags;

	flags = rtai_critical_enter(NULL);
    /* Backup and replace the sysreq vector. */
	rtai_sysvec = rtai_set_gate_vector(RTAI_SYS_VECTOR, 15, 3, &rtai_uvec_handler);
	rtai_critical_exit(flags);

	if (rtai_cpufreq_arg == 0) {
		adsysinfo_t sysinfo;
		adeos_get_sysinfo(&sysinfo);
		rtai_cpufreq_arg = (unsigned long)sysinfo.cpufreq;
	}
	rtai_tunables.cpu_freq = rtai_cpufreq_arg;

#ifdef CONFIG_X86_LOCAL_APIC
	if (rtai_apicfreq_arg == 0) {
		rtai_apicfreq_arg = HZ*apic_read(APIC_TMICT);
	}
	rtai_tunables.apic_freq = rtai_apicfreq_arg;
#endif /* CONFIG_X86_LOCAL_APIC */
}

static void rtai_uninstall_archdep (void) 
{
	unsigned long flags;

	flags = rtai_critical_enter(NULL);
	idt_table[RTAI_SYS_VECTOR] = rtai_sysvec;
	rtai_critical_exit(flags);
}

int rtai_calibrate_8254 (void)
{
	unsigned long flags;
	RTIME t, dt;
	int i;

	flags = rtai_critical_enter(NULL);
	outb(0x34,0x43);
	t = rtai_rdtsc();
	for (i = 0; i < 10000; i++) { 
		outb(LATCH & 0xff,0x40);
		outb(LATCH >> 8,0x40);
	}
	dt = rtai_rdtsc() - t;
	rtai_critical_exit(flags);

	return rtai_imuldiv(dt, 100000, RTAI_CPU_FREQ);
}

void (*rt_set_ihook (void (*hookfn)(int)))(int)
{
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	return (void (*)(int))xchg(&rtai_isr_hook, hookfn); /* This is atomic */
#else  /* !CONFIG_RTAI_SCHED_ISR_LOCK */
	return NULL;
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
}


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
static int errno;

static inline _syscall3(int, sched_setscheduler, pid_t,pid, int,policy, struct sched_param *,param)
#endif

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
void rtai_set_linux_task_priority (struct task_struct *task, int policy, int prio)
{
	task->policy = policy;
	task->rt_priority = prio;
	set_tsk_need_resched(current);
}
#else /* KERNEL_VERSION >= 2.6.0 */
void rtai_set_linux_task_priority (struct task_struct *task, int policy, int prio)
{
	int rc;
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,11)
	struct sched_param __user param;
	mm_segment_t old_fs;

	param.sched_priority = prio;
	old_fs = get_fs();
	set_fs(KERNEL_DS);
	rc = sched_setscheduler(task->pid, policy, &param);
	set_fs(old_fs);
#else
	struct sched_param param = { prio };
	rc = sched_setscheduler(task, policy, &param);
#endif

	if (rc) {
		printk("RTAI[hal]: sched_setscheduler(policy=%d,prio=%d) failed, code %d (%s -- pid=%d)\n", policy, prio, rc, task->comm, task->pid);
	}
}
#endif  /* KERNEL_VERSION < 2.6.0 */

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rtai_proc_root = NULL;

static int rtai_read_proc (char *page, char **start, off_t off, int count, int *eof, void *data)
{
	PROC_PRINT_VARS;
	int i, none;

	PROC_PRINT("\n** RTAI/x86:\n\n");
#ifdef CONFIG_X86_LOCAL_APIC
	PROC_PRINT("    APIC Frequency: %lu\n",rtai_tunables.apic_freq);
	PROC_PRINT("    APIC Latency: %d ns\n",RTAI_LATENCY_APIC);
	PROC_PRINT("    APIC Setup: %d ns\n",RTAI_SETUP_TIME_APIC);
#endif /* CONFIG_X86_LOCAL_APIC */
    
	none = 1;
	PROC_PRINT("\n** Real-time IRQs used by RTAI: ");
    	for (i = 0; i < RTAI_NR_IRQS; i++) {
		if (rtai_realtime_irq[i].handler) {
			if (none) {
				PROC_PRINT("\n");
				none = 0;
			}
			PROC_PRINT("\n    #%d at %p", i, rtai_realtime_irq[i].handler);
		}
        }
	if (none) {
		PROC_PRINT("none");
	}
	PROC_PRINT("\n\n");

	PROC_PRINT("** RTAI extension traps: \n\n");
	PROC_PRINT("    SYSREQ=0x%x\n",RTAI_SYS_VECTOR);

	none = 1;
	PROC_PRINT("** RTAI SYSREQs in use: ");
    	for (i = 0; i < RTAI_NR_SRQS; i++) {
		if (rtai_sysreq_table[i].k_handler || rtai_sysreq_table[i].u_handler) {
			PROC_PRINT("#%d ", i);
			none = 0;
		}
        }

	if (none) {
		PROC_PRINT("none");
	}
    	PROC_PRINT("\n\n");
	PROC_PRINT_DONE;
}

static int rtai_proc_register (void)
{
	struct proc_dir_entry *ent;

	rtai_proc_root = create_proc_entry("rtai",S_IFDIR, 0);
	if (!rtai_proc_root) {
		printk(KERN_ERR "Unable to initialize /proc/rtai.\n");
		return -1;
        }
	rtai_proc_root->owner = THIS_MODULE;
	ent = create_proc_entry("hal",S_IFREG|S_IRUGO|S_IWUSR,rtai_proc_root);
	if (!ent) {
		printk(KERN_ERR "Unable to initialize /proc/rtai/hal.\n");
		return -1;
        }
	ent->read_proc = rtai_read_proc;

	return 0;
}

static void rtai_proc_unregister (void)
{
	remove_proc_entry("hal",rtai_proc_root);
	remove_proc_entry("rtai",0);
}

#endif /* CONFIG_PROC_FS */

static void rtai_domain_entry (int iflag)
{
	if (iflag) {
		rt_printk(KERN_INFO "RTAI[hal]: %s mounted over Adeos %s.\n", PACKAGE_VERSION, ADEOS_VERSION_STRING);
		rt_printk(KERN_INFO "RTAI[hal]: compiled with %s.\n", CONFIG_RTAI_COMPILER);
	}
#ifndef CONFIG_ADEOS_NOTHREADS
	for (;;) adeos_suspend_domain();
#endif /* !CONFIG_ADEOS_NOTHREADS */
}

extern void *adeos_extern_irq_handler;

int __rtai_hal_init (void)
{
	int trapnr;
	adattr_t attr;

#ifdef CONFIG_X86_LOCAL_APIC
	if (!test_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability)) {
		printk("RTAI[hal]: ERROR, LOCAL APIC CONFIGURED BUT NOT AVAILABLE/ENABLED\n");
		return -1;
	}
#endif

	if (!(rtai_sysreq_virq = adeos_alloc_irq())) {
		printk(KERN_ERR "RTAI[hal]: no virtual interrupt available.\n");
		return 1;
	}
	adeos_virtualize_irq(rtai_sysreq_virq, &rtai_lsrq_dispatcher, NULL, IPIPE_HANDLE_MASK);
	adeos_extern_irq_handler = rtai_hirq_dispatcher;

	rtai_install_archdep();

#ifdef CONFIG_PROC_FS
	rtai_proc_register();
#endif

	adeos_init_attr(&attr);
	attr.name     = "RTAI";
	attr.domid    = RTAI_DOMAIN_ID;
	attr.entry    = rtai_domain_entry;
	attr.estacksz = PAGE_SIZE;
	attr.priority = 2000000000;
	adeos_register_domain(&rtai_domain, &attr);
	for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++) {
		adeos_catch_event(trapnr, (void *)rtai_trap_fault);
	}

#ifdef CONFIG_SMP
	if (IsolCpusMask) {
		for (trapnr = 0; trapnr < IPIPE_NR_XIRQS; trapnr++) {
			rtai_orig_irq_affinity[trapnr] = rt_assign_irq_to_cpu(trapnr, ~IsolCpusMask);
		}
	}
#else
	IsolCpusMask = 0;
#endif

#ifdef CONFIG_ADEOS_NOTHREADS
	printk(KERN_INFO "RTAI[hal]: mounted (ADEOS-NOTHREADS, IMMEDIATE (INTERNAL TIMING IRQs %s), ISOL_CPUS_MASK: %lx).\n", DONT_DISPATCH_CORE_IRQS ? "VECTORED" : "DISPATCHED", IsolCpusMask);
#else
	printk(KERN_INFO "RTAI[hal]: mounted (ADEOS-THREADS, IMMEDIATE (INTERNAL TIMING IRQs %s), ISOL_CPUS_MASK: %lx).\n", DONT_DISPATCH_CORE_IRQS ? "VECTORED" : "DISPATCHED", IsolCpusMask);
#endif

	return 0;
}

void __rtai_hal_exit (void)
{
	int trapnr;
#ifdef CONFIG_PROC_FS
	rtai_proc_unregister();
#endif
	adeos_extern_irq_handler = NULL;
	adeos_unregister_domain(&rtai_domain);
	for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++) {
		adeos_catch_event(trapnr, NULL);
	}
	adeos_virtualize_irq(rtai_sysreq_virq, NULL, NULL, 0);
	adeos_free_irq(rtai_sysreq_virq);
	rtai_uninstall_archdep();

	if (IsolCpusMask) {
		for (trapnr = 0; trapnr < IPIPE_NR_XIRQS; trapnr++) {
			rt_reset_irq_to_sym_mode(trapnr);
		}
	}

	printk(KERN_INFO "RTAI[hal]: unmounted.\n");
}

module_init(__rtai_hal_init);
module_exit(__rtai_hal_exit);

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,60,0)
asmlinkage int rt_printk(const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	r = vprintk(fmt, args);
	va_end(args);

	return r;
}

asmlinkage int rt_sync_printk(const char *fmt, ...)
{
	va_list args;
	int r;

	va_start(args, fmt);
	adeos_set_printk_sync(&rtai_domain);
	r = vprintk(fmt, args);
	adeos_set_printk_async(&rtai_domain);
	va_end(args);

	return r;
}
#else
#define VSNPRINTF_BUF 256
asmlinkage int rt_printk(const char *fmt, ...)
{
	char buf[VSNPRINTF_BUF];
	va_list args;

        va_start(args, fmt);
        vsnprintf(buf, VSNPRINTF_BUF, fmt, args);
        va_end(args);
	return printk("%s", buf);
}

asmlinkage int rt_sync_printk(const char *fmt, ...)
{
	char buf[VSNPRINTF_BUF];
	va_list args;
	int r;

        va_start(args, fmt);
        vsnprintf(buf, VSNPRINTF_BUF, fmt, args);
        va_end(args);
	adeos_set_printk_sync(&rtai_domain);
	r = printk("%s", buf);
	adeos_set_printk_async(&rtai_domain);

	return r;
}
#endif

/*
 *  support for decoding long long numbers in kernel space.
 */

void *ll2a (long long ll, char *s)
{
	unsigned long i, k, ul;
	char a[20];

	if (ll < 0) {
		s[0] = 1;
		ll = -ll;
	} else {
		s[0] = 0;
	}
	i = 0;
	while (ll > 0xFFFFFFFF) {
		ll = rtai_ulldiv(ll, 10, &k);
		a[++i] = k + '0';
	}
	ul = ((unsigned long *)&ll)[LOW];
	do {
		ul = (k = ul)/10;
		a[++i] = k - ul*10 + '0';
	} while (ul);
	if (s[0]) {
		k = 1;
		s[0] = '-';
	} else {
		k = 0;
	}
	a[0] = 0;
	while ((s[k++] = a[i--]));
	return s;
}

EXPORT_SYMBOL(rtai_realtime_irq);
EXPORT_SYMBOL(rt_request_irq);
EXPORT_SYMBOL(rt_release_irq);
EXPORT_SYMBOL(rt_set_irq_cookie);
EXPORT_SYMBOL(rt_set_irq_retmode);
EXPORT_SYMBOL(rt_startup_irq);
EXPORT_SYMBOL(rt_shutdown_irq);
EXPORT_SYMBOL(rt_enable_irq);
EXPORT_SYMBOL(rt_disable_irq);
EXPORT_SYMBOL(rt_mask_and_ack_irq);
EXPORT_SYMBOL(rt_unmask_irq);
EXPORT_SYMBOL(rt_ack_irq);
EXPORT_SYMBOL(rt_request_linux_irq);
EXPORT_SYMBOL(rt_free_linux_irq);
EXPORT_SYMBOL(rt_pend_linux_irq);
EXPORT_SYMBOL(rt_request_srq);
EXPORT_SYMBOL(rt_free_srq);
EXPORT_SYMBOL(rt_pend_linux_srq);
EXPORT_SYMBOL(rt_assign_irq_to_cpu);
EXPORT_SYMBOL(rt_reset_irq_to_sym_mode);
EXPORT_SYMBOL(rt_request_apic_timers);
EXPORT_SYMBOL(rt_free_apic_timers);
EXPORT_SYMBOL(rt_request_timer);
EXPORT_SYMBOL(rt_free_timer);
EXPORT_SYMBOL(rt_set_trap_handler);
EXPORT_SYMBOL(rd_8254_ts);
EXPORT_SYMBOL(rt_setup_8254_tsc);
EXPORT_SYMBOL(rt_set_ihook);

EXPORT_SYMBOL(rtai_calibrate_8254);
EXPORT_SYMBOL(rtai_broadcast_to_local_timers);
EXPORT_SYMBOL(rtai_critical_enter);
EXPORT_SYMBOL(rtai_critical_exit);
EXPORT_SYMBOL(rtai_set_linux_task_priority);

EXPORT_SYMBOL(rtai_linux_context);
EXPORT_SYMBOL(rtai_domain);
EXPORT_SYMBOL(rtai_proc_root);
EXPORT_SYMBOL(rtai_tunables);
EXPORT_SYMBOL(rtai_cpu_lock);
EXPORT_SYMBOL(rtai_cpu_realtime);
EXPORT_SYMBOL(rt_times);
EXPORT_SYMBOL(rt_smp_times);

EXPORT_SYMBOL(rt_printk);
EXPORT_SYMBOL(rt_sync_printk);
EXPORT_SYMBOL(ll2a);

EXPORT_SYMBOL(rtai_set_gate_vector);
EXPORT_SYMBOL(rtai_reset_gate_vector);

EXPORT_SYMBOL(rtai_lxrt_dispatcher);
EXPORT_SYMBOL(rt_scheduling);
EXPORT_SYMBOL(adeos_pended);
#ifdef CONFIG_SMP
EXPORT_SYMBOL(rt_set_sched_ipi_gate);
EXPORT_SYMBOL(rt_reset_sched_ipi_gate);
#endif
/*@}*/
