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
 * @defgroup hal RTAI services functions.
 *
 * This module defines some functions that can be used by RTAI tasks, for
 * managing interrupts and communication services with Linux processes.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/wrapper.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <asm/system.h>
#include <asm/hw_irq.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/mmu_context.h>
#include <asm/uaccess.h>
#include <asm/time.h>
#define __RTAI_HAL__
#include <asm/rtai_hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <rtai_proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

MODULE_LICENSE("GPL");

static unsigned long rtai_cpufreq_arg = RTAI_CALIBRATED_CPU_FREQ;
MODULE_PARM(rtai_cpufreq_arg,"i");

#ifdef CONFIG_SMP
#error "SMP is not supported"
#endif /* CONFIG_SMP */

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
static int rtai_isr_nesting[RTAI_NR_CPUS];
static void (*rtai_isr_hook)(int nesting);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

extern struct desc_struct idt_table[];

extern void show_registers(struct pt_regs *regs);

adomain_t rtai_domain;

#define RTAI_NR_IRQS    (RTAI_TIMER_DECR_IRQ + 1)

static struct {

    void (*handler)(unsigned irq, void *cookie);
    void *cookie;

} rtai_realtime_irq[RTAI_NR_IRQS];

static struct {

    unsigned long flags;
    int count;

} rtai_linux_irq[NR_IRQS];

static struct {

    void (*k_handler)(void);
    long long (*u_handler)(unsigned);
    unsigned label;

} rtai_sysreq_table[RTAI_NR_SRQS];

static unsigned rtai_sysreq_virq;

static unsigned long rtai_sysreq_map = 3; /* srqs #[0-1] are reserved */

static unsigned long rtai_sysreq_pending;

static unsigned long rtai_sysreq_running;

static spinlock_t rtai_ssrq_lock = SPIN_LOCK_UNLOCKED;

static volatile int rtai_sync_level;

static atomic_t rtai_sync_count = ATOMIC_INIT(1);

#ifdef FIXME
static int rtai_last_8254_counter2;

static RTIME rtai_ts_8254;

static struct desc_struct rtai_sysvec;

static RT_TRAP_HANDLER rtai_trap_handler;
#endif

static int rtai_mount_count;

struct rt_times rt_times = { 0 };

struct rt_times rt_smp_times[RTAI_NR_CPUS] = { { 0 } };

struct rtai_switch_data rtai_linux_context[RTAI_NR_CPUS] = { { 0 } };

struct calibration_data rtai_tunables = { 0 };

volatile unsigned long rtai_status = 0;

volatile unsigned long rtai_cpu_realtime = 0;

volatile unsigned long rtai_cpu_lock = 0;

volatile unsigned long rtai_cpu_lxrt = 0;

int rtai_adeos_ptdbase = -1;

static int rtai_uvec_handler (struct pt_regs *regs);

int (*rtai_signal_handler)(struct task_struct *task,
			   int sig);

static inline unsigned long rtai_critical_enter (void (*synch)(void)) {

    unsigned long flags = adeos_critical_enter(synch);

    if (atomic_dec_and_test(&rtai_sync_count))
	rtai_sync_level = 0;
    else if (synch != NULL)
	printk("RTAI/Adeos: warning: nested sync will fail.\n");

    return flags;
}

static inline void rtai_critical_exit (unsigned long flags) {

    atomic_inc(&rtai_sync_count);
    adeos_critical_exit(flags);
}

/* Note: On Linux boxen running Adeos, adp_root == &linux_domain */

void rtai_linux_cli (void) {
    adeos_stall_pipeline_from(adp_root);
}

void rtai_linux_sti (void) {
    adeos_unstall_pipeline_from(adp_root);
}

unsigned rtai_linux_save_flags (void) {
    return (unsigned)adeos_test_pipeline_from(adp_root);
}

void rtai_linux_restore_flags (unsigned flags) {
    adeos_restore_pipeline_from(adp_root,flags);
}

void rtai_linux_restore_flags_nosync (unsigned flags, int cpuid) {
    adeos_restore_pipeline_nosync(adp_root,flags,cpuid);
}

unsigned rtai_linux_save_flags_and_cli (void) {
    return (unsigned)adeos_test_and_stall_pipeline_from(adp_root);
}

int rt_request_irq (unsigned irq,
		    void (*handler)(unsigned irq, void *cookie),
		    void *cookie)
{
    unsigned long flags;
    adeos_declare_cpuid;

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (handler == NULL || irq >= RTAI_NR_IRQS)
	return -EINVAL;

    flags = rtai_critical_enter(NULL);

    if (rtai_realtime_irq[irq].handler != NULL)
	{
	rtai_critical_exit(flags);
	return -EBUSY;
	}

    /* Disable decrementer handling in Linux timer_interrupt() */
    if (irq == RTAI_TIMER_DECR_IRQ)	
	disarm_decr[cpuid] = 1;
 
    rtai_realtime_irq[irq].handler = handler;
    rtai_realtime_irq[irq].cookie = cookie;

    rtai_critical_exit(flags);

    return 0;
}

int rt_release_irq (unsigned irq)

{
    unsigned long flags;
    adeos_declare_cpuid;

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (irq >= RTAI_NR_IRQS)
	return -EINVAL;

    flags = rtai_critical_enter(NULL);

    rtai_realtime_irq[irq].handler = NULL;
    /* Reenable decrementer handling in Linux timer_interrupt() */
    if (irq == RTAI_TIMER_DECR_IRQ)	
	disarm_decr[cpuid] = 0;

    rtai_critical_exit(flags);

    return 0;
}

void rt_set_irq_cookie (unsigned irq, void *cookie) 
{
    if (irq < RTAI_NR_IRQS)
	rtai_realtime_irq[irq].cookie = cookie;
}

#ifdef FIXME
struct desc_struct rt_set_full_intr_vect(unsigned int vector, int type, int dpl, void *handler)
{
    struct desc_struct fun = { 0 };
    if (vector >= MIN_IDT_VEC && vector <= MAX_IDT_VEC) {
    fun.fun = idt_table[vector - MIN_IDT_VEC];
    idt_table[vector - MIN_IDT_VEC] = handler;
    if (!rtai_srq_bckdr) {
    rtai_srq_bckdr = dispatch_srq;
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
#endif

/* Note: Adeos already does all the magic that allows calling the
   interrupt controller routines safely. */

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
unsigned rt_startup_irq (unsigned irq) {

    return irq_desc[irq].handler->startup(irq);
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
void rt_shutdown_irq (unsigned irq) {

    irq_desc[irq].handler->shutdown(irq);
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
void rt_enable_irq (unsigned irq) {

    irq_desc[irq].handler->enable(irq);
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
void rt_disable_irq (unsigned irq) {

    irq_desc[irq].handler->disable(irq);
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
void rt_mask_and_ack_irq (unsigned irq) {

    irq_desc[irq].handler->ack(irq);
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
void rt_unmask_irq (unsigned irq) {

    if (irq_desc[irq].handler->end != NULL)
	irq_desc[irq].handler->end(irq);
    else
	irq_desc[irq].handler->enable(irq);
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
void rt_ack_irq (unsigned irq) {

    rt_enable_irq(irq);
}

void rt_do_irq (unsigned irq) {

    adeos_trigger_irq(irq);
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
int rt_request_linux_irq (unsigned irq,
			  void (*handler)(int irq,
					  void *dev_id,
					  struct pt_regs *regs), 
			  char *name,
			  void *dev_id)
{
    unsigned long flags;

    if (irq >= NR_IRQS || !handler)
	return -EINVAL;

    rtai_local_irq_save(flags);

    spin_lock(&irq_desc[irq].lock);

    if (rtai_linux_irq[irq].count++ == 0 && irq_desc[irq].action)
	{
	rtai_linux_irq[irq].flags = irq_desc[irq].action->flags;
	irq_desc[irq].action->flags |= SA_SHIRQ;
	}

    spin_unlock(&irq_desc[irq].lock);

    rtai_local_irq_restore(flags);

    request_irq(irq,handler,SA_SHIRQ,name,dev_id);

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

    if (irq >= NR_IRQS || rtai_linux_irq[irq].count == 0)
	return -EINVAL;

    rtai_local_irq_save(flags);

    free_irq(irq,dev_id);

    spin_lock(&irq_desc[irq].lock);

    if (--rtai_linux_irq[irq].count == 0 && irq_desc[irq].action)
	irq_desc[irq].action->flags = rtai_linux_irq[irq].flags;

    spin_unlock(&irq_desc[irq].lock);

    rtai_local_irq_restore(flags);

    return 0;
}

/**
 * Pend an IRQ to Linux.
 *
 * rt_pend_linux_irq appends a Linux interrupt irq for processing in Linux IRQ
 * mode, i.e. with hardware interrupts fully enabled.
 *
 * @note rt_pend_linux_irq does not perform any check on @a irq.
 */
void rt_pend_linux_irq (unsigned irq) {

    adeos_propagate_irq(irq);
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
int rt_request_srq (unsigned label,
		    void (*k_handler)(void),
		    long long (*u_handler)(unsigned))
{
    unsigned long flags;
    int srq;

    if (k_handler == NULL)
	return -EINVAL;

    rtai_local_irq_save(flags);

    if (rtai_sysreq_map != ~0)
	{
	srq = ffz(rtai_sysreq_map);
	set_bit(srq,&rtai_sysreq_map);
	rtai_sysreq_table[srq].k_handler = k_handler;
	rtai_sysreq_table[srq].u_handler = u_handler;
	rtai_sysreq_table[srq].label = label;
	/* Set trap handler for sysreq. */
	if (__adeos_handle_trap == NULL) 
	    __adeos_handle_trap = rtai_uvec_handler;
	}
    else
	srq = -EBUSY;

    rtai_local_irq_restore(flags);

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
    if (srq < 2 || srq >= RTAI_NR_SRQS ||
	!test_and_clear_bit(srq,&rtai_sysreq_map))
	return -EINVAL;

    return 0;
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
    if (srq > 1 && srq < RTAI_NR_SRQS)
	{
	set_bit(srq,&rtai_sysreq_pending);
	adeos_schedule_irq(rtai_sysreq_virq);
	}
}

#ifdef CONFIG_SMP

#error "SMP is not supported"

#else /* !CONFIG_SMP */

#define rtai_critical_sync NULL

void rtai_broadcast_to_timers (int irq,
			       void *dev_id,
			       struct pt_regs *regs) {
} 

#endif /* CONFIG_SMP */



void rt_request_apic_timers (void (*handler)(void),
			     struct apic_timer_setup_data *tmdata) {
}

#define rt_free_apic_timers() rt_free_timer()


#ifdef CONFIG_SMP

#error "SMP is not supported"

#else  /* !CONFIG_SMP */
int rt_assign_irq_to_cpu (int irq, unsigned long cpus_mask) {

    return 0;
}

int rt_reset_irq_to_sym_mode (int irq) {

    return 0;
}

void rt_request_timer_cpuid (void (*handler)(void),
			     unsigned tick,
			     int cpuid) {
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
int rt_request_timer (void (*handler)(void),
		      unsigned tick,
		      int use_apic)
{
    unsigned long flags;

    TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_REQUEST,handler,tick);

    flags = rtai_critical_enter(rtai_critical_sync);

    rt_times.tick_time = rtai_rdtsc();
    rt_times.linux_tick = tb_ticks_per_jiffy;

    if (tick > 0)
	{
	if (tick > tb_ticks_per_jiffy)
	    tick = tb_ticks_per_jiffy;
	rt_times.intr_time = rt_times.tick_time + tick;
	rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
	rt_times.periodic_tick = tick;
#ifdef CONFIG_40x
        /* Set the PIT auto-reload mode */
        mtspr(SPRN_TCR, mfspr(SPRN_TCR) | TCR_ARE);
	/* Set the PIT reload value and just let it run. */
	mtspr(SPRN_PIT, tick);
#endif /* CONFIG_40x */
	}
    else
	{
	rt_times.intr_time = rt_times.tick_time + rt_times.linux_tick;
	rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
	rt_times.periodic_tick = rt_times.linux_tick;
#ifdef CONFIG_40x
	/* Disable the PIT auto-reload mode */
        mtspr(SPRN_TCR, mfspr(SPRN_TCR) & ~TCR_ARE);
#endif /* CONFIG_40x */
	}

    rtai_sync_level = 2;
    rt_release_irq(RTAI_TIMER_DECR_IRQ);
    if (rt_request_irq(RTAI_TIMER_DECR_IRQ,(rt_irq_handler_t)handler,NULL) < 0)
	{
	rtai_critical_exit(flags);
	return -EINVAL;
	}

    rt_set_timer_delay(rt_times.periodic_tick);

    rtai_critical_exit(flags);
    
    return 0;
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

    flags = rtai_critical_enter(rtai_critical_sync);

    rt_release_irq(RTAI_TIMER_DECR_IRQ);

#ifdef CONFIG_40x
    /* Re-enable the PIT auto-reload mode */
    mtspr(SPRN_TCR, mfspr(SPRN_TCR) | TCR_ARE);
    /* Set the PIT reload value and just let it run. */
    mtspr(SPRN_PIT, tb_ticks_per_jiffy);
#endif /* CONFIG_40x */

    rtai_critical_exit(flags);
}

#ifdef FIXME
RT_TRAP_HANDLER rt_set_trap_handler (RT_TRAP_HANDLER handler) {

    return (RT_TRAP_HANDLER)xchg(&rtai_trap_handler,handler);
}
#endif

void rt_mount (void) {

    MOD_INC_USE_COUNT;
    rtai_mount_count++;
    TRACE_RTAI_MOUNT();
}

void rt_umount (void) {

    TRACE_RTAI_UMOUNT();
    rtai_mount_count--;
    MOD_DEC_USE_COUNT;
}

static void rtai_irq_trampoline (unsigned irq)

{
    TRACE_RTAI_GLOBAL_IRQ_ENTRY(irq,0);

    if (rtai_realtime_irq[irq].handler)
	{
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	adeos_declare_cpuid;

#ifdef adeos_load_cpuid
	adeos_load_cpuid();
#endif /* adeos_load_cpuid */

	if (rtai_isr_nesting[cpuid]++ == 0 && rtai_isr_hook)
	    rtai_isr_hook(rtai_isr_nesting[cpuid]);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
	//printk("rtai_irq_trampoline(%d)\n", irq);
	rtai_realtime_irq[irq].handler(irq,rtai_realtime_irq[irq].cookie);
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	if (--rtai_isr_nesting[cpuid] == 0 && rtai_isr_hook)
	    rtai_isr_hook(rtai_isr_nesting[cpuid]);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
	}
    else
	adeos_propagate_irq(irq);

    TRACE_RTAI_GLOBAL_IRQ_EXIT();
}

#ifdef FIXME
static void rtai_trap_fault (adevinfo_t *evinfo)

{
    adeos_declare_cpuid;

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

    TRACE_RTAI_TRAP_ENTRY(evinfo->event,0);

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

#ifdef adeos_load_cpuid
    adeos_load_cpuid();
#endif /* adeos_load_cpuid */

    if (evinfo->domid == RTAI_DOMAIN_ID)
	{
	if (evinfo->event == 7)	/* (FPU) Device not available. */
	    {
	    /* Ok, this one is a bit insane: some RTAI examples use
	       the FPU in real-time mode while the TS bit is on from a
	       previous Linux switch, so this trap is raised. We just
	       simulate a math_state_restore() using the proper
	       "current" value from the Linux domain here to please
	       everyone without impacting the existing code. */

	    struct task_struct *linux_task = rtai_get_current(cpuid);

#if CONFIG_PREEMPT
	    /* See comment in math_state_restore() in
	       arch/i386/traps.c from a kpreempt-enabled kernel for
	       more on this. */
	    linux_task->preempt_count++;
#endif

	    if (linux_task->used_math)
		restore_task_fpenv(linux_task);	/* Does clts(). */
	    else
		{
		init_xfpu();	/* Does clts(). */
		linux_task->used_math = 1;
		}

	    linux_task->flags |= PF_USEDFPU;

#if CONFIG_PREEMPT
	    linux_task->preempt_count--;
#endif

	    goto endtrap;
	    }

	if (rtai_trap_handler != NULL &&
	    (test_bit(cpuid,&rtai_cpu_realtime) || test_bit(cpuid,&rtai_cpu_lxrt)) &&
	    rtai_trap_handler(evinfo->event,
			      trap2sig[evinfo->event],
			      (struct pt_regs *)evinfo->evdata,
			      NULL) != 0)
	    goto endtrap;
	}

    adeos_propagate_event(evinfo);

  endtrap:

    TRACE_RTAI_TRAP_EXIT();
}

#endif /* FIXME */

static void rtai_ssrq_trampoline (unsigned virq)

{
    unsigned long pending;

    spin_lock(&rtai_ssrq_lock);

    while ((pending = rtai_sysreq_pending & ~rtai_sysreq_running) != 0)
	{
	unsigned srq = ffnz(pending);
	set_bit(srq,&rtai_sysreq_running);
	clear_bit(srq,&rtai_sysreq_pending);
	spin_unlock(&rtai_ssrq_lock);

	if (test_bit(srq,&rtai_sysreq_map))
	    rtai_sysreq_table[srq].k_handler();

	clear_bit(srq,&rtai_sysreq_running);
	spin_lock(&rtai_ssrq_lock);
	}

    spin_unlock(&rtai_ssrq_lock);
}

/* For the time being we use the trap exception for sysreq like in 
   the old RTAI implementation. Lateron we try using system calls. */

static int rtai_uvec_handler (struct pt_regs *regs)
{
    unsigned long vec, srq, label; 
    long long retval = 0;

    if (regs->gpr[0] && 
	regs->gpr[0] == ((srq = regs->gpr[3]) + (label = regs->gpr[4]))) {

    if (!(vec = srq >> 24)) 
	{
	TRACE_RTAI_SRQ_ENTRY(srq, !user_mode(regs));
	    
	if (srq > 1 && srq < RTAI_NR_SRQS &&
	    test_bit(srq,&rtai_sysreq_map) &&
	    rtai_sysreq_table[srq].u_handler != NULL)
	    retval = rtai_sysreq_table[srq].u_handler(label);
	else
	    for (srq = 2; srq < RTAI_NR_SRQS; srq++)
		if (test_bit(srq,&rtai_sysreq_map) &&
		    rtai_sysreq_table[srq].label == label)
		    retval = (long long)srq;

	TRACE_RTAI_SRQ_EXIT();
	} 
    else 
	{
	rt_printk("rtai_usrq_handler: invalid vector 0x%ld detected",
		  vec);
	}
    regs->gpr[0] = 0;
    regs->gpr[3] = ((unsigned long *)&retval)[0];
    regs->gpr[4] = ((unsigned long *)&retval)[1];
    regs->nip += 4;

    return 0;
    } 
    else 
	{
	return 1;
	}
}


#ifdef FIXME
static struct mmreq {
    int in, out, count;
#define MAX_MM 32  /* Should be more than enough (must be a power of 2). */
#define bump_mmreq(x) do { x = (x + 1) & (MAX_MM - 1); } while(0)
    struct mm_struct *mm[MAX_MM];
} rtai_mmrqtab[NR_CPUS];

static void rtai_linux_schedule_head (adevinfo_t *evinfo)

{
    struct { struct task_struct *prev, *next; } *evdata = (__typeof(evdata))evinfo->evdata;
    struct task_struct *prev = evdata->prev;

    /* The SCHEDULE_HEAD event is sent by the (Adeosized) Linux kernel
       each time it's about to switch a process out. This hook is
       aimed at preventing the last active MM from being dropped
       during the LXRT real-time operations since it's a lengthy
       atomic operation. See kernel/sched.c (schedule()) for more. The
       MM dropping is simply postponed until the SCHEDULE_TAIL event
       is received, right after the incoming task has been switched
       in. */

    if (!prev->mm)
	{
	struct mmreq *p = rtai_mmrqtab + prev->processor;
	struct mm_struct *oldmm = prev->active_mm;
	BUG_ON(p->count >= MAX_MM);
	/* Prevent the MM from being dropped in schedule(), then pend
	   a request to drop it later in rtai_linux_schedule_tail(). */
	atomic_inc(&oldmm->mm_count);
	p->mm[p->in] = oldmm;
	bump_mmreq(p->in);
	p->count++;
	}

    adeos_propagate_event(evinfo);
}

static void rtai_linux_schedule_tail (adevinfo_t *evinfo)

{
    struct mmreq *p;

    if (evinfo->domid == RTAI_DOMAIN_ID)
	/* About to resume after the transition to hard LXRT mode.  Do
	   _not_ propagate this event so that Linux's tail scheduling
	   won't be performed. */
	return;

    p = rtai_mmrqtab + smp_processor_id();

    while (p->out != p->in)
	{
	struct mm_struct *oldmm = p->mm[p->out];
	mmdrop(oldmm);
	bump_mmreq(p->out);
	p->count--;
	}

    adeos_propagate_event(evinfo);
}

void rtai_switch_linux_mm (struct task_struct *prev,
			   struct task_struct *next,
			   int cpuid)
{
    struct mm_struct *oldmm = prev->active_mm;

    switch_mm(oldmm,next->active_mm,next,cpuid);

    if (!next->mm)
	enter_lazy_tlb(oldmm,next,cpuid);
}

static void rtai_linux_signal_process (adevinfo_t *evinfo)

{
    if (rtai_signal_handler)
	{
	struct { struct task_struct *task; int sig; } *evdata = (__typeof(evdata))evinfo->evdata;
	struct task_struct *task = evdata->task;

	if (evdata->sig == SIGKILL &&
	    (task->policy == SCHED_FIFO || task->policy == SCHED_RR) &&
	    task->ptd[0])
	    {
	    if (!rtai_signal_handler(task,evdata->sig))
		/* Don't propagate so that Linux won't further process
		   the signal. */
		return;
	    }
	}

    adeos_propagate_event(evinfo);
}

void rtai_attach_lxrt (void)

{
    /* Must be called on behalf of the Linux domain. */
    adeos_catch_event(ADEOS_SCHEDULE_TAIL,&rtai_linux_schedule_tail);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,&rtai_linux_schedule_head);
    adeos_catch_event(ADEOS_SIGNAL_PROCESS,&rtai_linux_signal_process);
}

void rtai_detach_lxrt (void)

{
    unsigned long flags;
    struct mmreq *p;
    
    /* Must be called on behalf of the Linux domain. */
    adeos_catch_event(ADEOS_SIGNAL_PROCESS,NULL);
    adeos_catch_event(ADEOS_SCHEDULE_HEAD,NULL);
    adeos_catch_event(ADEOS_SCHEDULE_TAIL,NULL);

    flags = rtai_critical_enter(NULL);

    /* Flush the MM log for all processors */
    for (p = rtai_mmrqtab; p < rtai_mmrqtab + NR_CPUS; p++)
	{
	while (p->out != p->in)
	    {
	    struct mm_struct *oldmm = p->mm[p->out];
	    mmdrop(oldmm);
	    bump_mmreq(p->out);
	    p->count--;
	    }
	}

    rtai_critical_exit(flags);
}
#endif /* FIXME */

static void rtai_install_archdep (void)

{
    adsysinfo_t sysinfo;

    adeos_get_sysinfo(&sysinfo);
    if (sysinfo.archdep.tmirq != RTAI_TIMER_DECR_IRQ)
	{
	printk("RTAI/Adeos: the timer interrupt %d is not supported\n",
	       sysinfo.archdep.tmirq);
	}
    
    if (rtai_cpufreq_arg == 0)
	{
	rtai_cpufreq_arg = (unsigned long)sysinfo.cpufreq;
	}
    rtai_tunables.cpu_freq = rtai_cpufreq_arg;
}

static void rtai_uninstall_archdep (void) {

    unsigned long flags;

    flags = rtai_critical_enter(NULL);
    __adeos_handle_trap = 0;
    rtai_critical_exit(flags);
}


void (*rt_set_ihook (void (*hookfn)(int)))(int) {

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
    return (void (*)(int))xchg(&rtai_isr_hook,hookfn); /* This is atomic */
#else  /* !CONFIG_RTAI_SCHED_ISR_LOCK */
    return NULL;
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
}

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rtai_proc_root = NULL;

static int rtai_read_proc (char *page,
			   char **start,
			   off_t off,
			   int count,
			   int *eof,
			   void *data)
{
    PROC_PRINT_VARS;
    int i, none;

    PROC_PRINT("\n** RTAI/ppc over Adeos:\n\n");
    PROC_PRINT("    RTAI mount count: %d\n",rtai_mount_count);
    PROC_PRINT("    Decr. Frequency: %lu\n",rtai_tunables.cpu_freq);
    PROC_PRINT("    Decr. Latency: %d ns\n",RTAI_LATENCY_8254);
    PROC_PRINT("    Decr. Setup Time: %d ns\n",RTAI_SETUP_TIME_8254);
    
    none = 1;

    PROC_PRINT("\n** Real-time IRQs used by RTAI: ");

    for (i = 0; i < RTAI_NR_IRQS; i++)
	{
	if (rtai_realtime_irq[i].handler)
	    {
	    if (none)
		{
		PROC_PRINT("\n");
		none = 0;
		}

	    PROC_PRINT("\n    #%d at %p", i, rtai_realtime_irq[i].handler);
	    }
        }

    if (none)
	PROC_PRINT("none");

    PROC_PRINT("\n\n");

#ifdef FIXME
    PROC_PRINT("** RTAI extension traps: \n\n");
    PROC_PRINT("    SYSREQ=0x%x\n",RTAI_SYS_VECTOR);
    PROC_PRINT("      LXRT=0x%x\n",RTAI_LXRT_VECTOR);
    PROC_PRINT("       SHM=0x%x\n\n",RTAI_SHM_VECTOR);
#endif

    none = 1;
    PROC_PRINT("** RTAI SYSREQs in use: ");

    for (i = 0; i < RTAI_NR_SRQS; i++)
	{
	if (rtai_sysreq_table[i].k_handler ||
	    rtai_sysreq_table[i].u_handler)
	    {
	    PROC_PRINT("#%d ", i);
	    none = 0;
	    }
        }

    if (none)
	PROC_PRINT("none");

    PROC_PRINT("\n\n");

    PROC_PRINT_DONE;
}

static int rtai_proc_register (void)

{
    struct proc_dir_entry *ent;

    rtai_proc_root = create_proc_entry("rtai",S_IFDIR, 0);

    if (!rtai_proc_root)
	{
	printk("Unable to initialize /proc/rtai.\n");
	return -1;
        }

    rtai_proc_root->owner = THIS_MODULE;

    ent = create_proc_entry("rtai",S_IFREG|S_IRUGO|S_IWUSR,rtai_proc_root);

    if (!ent)
	{
	printk("Unable to initialize /proc/rtai/rtai.\n");
	return -1;
        }

    ent->read_proc = rtai_read_proc;

    return 0;
}

static void rtai_proc_unregister (void)

{
    remove_proc_entry("rtai",rtai_proc_root);
    remove_proc_entry("rtai",0);
}
#endif /* CONFIG_PROC_FS */

static void rtai_domain_entry (int iflag)

{
    unsigned irq;
#if FIXME
    unsigned trapnr;
#endif

    if (iflag)
	{
	for (irq = 0; irq < NR_IRQS; irq++)
	    adeos_virtualize_irq(irq,
				 &rtai_irq_trampoline,
				 NULL,
				 IPIPE_DYNAMIC_MASK);
	/* Decrementer trap = virtual timer interrupt */
	adeos_virtualize_irq(RTAI_TIMER_DECR_IRQ,
			     &rtai_irq_trampoline,
			     NULL,
			     IPIPE_DYNAMIC_MASK);

#if FIXME
	/* Trap all faults. */
	for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++)
	    adeos_catch_event(trapnr,&rtai_trap_fault);
#endif

	printk("RTAI %s mounted over Adeos %s.\n",PACKAGE_VERSION,ADEOS_VERSION_STRING);
	}

    for (;;)
	adeos_suspend_domain();
}

int init_module (void)

{
    unsigned long flags;
    int key0, key1;
    adattr_t attr;

    /* Allocate a virtual interrupt to handle sysreqs within the Linux
       domain. */
    rtai_sysreq_virq = adeos_alloc_irq();
    printk("RTAI/Adeos: rtai_sysreq_virq=%d\n",rtai_sysreq_virq); 

    if (!rtai_sysreq_virq)
	{
	printk("RTAI/Adeos: no virtual interrupt available.\n");
	return 1;
	}

    /* Reserve the first two _consecutive_ per-thread data key in the
       Linux domain. This is rather crappy, since we depend on
       statically defined PTD key values, which is exactly what the
       PTD scheme is here to prevent. Unfortunately, reserving these
       specific keys is the only way to remain source compatible with
       the current LXRT implementation. */
    flags = rtai_critical_enter(NULL);
    rtai_adeos_ptdbase = key0 = adeos_alloc_ptdkey();
    key1 = adeos_alloc_ptdkey();
    rtai_critical_exit(flags);

    if (key0 != 0 && key1 != 1)
	{
	printk("RTAI/Adeos: per-thread keys #0 and/or #1 are busy.\n");
	return 1;
	}

    adeos_virtualize_irq(rtai_sysreq_virq,
			 &rtai_ssrq_trampoline,
			 NULL,
			 IPIPE_HANDLE_MASK);

    rtai_install_archdep();

    rtai_mount_count = 1;

#ifdef CONFIG_PROC_FS
    rtai_proc_register();
#endif

    /* Let Adeos do its magic for our real-time domain. */
    adeos_init_attr(&attr);
    attr.name = "RTAI";
    attr.domid = RTAI_DOMAIN_ID;
    attr.entry = &rtai_domain_entry;
    attr.priority = ADEOS_ROOT_PRI + 100; /* Precede Linux in the pipeline */

    return adeos_register_domain(&rtai_domain,&attr);
}

void cleanup_module (void)

{
#ifdef CONFIG_PROC_FS
    rtai_proc_unregister();
#endif

    adeos_virtualize_irq(rtai_sysreq_virq,NULL,NULL,0);
    adeos_free_irq(rtai_sysreq_virq);
    rtai_uninstall_archdep();
    adeos_free_ptdkey(rtai_adeos_ptdbase); /* #0 and #1 actually */
    adeos_free_ptdkey(rtai_adeos_ptdbase + 1);
    adeos_unregister_domain(&rtai_domain);
    printk("RTAI/Adeos unmounted.\n");
}

/*@}*/
