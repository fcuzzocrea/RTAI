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
 *   Porting to x86_64 architecture:
 *   Copyright &copy; 2005 Paolo Mantegazza, \n
 *   Copyright &copy; 2005 Daniele Gasperini \n
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

#ifdef CONFIG_X86_LOCAL_APIC

#undef  NR_IRQS
#define NR_IRQS  IPIPE_NR_XIRQS

static unsigned long rtai_apicfreq_arg = RTAI_CALIBRATED_APIC_FREQ;

MODULE_PARM(rtai_apicfreq_arg,"i");

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

#else /* !CONFIG_X86_LOCAL_APIC */

#define rtai_setup_periodic_apic(count, vector);

#define rtai_setup_oneshot_apic(count, vector);

#endif /* CONFIG_X86_LOCAL_APIC */

struct { volatile int locked, rqsted; } rt_scheduling[RTAI_NR_CPUS];

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
static void (*rtai_isr_hook)(int cpuid);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

extern struct gate_struct idt_table[];

adomain_t rtai_domain;

struct {
	int (*handler)(unsigned irq, void *cookie);
	void *cookie;
	int retmode;
} rtai_realtime_irq[NR_IRQS];

static struct {
	unsigned long flags;
	int count;
} rtai_linux_irq[NR_IRQS];

static struct {
	void (*k_handler)(void);
	long long (*u_handler)(unsigned long);
	unsigned label;
} rtai_sysreq_table[RTAI_NR_SRQS];

adeos_handle_irq_t adeos_default_irq_handler;

static unsigned rtai_sysreq_virq;

static unsigned long rtai_sysreq_map = 3; /* srqs #[0-1] are reserved */

static unsigned long rtai_sysreq_pending;

static unsigned long rtai_sysreq_running;

static spinlock_t rtai_ssrq_lock = SPIN_LOCK_UNLOCKED;

static volatile int rtai_sync_level;

static atomic_t rtai_sync_count = ATOMIC_INIT(1);

static int rtai_last_8254_counter2;

static RTIME rtai_ts_8254;

static struct gate_struct rtai_sysvec;

static RT_TRAP_HANDLER rtai_trap_handler;

struct rt_times rt_times;

struct rt_times rt_smp_times[RTAI_NR_CPUS];

struct rtai_switch_data rtai_linux_context[RTAI_NR_CPUS];

struct calibration_data rtai_tunables;

volatile unsigned long rtai_cpu_realtime;

volatile unsigned long rtai_cpu_lock;

int rtai_adeos_ptdbase = -1;

unsigned long rtai_critical_enter (void (*synch)(void))
{
	unsigned long flags = adeos_critical_enter(synch);

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

int rt_request_irq (unsigned irq, int (*handler)(unsigned irq, void *cookie), void *cookie, int retmode)
{
	unsigned long flags;

	if (handler == NULL || irq >= NR_IRQS) {
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
	return 0;
}

int rt_release_irq (unsigned irq)
{
	unsigned long flags;
	if (irq >= NR_IRQS || !rtai_realtime_irq[irq].handler) {
		return -EINVAL;
	}
	flags = rtai_critical_enter(NULL);
	rtai_realtime_irq[irq].handler = NULL;
	rtai_critical_exit(flags);
	return 0;
}

void rt_set_irq_cookie (unsigned irq, void *cookie)
{
	if (irq < NR_IRQS) {
		rtai_realtime_irq[irq].cookie = cookie;
	}
}

void rt_set_irq_retmode (unsigned irq, int retmode)
{
	if (irq < NR_IRQS) {
		rtai_realtime_irq[irq].retmode = retmode ? 1 : 0;
	}
}

/* We use what follos in place of ADEOS ones to play safely with 
   Linux preemption and to quickly manage Linux interrupt state.
   In what follows comments of the type "//num)" report the ADEOS 
   call that is explicitely recoded "num" lines above, for greater 
   efficiency. */

extern struct hw_interrupt_type __adeos_std_irq_dtype[];
extern unsigned long io_apic_irqs;

#define BEGIN_PIC() \
do { \
	rtai_save_flags_and_cli(flags); \
	cpuid = rtai_cpuid(); \
	lflags = xchg(&adp_root->cpudata[cpuid].status, 1 << IPIPE_STALL_FLAG); \
	rtai_save_and_lock_preempt_count()

#define END_PIC() \
	rtai_restore_preempt_count(); \
	adp_root->cpudata[cpuid].status = lflags; \
	rtai_restore_flags(flags); \
} while (0)

unsigned rt_startup_irq (unsigned irq)
{
        unsigned long flags, lflags;
        int retval, cpuid;

	BEGIN_PIC();
	__adeos_unlock_irq(adp_root, irq);
	retval = __adeos_std_irq_dtype[irq].startup(irq);
	END_PIC();
        return retval;
}

void rt_shutdown_irq (unsigned irq)
{
        unsigned long flags, lflags;
        int cpuid;

	BEGIN_PIC();
	__adeos_std_irq_dtype[irq].shutdown(irq);
	__adeos_clear_irq(adp_root, irq);
	END_PIC();
}

static inline void _rt_enable_irq (unsigned irq)
{
        unsigned long flags, lflags;
	int cpuid;

	BEGIN_PIC();
	__adeos_unlock_irq(adp_root, irq);
	__adeos_std_irq_dtype[irq].enable(irq);
	END_PIC();
}

void rt_enable_irq (unsigned irq)
{
	_rt_enable_irq(irq);
}

void rt_disable_irq (unsigned irq)
{
        unsigned long flags, lflags;
	int cpuid;

	BEGIN_PIC();
	__adeos_std_irq_dtype[irq].disable(irq);
	__adeos_lock_irq(adp_root, cpuid, irq);
	END_PIC();
}

static inline void _rt_end_irq (unsigned irq)
{
        unsigned long flags, lflags;
	int cpuid;

	BEGIN_PIC();
	if (
#ifdef CONFIG_X86_IO_APIC
	    !IO_APIC_IRQ(irq) ||
#endif /* CONFIG_X86_IO_APIC */
	    !(irq_desc[irq].status & (IRQ_DISABLED | IRQ_INPROGRESS))) {
		__adeos_unlock_irq(adp_root, irq);
	}
	__adeos_std_irq_dtype[irq].end(irq);
	END_PIC();
}

void rt_end_irq (unsigned irq)
{
	_rt_end_irq(irq);
}

void rt_mask_and_ack_irq (unsigned irq)
{
        irq_desc[irq].handler->ack(irq);
}

void rt_unmask_irq (unsigned irq)
{
	_rt_end_irq(irq);
}

void rt_ack_irq (unsigned irq)
{
	_rt_enable_irq(irq);
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
			  irqreturn_t (*handler)(int irq,
			  void *dev_id,
			  struct pt_regs *regs), 
			  char *name,
			  void *dev_id)
{
    unsigned long flags;

    if (irq >= NR_IRQS || !handler) {
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

    if (irq >= NR_IRQS || rtai_linux_irq[irq].count == 0)
	return -EINVAL;

    rtai_save_flags_and_cli(flags);

    free_irq(irq,dev_id);

    spin_lock(&irq_desc[irq].lock);

    if (--rtai_linux_irq[irq].count == 0 && irq_desc[irq].action)
	irq_desc[irq].action->flags = rtai_linux_irq[irq].flags;

    spin_unlock(&irq_desc[irq].lock);

    rtai_restore_flags(flags);

    return 0;
}

volatile unsigned long adeos_pended;

#define adeos_pend_irq(irq) \
do { \
	unsigned long flags; \
	rtai_save_flags_and_cli(flags); \
	adeos_pend_uncond(irq, rtai_cpuid()); \
	rtai_restore_flags(flags); \
} while (0)

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
	adeos_pend_irq(irq);
//1)	adeos_schedule_irq(irq);
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
		    long long (*u_handler)(unsigned long))
{
    unsigned long flags;
    int srq;

    if (k_handler == NULL)
	return -EINVAL;

    rtai_save_flags_and_cli(flags);

    if (rtai_sysreq_map != ~0)
	{
	srq = ffz(rtai_sysreq_map);
	set_bit(srq, &rtai_sysreq_map);
	rtai_sysreq_table[srq].k_handler = k_handler;
	rtai_sysreq_table[srq].u_handler = u_handler;
	rtai_sysreq_table[srq].label = label;
	}
    else
	srq = -EBUSY;

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
	return  (srq < 2 || srq >= RTAI_NR_SRQS || !test_and_clear_bit(srq, &rtai_sysreq_map)) ? -EINVAL : 0;
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
		set_bit(srq, &rtai_sysreq_pending);
		adeos_pend_irq(rtai_sysreq_virq);
//1)		adeos_schedule_irq(rtai_sysreq_virq);
	}
}

#ifdef CONFIG_X86_LOCAL_APIC

irqreturn_t rtai_broadcast_to_local_timers (int irq,
					    void *dev_id,
					    struct pt_regs *regs)
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

#ifdef CONFIG_SMP

static unsigned long rtai_old_irq_affinity[NR_IRQS],
                     rtai_set_irq_affinity[NR_IRQS];

static spinlock_t rtai_iset_lock = SPIN_LOCK_UNLOCKED;

static long long rtai_timers_sync_time;

static struct apic_timer_setup_data rtai_timer_mode[RTAI_NR_CPUS];

static void rtai_critical_sync (void)
{
    struct apic_timer_setup_data *p;

    switch (rtai_sync_level)
	{
	case 1:

	    p = &rtai_timer_mode[rtai_cpuid()];
	    
	    while (rtai_rdtsc() < rtai_timers_sync_time)
		;

	    if (p->mode)
		rtai_setup_periodic_apic(p->count,RTAI_APIC_TIMER_VECTOR);
	    else
		rtai_setup_oneshot_apic(p->count,RTAI_APIC_TIMER_VECTOR);

	    break;

	case 2:

	    rtai_setup_oneshot_apic(0,RTAI_APIC_TIMER_VECTOR);
	    break;

	case 3:

	    rtai_setup_periodic_apic(RTAI_APIC_ICOUNT,LOCAL_TIMER_VECTOR);
	    break;
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
void rt_request_apic_timers (void (*handler)(void),
			     struct apic_timer_setup_data *tmdata)
{
    volatile struct rt_times *rtimes;
    struct apic_timer_setup_data *p;
    unsigned long flags;
    int cpuid;

    TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_REQUEST_APIC,handler,0);

    flags = rtai_critical_enter(rtai_critical_sync);

    rtai_sync_level = 1;

    rtai_timers_sync_time = rtai_rdtsc() + rtai_imuldiv(LATCH,
							rtai_tunables.cpu_freq,
							RTAI_FREQ_8254);
    for (cpuid = 0; cpuid < RTAI_NR_CPUS; cpuid++)
	{
	p = &rtai_timer_mode[cpuid];
	*p = tmdata[cpuid];
	rtimes = &rt_smp_times[cpuid];

	if (p->mode)
	    {
	    rtimes->linux_tick = RTAI_APIC_ICOUNT;
	    rtimes->tick_time = rtai_llimd(rtai_timers_sync_time,
					   RTAI_FREQ_APIC,
					   rtai_tunables.cpu_freq);
	    rtimes->periodic_tick = rtai_imuldiv(p->count,
						 RTAI_FREQ_APIC,
						 1000000000);
	    p->count = rtimes->periodic_tick;
	    }
	else
	    {
	    rtimes->linux_tick = rtai_imuldiv(LATCH,
					      rtai_tunables.cpu_freq,
					      RTAI_FREQ_8254);
	    rtimes->tick_time = rtai_timers_sync_time;
	    rtimes->periodic_tick = rtimes->linux_tick;
	    p->count = RTAI_APIC_ICOUNT;
	    }

	rtimes->intr_time = rtimes->tick_time + rtimes->periodic_tick;
	rtimes->linux_time = rtimes->tick_time + rtimes->linux_tick;
	}

    p = &rtai_timer_mode[rtai_cpuid()];

    while (rtai_rdtsc() < rtai_timers_sync_time)
	;

    if (p->mode)
	rtai_setup_periodic_apic(p->count,RTAI_APIC_TIMER_VECTOR);
    else
	rtai_setup_oneshot_apic(p->count,RTAI_APIC_TIMER_VECTOR);

    rt_release_irq(RTAI_APIC_TIMER_IPI);

    rt_request_irq(RTAI_APIC_TIMER_IPI, (rt_irq_handler_t)handler, NULL, 0);

    REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();

    for (cpuid = 0; cpuid < RTAI_NR_CPUS; cpuid++)
	{
	p = &tmdata[cpuid];

	if (p->mode)
	    p->count = rtai_imuldiv(p->count,RTAI_FREQ_APIC,1000000000);
	else
	    p->count = rtai_imuldiv(p->count,rtai_tunables.cpu_freq,1000000000);
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
    rt_release_irq(RTAI_APIC_TIMER_IPI);

    rtai_critical_exit(flags);
}

/**
 * Set IRQ->CPU assignment
 *
 * rt_assign_irq_to_cpu forces the assignment of the external interrupt @a irq
 * to the CPU @a cpu.
 *
 * @retval 1 if there is one CPU in the system.
 * @retval 0 on success if there are at least 2 CPUs.
 * @return the number of CPUs if @a cpu refers to a non-existent CPU.
 * @retval EINVAL if @a irq is not a valid IRQ number or some internal data
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
    unsigned long oldmask, flags;

    rtai_save_flags_and_cli(flags);

    spin_lock(&rtai_iset_lock);

    oldmask = CPUMASK(adeos_set_irq_affinity(irq, CPUMASK_T(cpumask)));

    if (oldmask == 0)
	{
	/* Oops... Something went wrong. */
	spin_unlock(&rtai_iset_lock);
	rtai_restore_flags(flags);
	return -EINVAL;
	}

    rtai_old_irq_affinity[irq] = oldmask;
    rtai_set_irq_affinity[irq] = cpumask;

    spin_unlock(&rtai_iset_lock);

    rtai_restore_flags(flags);

    return 0;
}

/**
 * reset IRQ->CPU assignment
 *
 * rt_reset_irq_to_sym_mode resets the interrupt irq to the symmetric interrupts
 * management. The symmetric mode distributes the IRQs over all the CPUs.
 *
 * @retval 1 if there is one CPU in the system.
 * @retval 0 on success if there are at least 2 CPUs.
 * @return the number of CPUs if @a cpu refers to a non-existent CPU.
 * @retval EINVAL if @a irq is not a valid IRQ number or some internal data
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

    rtai_save_flags_and_cli(flags);

    spin_lock(&rtai_iset_lock);

    if (rtai_old_irq_affinity[irq] == 0)
	{
	spin_unlock(&rtai_iset_lock);
	rtai_restore_flags(flags);
	return -EINVAL;
	}

    oldmask = CPUMASK(adeos_set_irq_affinity(irq, CPUMASK_T(0))); /* Query -- no change. */

    if (oldmask == rtai_set_irq_affinity[irq])
	{
	/* Ok, proceed since nobody changed it in the meantime. */
	adeos_set_irq_affinity(irq, CPUMASK_T(rtai_old_irq_affinity[irq]));
	rtai_old_irq_affinity[irq] = 0;
	}

    spin_unlock(&rtai_iset_lock);

    rtai_restore_flags(flags);

    return 0;
}

#else  /* !CONFIG_SMP */

#define rtai_critical_sync NULL

void rt_request_apic_timers (void (*handler)(void),
			     struct apic_timer_setup_data *tmdata) {
}

void rt_free_apic_timers(void) {
    rt_free_timer();
}

int rt_assign_irq_to_cpu (int irq, unsigned long cpus_mask) {

    return 0;
}

int rt_reset_irq_to_sym_mode (int irq) {

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
			rtai_setup_periodic_apic(tick,RTAI_APIC_TIMER_VECTOR);
			retval = REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();
		} else {
			outb(0x34, 0x43);
			outb(tick & 0xff, 0x40);
			outb(tick >> 8, 0x40);
			rt_release_irq(RTAI_TIMER_8254_IRQ);
		    	retval = rt_request_irq(RTAI_TIMER_8254_IRQ, (rt_irq_handler_t)handler, NULL, 0);
		}
	} else {
		rt_times.linux_tick = rtai_imuldiv(LATCH,rtai_tunables.cpu_freq,RTAI_FREQ_8254);
		rt_times.intr_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.linux_time = rt_times.tick_time + rt_times.linux_tick;
		rt_times.periodic_tick = rt_times.linux_tick;

		if (use_apic) {
			rt_release_irq(RTAI_APIC_TIMER_IPI);
			rt_request_irq(RTAI_APIC_TIMER_IPI, (rt_irq_handler_t)handler, NULL, 0);
			rtai_setup_oneshot_apic(RTAI_APIC_ICOUNT,RTAI_APIC_TIMER_VECTOR);
    			retval = REQUEST_LINUX_IRQ_BROADCAST_TO_APIC_TIMERS();
		} else {
			outb(0x30, 0x43);
			outb(LATCH & 0xff, 0x40);
			outb(LATCH >> 8, 0x40);
			rt_release_irq(RTAI_TIMER_8254_IRQ);
			retval = rt_request_irq(RTAI_TIMER_8254_IRQ, (rt_irq_handler_t)handler, NULL, 0);
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
		used_apic = 0;
	} else {
		outb(0x34, 0x43);
		outb(LATCH & 0xff, 0x40);
		outb(LATCH >> 8,0x40);
		rt_release_irq(RTAI_TIMER_8254_IRQ);
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

    rtai_hw_save_flags_and_cli(flags);	/* local hw masking is required here. */
    outb(0xD8,0x43);
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

    outb_p(0x00,0x43);
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

#define CHECK_KERCTX();

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,9)
#define ADEOS_TICK_REGS __adeos_tick_regs[cpuid]
#else
#define ADEOS_TICK_REGS __adeos_tick_regs
#endif

static int rtai_irq_trampoline (struct pt_regs *regs) {
	unsigned long lflags;
	int cpuid = rtai_cpuid();
	int irq = regs->orig_rax & 0xFF;
	
TRACE_RTAI_GLOBAL_IRQ_ENTRY(irq,0);

	CHECK_KERCTX();

	lflags = xchg(&adp_root->cpudata[cpuid].status, (1 << IPIPE_STALL_FLAG));
	
	adp_root->irqs[irq].acknowledge(irq); mb();
	if (rtai_realtime_irq[irq].handler) {
		RTAI_SCHED_ISR_LOCK();
                if (rtai_realtime_irq[irq].retmode &&
			((int (*)(int, void *))rtai_realtime_irq[irq].handler)(irq, rtai_realtime_irq[irq].cookie)) {
			RTAI_SCHED_ISR_UNLOCK();
			adp_root->cpudata[cpuid].status = lflags;
//1)			adeos_restore_pipeline_nosync(adp_root, lflags, cpuid);
			return 0;
                } else {
			rtai_realtime_irq[irq].handler(irq, rtai_realtime_irq[irq].cookie);
			RTAI_SCHED_ISR_UNLOCK();
		}
	} else {
		adeos_pend_uncond(irq, cpuid);
//1)		adeos_schedule_irq(irq);
	}
	adp_root->cpudata[cpuid].status = lflags;
//1)	adeos_restore_pipeline_nosync(adp_root, lflags, cpuid);

	if (test_and_clear_bit(cpuid, &adeos_pended) && !test_bit(IPIPE_STALL_FLAG, &lflags)) {
//1)	if (test_and_clear_bit(cpuid, &adeos_pended) && !adeos_test_pipeline_from(adp_root)) {
//		rtai_sti();
		if (irq == __adeos_tick_irq) {
			ADEOS_TICK_REGS.eflags = regs->eflags;
			ADEOS_TICK_REGS.rip = regs->rip;
			ADEOS_TICK_REGS.cs = regs->cs;
			ADEOS_TICK_REGS.rsp = regs->rsp;
#if defined(CONFIG_SMP) && defined(CONFIG_FRAME_POINTER)
			ADEOS_TICK_REGS.rbp = regs->rbp;
#endif /* CONFIG_SMP && CONFIG_FRAME_POINTER */
        	}
//		rtai_cli();
		if (adp_root->cpudata[cpuid].irq_pending_hi != 0) {
			__adeos_sync_stage(IPIPE_IRQMASK_ANY);
		}
//3)		adeos_unstall_pipeline_from(adp_root);
		return 1;
        }

TRACE_RTAI_GLOBAL_IRQ_EXIT();

	return 0;
}

//#define HINT_DIAG_ECHO
//#define HINT_DIAG_TRAPS

#ifdef HINT_DIAG_ECHO
#define HINT_DIAG_MSG(x) x
#else
#define HINT_DIAG_MSG(x)
#endif

static void rtai_trap_fault (adevinfo_t *evinfo) {
    adeos_propagate_event(evinfo);
}

static void rtai_ssrq_trampoline (unsigned virq)
{
    unsigned long pending;

    spin_lock(&rtai_ssrq_lock);
    while ((pending = rtai_sysreq_pending & ~rtai_sysreq_running) != 0) {
	unsigned srq = ffnz(pending);
	set_bit(srq,&rtai_sysreq_running);
	clear_bit(srq,&rtai_sysreq_pending);
	spin_unlock(&rtai_ssrq_lock);

	if (test_bit(srq,&rtai_sysreq_map)) {
	    rtai_sysreq_table[srq].k_handler();
	}

	clear_bit(srq,&rtai_sysreq_running);
	spin_lock(&rtai_ssrq_lock);
    }
    spin_unlock(&rtai_ssrq_lock);
}

static inline long long rtai_usrq_trampoline (unsigned srq, unsigned long label)
{
    long long r = 0;

    TRACE_RTAI_SRQ_ENTRY(srq);

    if (srq > 1 && srq < RTAI_NR_SRQS && test_bit(srq,&rtai_sysreq_map) && rtai_sysreq_table[srq].u_handler != NULL) {
	r = rtai_sysreq_table[srq].u_handler(label); 
    } else {
	for (srq = 2; srq < RTAI_NR_SRQS; srq++) {
	    if (test_bit(srq,&rtai_sysreq_map) && rtai_sysreq_table[srq].label == label) {
		 r = (long long)srq;
	    }
	}
    }

    TRACE_RTAI_SRQ_EXIT();

    return r;
}

#include <asm/rtai_usi.h>
long long (*rtai_lxrt_dispatcher)(unsigned long, unsigned long);

void rtai_syscall_entry (struct pt_regs *regs) {
#ifdef USI_SRQ_MASK
	IF_IS_A_USI_SRQ_CALL_IT();
#endif

	*((unsigned long long *)regs->rdx) = (long)regs->rax > RTAI_NR_SRQS ? rtai_lxrt_dispatcher(regs->rax, regs->rcx) : rtai_usrq_trampoline(regs->rax, regs->rcx);
	if (!in_hrt_mode(rtai_cpuid())) {
		local_irq_enable();
	}
}

static void rtai_uvec_handler (void)
{
__asm__ ( \
        "\n .p2align\n" \
	"pushq $0\n" \
	"cld\n" \
	"pushq %rdi\n" \
	"pushq %rsi\n" \
	"pushq %rdx\n" \
	"pushq %rcx\n" \
	"pushq %rax\n" \
	"pushq %r8\n" \
	"pushq %r9\n" \
	"pushq %r10\n" \
	"pushq %r11\n" \
	"pushq %rbx\n" \
	"pushq %rbp\n" \
	"pushq %r12\n" \
	"pushq %r13\n" \
	"pushq %r14\n" \
	"pushq %r15\n" \
	"movq %rsp, %rdi\n" \
	"movq %rsp, %rbp\n" \
	"testl $3, 136(%rdi)\n" \
	"je 1f\n" \
	"swapgs\n" \
	"1: movq %gs:56, %rax\n" \
	"cmoveq %rax, %rsp\n" \
	"pushq %rdi\n" \
	"sti\n" \
	"call rtai_syscall_entry\n" \
	"cli\n" \
	"popq %rdi\n" \
	"testl $3, 136(%rdi)\n" \
        "je 2f\n" \
	"swapgs\n" \
	"2:\n" \
	"movq %rdi, %rsp\n" \
	"popq %r15\n" \
	"popq %r14\n" \
	"popq %r13\n" \
	"popq %r12\n" \
	"popq %rbp\n" \
	"popq %rbx\n" \
	"popq %r11\n" \
	"popq %r10\n" \
	"popq %r9\n" \
	"popq %r8\n" \
	"popq %rax\n" \
	"popq %rcx\n" \
	"popq %rdx\n" \
	"popq %rsi\n" \
	"popq %rdi\n" \
	"addq $8, %rsp\n" \
        "iretq");
}

struct gate_struct rtai_set_gate_vector (unsigned vector, int type, int dpl, void *handler)
{
	struct gate_struct e = idt_table[vector];
	_set_gate(&idt_table[vector], type, (unsigned long)handler, dpl, 0);
	return e;
}

void rtai_reset_gate_vector (unsigned vector, struct gate_struct e)
{
	idt_table[vector] = e;
}

static void rtai_install_archdep (void)

{
    unsigned long flags;

    flags = rtai_critical_enter(NULL);

    /* Backup and replace the sysreq vector. */
    rtai_sysvec = rtai_set_gate_vector(RTAI_SYS_VECTOR, GATE_INTERRUPT, 3, rtai_uvec_handler);

    rtai_critical_exit(flags);

    if (rtai_cpufreq_arg == 0) {
	adsysinfo_t sysinfo;
	adeos_get_sysinfo(&sysinfo);
	rtai_cpufreq_arg = (unsigned long)sysinfo.cpufreq; /* FIXME: 4Ghz barrier is close... */
}

    rtai_tunables.cpu_freq = rtai_cpufreq_arg;

#ifdef CONFIG_X86_LOCAL_APIC
    if (rtai_apicfreq_arg == 0)
	rtai_apicfreq_arg = apic_read(APIC_TMICT) * HZ;

    rtai_tunables.apic_freq = rtai_apicfreq_arg;
#endif /* CONFIG_X86_LOCAL_APIC */
}

static void rtai_uninstall_archdep (void) {

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

    for (i = 0; i < 10000; i++)
	{ 
	outb(LATCH & 0xff,0x40);
	outb(LATCH >> 8,0x40);
	}

    dt = rtai_rdtsc() - t;

    rtai_critical_exit(flags);

    return rtai_imuldiv(dt,100000,RTAI_CPU_FREQ);
}

void (*rt_set_ihook (void (*hookfn)(int)))(int)
{
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	return (void (*)(int))xchg(&rtai_isr_hook, hookfn); /* This is atomic */
#else  /* !CONFIG_RTAI_SCHED_ISR_LOCK */
	return NULL;
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
}

static int errno;

static inline _syscall3(int,
			sched_setscheduler,
			pid_t,pid,
			int,policy,
			struct sched_param *,param)

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
	return;
    struct sched_param __user param;
    mm_segment_t old_fs;
    int rc;

    param.sched_priority = prio;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    rc = sched_setscheduler(task->pid,policy,&param);
    set_fs(old_fs);

    if (rc)
	printk("RTAI[hal]: sched_setscheduler(policy=%d,prio=%d) failed, code %d (%s -- pid=%d)\n",
	       policy,
	       prio,
	       rc,
	       task->comm,
	       task->pid);
}
#endif  /* KERNEL_VERSION < 2.6.0 */

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

    PROC_PRINT("\n** RTAI/x86_64:\n\n");
#ifdef CONFIG_X86_LOCAL_APIC
    PROC_PRINT("    APIC Frequency: %lu\n",rtai_tunables.apic_freq);
    PROC_PRINT("    APIC Latency: %d ns\n",RTAI_LATENCY_APIC);
    PROC_PRINT("    APIC Setup: %d ns\n",RTAI_SETUP_TIME_APIC);
#endif /* CONFIG_X86_LOCAL_APIC */
    
    none = 1;

    PROC_PRINT("\n** Real-time IRQs used by RTAI: ");

    for (i = 0; i < NR_IRQS; i++)
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

    PROC_PRINT("** RTAI extension traps: \n\n");
    PROC_PRINT("    SYSREQ=0x%x\n",RTAI_SYS_VECTOR);
    PROC_PRINT("      LXRT=0x%x\n",RTAI_LXRT_VECTOR);
    PROC_PRINT("       SHM=0x%x\n\n",RTAI_SHM_VECTOR);

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
	printk(KERN_ERR "Unable to initialize /proc/rtai.\n");
	return -1;
        }

    rtai_proc_root->owner = THIS_MODULE;

    ent = create_proc_entry("hal",S_IFREG|S_IRUGO|S_IWUSR,rtai_proc_root);

    if (!ent)
	{
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
		rt_printk(KERN_INFO "RTAI[hal]: %s mounted over Adeos %s.\n", PACKAGE_VERSION,ADEOS_VERSION_STRING);
		rt_printk(KERN_INFO "RTAI[hal]: compiled with %s.\n", CONFIG_RTAI_COMPILER);
	}
#ifndef CONFIG_ADEOS_NOTHREADS
	for (;;) adeos_suspend_domain();
#endif /* !CONFIG_ADEOS_NOTHREADS */
}

static void rt_printk_srq_handler(void);
#define RT_PRINTK_SRQ  1

int __rtai_hal_init (void)
{
	unsigned long flags;
	int trapnr;
	adattr_t attr;

#ifdef CONFIG_X86_LOCAL_APIC
	if (!test_bit(X86_FEATURE_APIC, boot_cpu_data.x86_capability)) {
		printk("RTAI[hal]: ERROR, LOCAL APIC CONFIGURED BUT NOT AVAILABLE/ENABLED\n");
		return -1;
	}
#endif

	 /* Allocate a virtual interrupt to handle sysreqs within the Linux
            domain. */
	rtai_sysreq_virq = adeos_alloc_irq();

	if (!rtai_sysreq_virq) {
		printk(KERN_ERR "RTAI[hal]: no virtual interrupt available.\n");
		return 1;
	}

	/* Reserve the first two _consecutive_ per-thread data key in the
           Linux domain. This is rather crappy, since we depend on
           statically defined PTD key values, which is exactly what the
           PTD scheme is here to prevent. Unfortunately, reserving these
           specific keys is the only way to remain source compatible with
           the current LXRT implementation. */
	flags = rtai_critical_enter(NULL);
	
	rtai_adeos_ptdbase = adeos_alloc_ptdkey();
	trapnr = adeos_alloc_ptdkey() != rtai_adeos_ptdbase + 1;
	
	adeos_default_irq_handler = adeos_set_irq_handler(rtai_irq_trampoline);
	adeos_virtualize_irq(rtai_sysreq_virq, &rtai_ssrq_trampoline, NULL, IPIPE_HANDLE_MASK);

	rtai_critical_exit(flags);

	if (trapnr) {
		printk(KERN_ERR "RTAI[hal]: per-thread keys not available.\n");
		return 1;
	}

	rtai_install_archdep();

#ifdef CONFIG_PROC_FS
	rtai_proc_register();
#endif

	rtai_sysreq_table[RT_PRINTK_SRQ].k_handler = rt_printk_srq_handler;
	set_bit(RT_PRINTK_SRQ, &rtai_sysreq_map);

/* Let Adeos do its magic for our immediate irq dispatching real-time domain. */
	adeos_init_attr(&attr);
	attr.name     = "RTAI";
	attr.domid    = RTAI_DOMAIN_ID;
	attr.entry    = rtai_domain_entry;

	attr.priority = ADEOS_ROOT_PRI + 100; /* Before Linux in the pipeline */
	adeos_register_domain(&rtai_domain, &attr);
	/* Trap all faults. */
	for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++)
		adeos_catch_event(trapnr, &rtai_trap_fault);
	
	printk(KERN_INFO "RTAI[hal]: mounted %d.\n", NR_IRQS);

	return 0;
}

void __rtai_hal_exit (void)
{
	int trapnr;
	unsigned long flags;
#ifdef CONFIG_PROC_FS
	rtai_proc_unregister();
#endif
	adeos_unregister_domain(&rtai_domain);
	flags = rtai_critical_enter(NULL);
	for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++) {
		adeos_catch_event(trapnr, NULL);
	}
	
	// restore original adeos handler
	adeos_set_irq_handler(adeos_default_irq_handler);
	rtai_critical_exit(flags);
	
	clear_bit(RT_PRINTK_SRQ, &rtai_sysreq_map);
	adeos_virtualize_irq(rtai_sysreq_virq, NULL, NULL, 0);
	adeos_free_irq(rtai_sysreq_virq);
	rtai_uninstall_archdep();
	adeos_free_ptdkey(rtai_adeos_ptdbase); /* #0 and #1 actually */
	adeos_free_ptdkey(rtai_adeos_ptdbase + 1);
	current->state = TASK_INTERRUPTIBLE;
	schedule_timeout(HZ/20);
	printk(KERN_INFO "RTAI[hal]: unmounted.\n");
}

module_init(__rtai_hal_init);
module_exit(__rtai_hal_exit);

/*
 *  rt_printk.c, hacked from linux/kernel/printk.c.
 *
 * Modified for RT support, David Schleef.
 *
 * Adapted to RTAI, and restyled his own way by Paolo Mantegazza.
 *
 */

#define PRINTK_BUF_SIZE  (10000) // Test programs may generate much output. PC
#define TEMP_BUF_SIZE	 (500)

static char rt_printk_buf[PRINTK_BUF_SIZE];

static int buf_front, buf_back;
static char buf[TEMP_BUF_SIZE];

int rt_printk (const char *fmt, ...)
{
	unsigned long flags;
        static spinlock_t display_lock = SPIN_LOCK_UNLOCKED;
	va_list args;
	int len, i;

        flags = rt_spin_lock_irqsave(&display_lock);
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);
	if ((buf_front + len) >= PRINTK_BUF_SIZE) {
		i = PRINTK_BUF_SIZE - buf_front;
		memcpy(rt_printk_buf + buf_front, buf, i);
		memcpy(rt_printk_buf, buf + i, len - i);
		buf_front = len - i;
	} else {
		memcpy(rt_printk_buf + buf_front, buf, len);
		buf_front += len;
	}
        rt_spin_unlock_irqrestore(flags, &display_lock);
	rt_pend_linux_srq(RT_PRINTK_SRQ);

	return len;
}

static void rt_printk_srq_handler (void)
{
	int tmp;

	while(1) {
		tmp = buf_front;
		if (buf_back > tmp) {
			printk("%.*s", PRINTK_BUF_SIZE - buf_back, rt_printk_buf + buf_back);
			buf_back = 0;
		}
		if (buf_back == tmp) {
			break;
		}
		printk("%.*s", tmp - buf_back, rt_printk_buf + buf_back);
		buf_back = tmp;
	}
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

EXPORT_SYMBOL(rtai_set_gate_vector);
EXPORT_SYMBOL(rtai_reset_gate_vector);

EXPORT_SYMBOL(rtai_lxrt_dispatcher);
EXPORT_SYMBOL(rt_scheduling);
EXPORT_SYMBOL(adeos_pended);
/*@}*/
