/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for x86.
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
v * @defgroup hal Hardware abstraction layer.
 *
 * Basic architecture-dependent services used by the real-time
 * nucleus.
 *
 *@{*/

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
#include <nucleus/asm/hal.h>
#ifdef CONFIG_PROC_FS
#include <nucleus/procfs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

MODULE_LICENSE("GPL");

static unsigned long rthal_cpufreq_arg;
MODULE_PARM(rthal_cpufreq_arg,"i");

static unsigned long rthal_timerfreq_arg;
MODULE_PARM(rthal_timerfreq_arg,"i");

extern struct desc_struct idt_table[];

adomain_t rthal_domain;

static struct {

    void (*handler)(unsigned irq, void *cookie);
    void *cookie;
    unsigned long hits[RTHAL_NR_CPUS];

} rthal_realtime_irq[IPIPE_NR_XIRQS];

static struct {

    unsigned long flags;
    int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

static struct {

    void (*handler)(void);
    unsigned label;

} rthal_sysreq_table[RTHAL_NR_SRQS];

static int rthal_init_done;

static unsigned rthal_sysreq_virq;

static unsigned long rthal_sysreq_map = 1; /* #0 is invalid. */

static unsigned long rthal_sysreq_pending;

static unsigned long rthal_sysreq_running;

static spinlock_t rthal_ssrq_lock = SPIN_LOCK_UNLOCKED;

static volatile int rthal_sync_op;

static atomic_t rthal_sync_count = ATOMIC_INIT(1);

static rthal_trap_handler_t rthal_trap_handler;

struct rthal_switch_info rthal_linux_context[RTHAL_NR_CPUS];

struct rthal_calibration_data rthal_tunables;

volatile unsigned long rthal_cpu_realtime;

#ifdef CONFIG_X86_LOCAL_APIC

static long long rthal_timers_sync_time;

static struct rthal_apic_data rthal_timer_mode[RTHAL_NR_CPUS];

struct rthal_apic_data {

    int mode;
    unsigned long count;
};

static inline void rthal_setup_periodic_apic (unsigned count,
					      unsigned vector)
{
    apic_read(APIC_LVTT);
    apic_write_around(APIC_LVTT,APIC_LVT_TIMER_PERIODIC|vector);
    apic_read(APIC_TMICT);
    apic_write_around(APIC_TMICT,count);
}

static inline void rthal_setup_oneshot_apic (unsigned count,
					     unsigned vector)
{
    apic_read(APIC_LVTT);
    apic_write_around(APIC_LVTT,vector);
    apic_read(APIC_TMICT);
    apic_write_around(APIC_TMICT,count);
}

static void rthal_critical_sync (void)

{
    struct rthal_apic_data *p;

    switch (rthal_sync_op)
	{
	case 1:

	    p = &rthal_timer_mode[adeos_processor_id()];
	    
	    while (rthal_rdtsc() < rthal_timers_sync_time)
		;

	    if (p->mode)
		rthal_setup_periodic_apic(p->count,RTHAL_APIC_TIMER_VECTOR);
	    else
		rthal_setup_oneshot_apic(p->count,RTHAL_APIC_TIMER_VECTOR);

	    break;

	case 2:

	    rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT,LOCAL_TIMER_VECTOR);
	    break;
	}
}

irqreturn_t rthal_broadcast_to_local_timers (int irq,
					     void *dev_id,
					     struct pt_regs *regs)
{
    unsigned long flags;

    rthal_hw_lock(flags);
    apic_wait_icr_idle();
    apic_write_around(APIC_ICR,APIC_DM_FIXED|APIC_DEST_ALLINC|LOCAL_TIMER_VECTOR);
    rthal_hw_unlock(flags);

    return IRQ_HANDLED;
}

unsigned long rthal_calibrate_timer (void)

{
    unsigned long flags;
    rthal_time_t t, dt;
    int i;

    flags = rthal_critical_enter(NULL);

    t = rthal_rdtsc();

    for (i = 0; i < 10000; i++)
	{ 
	unsigned int v;
	apic_read(APIC_LVTT);
	apic_write_around(APIC_LVTT,RTHAL_APIC_TIMER_VECTOR);
	v = apic_read(APIC_TMICT);
	apic_write_around(APIC_TMICT,v);
	}

    dt = rthal_rdtsc() - t;

    rthal_critical_exit(flags);

    return rthal_imuldiv(dt,100000,RTHAL_CPU_FREQ);
}

int rthal_request_timer (void (*handler)(void),
			 unsigned long nstick)
{
    struct rthal_apic_data *p;
    unsigned long flags;
    int cpuid;

    /* This code works both for UP+LAPIC and SMP configurations. */

    /* Try releasing the LAPIC-bound IRQ now so that any attempt to
       run a LAPIC-enabled RTAI over a plain 8254-only/UP kernel will
       beget an error immediately. */

    if (rthal_release_irq(RTHAL_APIC_TIMER_IPI) < 0)
	return -EINVAL;

    flags = rthal_critical_enter(rthal_critical_sync);

    rthal_sync_op = 1;

    rthal_timers_sync_time = rthal_rdtsc() + rthal_imuldiv(LATCH,
							   RTHAL_CPU_FREQ,
							   CLOCK_TICK_RATE);

    /* We keep the setup data array just to be able to expose it to
       the visible interface if it happens to be really needed at some
       point in time. */
    
    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	{
	p = &rthal_timer_mode[cpuid];
	p->mode = !!nstick;	/* 0=oneshot, 1=periodic */
	p->count = nstick;

	if (p->mode)
	    p->count = rthal_imuldiv(p->count,RTHAL_TIMER_FREQ,1000000000);
	else
	    p->count = RTHAL_APIC_ICOUNT;
	}

    p = &rthal_timer_mode[adeos_processor_id()];

    while (rthal_rdtsc() < rthal_timers_sync_time)
	;

    if (p->mode)
	rthal_setup_periodic_apic(p->count,RTHAL_APIC_TIMER_VECTOR);
    else
	rthal_setup_oneshot_apic(p->count,RTHAL_APIC_TIMER_VECTOR);

    rthal_request_irq(RTHAL_APIC_TIMER_IPI,
		      (rthal_irq_handler_t)handler,
		      NULL);

    rthal_request_linux_irq(RTHAL_8254_IRQ,
			    &rthal_broadcast_to_local_timers,
			    "rthal_broadcast_timer",
			    &rthal_broadcast_to_local_timers);

    rthal_critical_exit(flags);

    return 0;
}

void rthal_release_timer (void)

{
    unsigned long flags;

    rthal_release_linux_irq(RTHAL_8254_IRQ,
			    &rthal_broadcast_to_local_timers);

    flags = rthal_critical_enter(&rthal_critical_sync);

    rthal_sync_op = 2;
    rthal_setup_periodic_apic(RTHAL_APIC_ICOUNT,LOCAL_TIMER_VECTOR);
    rthal_release_irq(RTHAL_APIC_TIMER_IPI);

    rthal_critical_exit(flags);
}

#else /* !CONFIG_X86_LOCAL_APIC */

unsigned long rthal_calibrate_timer (void)

{
    unsigned long flags;
    rthal_time_t t, dt;
    int i;

    flags = rthal_critical_enter(NULL);

    outb(0x34,0x43);

    t = rthal_rdtsc();

    for (i = 0; i < 10000; i++)
	{ 
	outb(LATCH & 0xff,0x40);
	outb(LATCH >> 8,0x40);
	}

    dt = rthal_rdtsc() - t;

    rthal_critical_exit(flags);

    return rthal_imuldiv(dt,100000,RTHAL_CPU_FREQ);
}

int rthal_request_timer (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;
    int err;

    flags = rthal_critical_enter(NULL);

    if (nstick > 0)
	{
	/* Periodic setup for 8254 channel #0. */
	unsigned period;
	period = (unsigned)rthal_llimd(nstick,RTHAL_TIMER_FREQ,1000000000);
	if (period > LATCH) period = LATCH;
	outb(0x34,0x43);
	outb(period & 0xff,0x40);
	outb(period >> 8,0x40);
	}
    else
	{
	/* Oneshot setup for 8254 channel #0. */
	outb(0x30,0x43);
	outb(LATCH & 0xff,0x40);
	outb(LATCH >> 8,0x40);
	}

    rthal_release_irq(RTHAL_8254_IRQ);

    err = rthal_request_irq(RTHAL_8254_IRQ,
			    (rthal_irq_handler_t)handler,
			    NULL);

    rthal_critical_exit(flags);

    return err;
}

void rthal_release_timer (void)

{
    unsigned long flags;

    flags = rthal_critical_enter(NULL);
    outb(0x34,0x43);
    outb(LATCH & 0xff,0x40);
    outb(LATCH >> 8,0x40);
    rthal_release_irq(RTHAL_8254_IRQ);

    rthal_critical_exit(flags);
}

#endif /* CONFIG_X86_LOCAL_APIC */

unsigned long rthal_critical_enter (void (*synch)(void))

{
    unsigned long flags = adeos_critical_enter(synch);

    if (atomic_dec_and_test(&rthal_sync_count))
	rthal_sync_op = 0;
    else if (synch != NULL)
	printk(KERN_WARNING "RTAI[hal]: Nested sync will fail.\n");

    return flags;
}

void rthal_critical_exit (unsigned long flags)

{
    atomic_inc(&rthal_sync_count);
    adeos_critical_exit(flags);
}

int rthal_request_irq (unsigned irq,
		       void (*handler)(unsigned irq, void *cookie),
		       void *cookie)
{
    unsigned long flags;
    int err = 0;

    if (handler == NULL || irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    flags = rthal_critical_enter(NULL);

    if (rthal_realtime_irq[irq].handler != NULL)
	{
	err = -EBUSY;
	goto unlock_and_exit;
	}

    rthal_realtime_irq[irq].handler = handler;
    rthal_realtime_irq[irq].cookie = cookie;

 unlock_and_exit:

    rthal_critical_exit(flags);

    return err;
}

int rthal_release_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    xchg(&rthal_realtime_irq[irq].handler,NULL);

    return 0;
}

/**
 * Enable an IRQ source.
 *
 */
int rthal_enable_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS || adeos_virtual_irq_p(irq))
	return -EINVAL;

    irq_desc[irq].handler->enable(irq);

    return 0;
}

/**
 * Disable an IRQ source.
 *
 */
int rthal_disable_irq (unsigned irq) {

    if (irq >= IPIPE_NR_XIRQS || adeos_virtual_irq_p(irq))
	return -EINVAL;

    irq_desc[irq].handler->disable(irq);

    return 0;
}

/**
 * Install a shared Linux interrupt handler.
 *
 * rthal_request_linux_irq installs function @a handler as a standard
 * Linux interrupt service routine for IRQ level @a irq forcing Linux
 * to share the IRQ with other interrupt handlers. The handler is
 * appended to any already existing Linux handler for the same irq and
 * is run by Linux irq as any of its handler. In this way a real time
 * application can monitor Linux interrupts handling at its will. The
 * handler appears in /proc/interrupts.
 *
 * @param irq is the IRQ level to which the handler will be associated.
 *
 * @param handler pointer on the interrupt service routine to be installed.
 *
 * @param name is a name for /proc/interrupts.
 *
 * @param dev_id is to pass to the interrupt handler, in the same way as the
 * standard Linux irq request call.
 *
 * The interrupt service routine can be uninstalled using
 * rthal_release_linux_irq().
 *
 * @retval 0 on success.
 * @retval -EINVAL if @a irq is not a valid external IRQ number or handler
 * is @c NULL.
 */
int rthal_request_linux_irq (unsigned irq,
			     irqreturn_t (*handler)(int irq,
						    void *dev_id,
						    struct pt_regs *regs), 
			     char *name,
			     void *dev_id)
{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS || adeos_virtual_irq_p(irq) || !handler)
	return -EINVAL;

    rthal_local_irq_save(flags);

    rthal_spin_lock(&irq_desc[irq].lock);

    if (rthal_linux_irq[irq].count++ == 0 && irq_desc[irq].action)
	{
	rthal_linux_irq[irq].flags = irq_desc[irq].action->flags;
	irq_desc[irq].action->flags |= SA_SHIRQ;
	}

    rthal_spin_unlock(&irq_desc[irq].lock);

    rthal_local_irq_restore(flags);

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
 * @retval -EINVAL if @a irq is not a valid external IRQ number.
 */
int rthal_release_linux_irq (unsigned irq, void *dev_id)

{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS ||
	adeos_virtual_irq_p(irq) ||
	rthal_linux_irq[irq].count == 0)
	return -EINVAL;

    rthal_local_irq_save(flags);

    free_irq(irq,dev_id);

    rthal_spin_lock(&irq_desc[irq].lock);

    if (--rthal_linux_irq[irq].count == 0 && irq_desc[irq].action)
	irq_desc[irq].action->flags = rthal_linux_irq[irq].flags;

    rthal_spin_unlock(&irq_desc[irq].lock);

    rthal_local_irq_restore(flags);

    return 0;
}

/**
 * Pend an IRQ to Linux.
 *
 * rthal_pend_linux_irq appends a Linux interrupt irq for processing in Linux IRQ
 * mode, i.e. with hardware interrupts fully enabled.
 */
int rthal_pend_linux_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    return adeos_propagate_irq(irq);
}

/**
 * Install a system request handler
 *
 * rthal_request_srq installs a RTAI system request (srq) by assigning
 * @a handler, the function to be called in kernel space following its
 * activation by a call to rthal_pend_linux_srq(). @a handler is in
 * practice used to request a service from the kernel. In fact Linux
 * system requests cannot be used safely from RTAI so you can setup a
 * handler that receives real time requests and safely executes them
 * when Linux is running.
 *
 * @return the number of the assigned system request on success.
 * @retval -EINVAL if @a handler is @c NULL.
 * @retval -EBUSY if no free srq slot is available.
 */
int rthal_request_srq (unsigned label,
		       void (*handler)(void))
{
    unsigned long flags;
    int srq;

    if (handler == NULL)
	return -EINVAL;

    flags = rthal_spin_lock_irqsave(&rthal_ssrq_lock);

    if (rthal_sysreq_map != ~0)
	{
	srq = ffz(rthal_sysreq_map);
	set_bit(srq,&rthal_sysreq_map);
	rthal_sysreq_table[srq].handler = handler;
	rthal_sysreq_table[srq].label = label;
	}
    else
	srq = -EBUSY;

    rthal_spin_unlock_irqrestore(flags,&rthal_ssrq_lock);

    return srq;
}

/**
 * Uninstall a system request handler
 *
 * rthal_release_srq uninstalls the specified system call @a srq, returned by
 * installing the related handler with a previous call to rthal_request_srq().
 *
 * @retval EINVAL if @a srq is invalid.
 */
int rthal_release_srq (unsigned srq)

{
    if (srq < 1 ||
	srq >= RTHAL_NR_SRQS ||
	!test_and_clear_bit(srq,&rthal_sysreq_map))
	return -EINVAL;

    return 0;
}

/**
 * Append a Linux IRQ.
 *
 * rthal_pend_linux_srq appends a system call request srq to be used as a service
 * request to the Linux kernel.
 *
 * @param srq is the value returned by rthal_request_srq.
 */
int rthal_pend_linux_srq (unsigned srq)

{
    if (srq > 0 && srq < RTHAL_NR_SRQS)
	{
	if (!test_and_set_bit(srq,&rthal_sysreq_pending))
	    {
	    adeos_schedule_irq(rthal_sysreq_virq);
	    return 1;
	    }

	return 0;	/* Already pending. */
	}

    return -EINVAL;
}

#ifdef CONFIG_SMP

static unsigned long rthal_old_irq_affinity[IPIPE_NR_XIRQS],
                     rthal_current_irq_affinity[IPIPE_NR_XIRQS];

static spinlock_t rthal_iset_lock = SPIN_LOCK_UNLOCKED;

/**
 * Set IRQ->CPU assignment
 *
 * rthal_set_irq_affinity forces the assignment of the external
 * interrupt @a irq to the CPUs listed in @a cpumask.
 *
 * @retval 0 on success.
 * @retval EINVAL if @a irq is not a valid external IRQ number.
 *
 * @note This function has effect only on multiprocessors systems.
 */
int rthal_set_irq_affinity (unsigned irq, unsigned long cpumask)

{
    unsigned long oldmask, flags;

    if (irq >= IPIPE_NR_XIRQS || adeos_virtual_irq_p(irq))
	return -EINVAL;

    rthal_local_irq_save(flags);

    rthal_spin_lock(&rthal_iset_lock);

    oldmask = adeos_set_irq_affinity(irq,cpumask);

    if (oldmask == 0)
	{
	/* Oops... Something went wrong. */
	rthal_spin_unlock(&rthal_iset_lock);
	rthal_local_irq_restore(flags);
	return -EINVAL;
	}

    rthal_old_irq_affinity[irq] = oldmask;
    rthal_current_irq_affinity[irq] = cpumask;

    rthal_spin_unlock(&rthal_iset_lock);

    rthal_local_irq_restore(flags);

    return 0;
}

/**
 * reset IRQ->CPU assignment
 *
 * rthal_reset_irq_affinity resets the interrupt irq to the previous
 * Linux setting.
 *
 * @retval 0 on success.
 * @retval -EINVAL if @a irq is not a valid external IRQ number.
 *
 * @note This function has effect only on multiprocessors systems.
 */
int rthal_reset_irq_affinity (unsigned irq)

{
    unsigned long oldmask, flags;

    if (irq >= IPIPE_NR_XIRQS || adeos_virtual_irq_p(irq))
	return -EINVAL;

    rthal_local_irq_save(flags);

    rthal_spin_lock(&rthal_iset_lock);

    if (rthal_old_irq_affinity[irq] == 0)
	{
	rthal_spin_unlock(&rthal_iset_lock);
	rthal_local_irq_restore(flags);
	return -EINVAL;
	}

    oldmask = adeos_set_irq_affinity(irq,0); /* Query -- no change. */

    if (oldmask == rthal_current_irq_affinity[irq])
	{
	/* Ok, proceed since nobody changed it in the meantime. */
	adeos_set_irq_affinity(irq,rthal_old_irq_affinity[irq]);
	rthal_old_irq_affinity[irq] = 0;
	}

    rthal_spin_unlock(&rthal_iset_lock);

    rthal_local_irq_restore(flags);

    return 0;
}

#else /* !CONFIG_SMP */

int rthal_set_irq_affinity (unsigned irq, unsigned long cpumask) {

    return 0;
}

int rthal_reset_irq_affinity (unsigned irq) {

    return 0;
}

#endif /* CONFIG_SMP */

#ifndef CONFIG_X86_TSC

static rthal_time_t rthal_ts_8254;

static int rthal_last_8254_counter2;

/* TSC emulation using PIT channel #2. */

rthal_time_t rthal_get_8254_tsc (void)

{
    unsigned long flags;
    rthal_time_t t;
    int inc, c2;

    rthal_hw_lock(flags); /* Local hw masking is required here. */

    outb(0xd8,0x43);
    c2 = inb(0x42);
    inc = rthal_last_8254_counter2 - (c2 |= (inb(0x42) << 8));
    rthal_last_8254_counter2 = c2;
    t = (rthal_ts_8254 += (inc > 0 ? inc : inc + RTHAL_8254_COUNT2LATCH));

    rthal_hw_unlock(flags);

    return t;
}

#endif /* !CONFIG_X86_TSC */

rthal_trap_handler_t rthal_set_trap_handler (rthal_trap_handler_t handler) {

    return (rthal_trap_handler_t)xchg(&rthal_trap_handler,handler);
}

static void rthal_irq_trampoline (unsigned irq)

{
    if (rthal_realtime_irq[irq].handler)
	{
	rthal_realtime_irq[irq].hits[adeos_processor_id()]++;
	rthal_realtime_irq[irq].handler(irq,rthal_realtime_irq[irq].cookie);
	}
    else
	adeos_propagate_irq(irq);
}

static void rthal_trap_fault (adevinfo_t *evinfo)

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
    for fusion switching back a Linux thread in non-RT mode which
    happens to have been preempted inside do_exit() after the MM
    context has been dropped (thus the LDT too). In such a case, %gs
    could be reloaded with what used to be the TLS descriptor of the
    exiting thread, but unfortunately after the LDT itself has been
    dropped. Since the default LDT is only 5 entries long, any attempt
    to refer to an LDT-indexed descriptor above this value would cause
    a GPF.  2) NMI is not pipelined by Adeos. */

    adeos_load_cpuid();

    if (evinfo->domid == RTHAL_DOMAIN_ID)
	{
	if (evinfo->event == 14)	/* Page fault. */
	    {
	    struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
	    unsigned long address;

	    /* As of 2.6, we must handle RTAI-originated faults in
	       kernel space caused by on-demand virtual memory
	       mappings. We can specifically process this case through
	       the Linux fault handler since we know that it is
	       context-agnostic and does not wreck the
	       determinism. Any other case would lead us to
	       panicking. */

	    __asm__("movl %%cr2,%0":"=r" (address));

	    if (address >= TASK_SIZE && !(regs->orig_eax & 5)) /* i.e. trap error code. */
		{
		asmlinkage void do_page_fault(struct pt_regs *regs, unsigned long error_code);
		do_page_fault(regs,regs->orig_eax);
		goto endtrap;
		}
	    }

	if (rthal_trap_handler != NULL &&
	    test_bit(cpuid,&rthal_cpu_realtime) &&
	    rthal_trap_handler(evinfo->event,
			       trap2sig[evinfo->event],
			       (struct pt_regs *)evinfo->evdata,
			       NULL) != 0)
	    goto endtrap;
	}

    adeos_propagate_event(evinfo);

endtrap:

    return;
}

static void rthal_ssrq_trampoline (unsigned virq)

{
    unsigned long pending;

    rthal_spin_lock(&rthal_ssrq_lock);

    while ((pending = rthal_sysreq_pending & ~rthal_sysreq_running) != 0)
	{
	unsigned srq = ffnz(pending);
	set_bit(srq,&rthal_sysreq_running);
	clear_bit(srq,&rthal_sysreq_pending);
	rthal_spin_unlock(&rthal_ssrq_lock);

	if (test_bit(srq,&rthal_sysreq_map))
	    rthal_sysreq_table[srq].handler();

	clear_bit(srq,&rthal_sysreq_running);
	rthal_spin_lock(&rthal_ssrq_lock);
	}

    rthal_spin_unlock(&rthal_ssrq_lock);
}

static int errno;

static inline _syscall3(int,
			sched_setscheduler,
			pid_t,pid,
			int,policy,
			struct sched_param *,param)

void rthal_set_linux_task_priority (struct task_struct *task, int policy, int prio)

{
    struct sched_param __user param;
    mm_segment_t old_fs;
    int rc;

    param.sched_priority = prio;
    old_fs = get_fs();
    set_fs(KERNEL_DS);
    rc = sched_setscheduler(task->pid,policy,&param);
    set_fs(old_fs);

    if (rc)
	printk(KERN_ERR "RTAI[hal]: setscheduler(policy=%d,prio=%d)=%d (%s -- pid=%d)\n",
	       policy,
	       prio,
	       rc,
	       task->comm,
	       task->pid);
}

static void rthal_domain_entry (int iflag)

{
    unsigned irq, trapnr;

    if (!iflag)
	goto spin;

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	adeos_virtualize_irq(irq,
			     &rthal_irq_trampoline,
			     NULL,
			     IPIPE_DYNAMIC_MASK);

    /* Trap all faults. */
    for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++)
	adeos_catch_event(trapnr,&rthal_trap_fault);

    printk(KERN_INFO "RTAI[hal]: %s loaded.\n",PACKAGE_VERSION);

 spin:

    for (;;)
	adeos_suspend_domain();
}

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rthal_proc_root = NULL;

static int rthal_read_proc (char *page,
			    char **start,
			    off_t off,
			    int count,
			    int *eof,
			    void *data)
{
    int len = 0, cpuid, irq, nirqs;
    char *p = page;

    p += sprintf(p,"RTAI/hal over Adeos %s\n",ADEOS_VERSION_STRING);
    p += sprintf(p,"Compiled with: %s\n",CONFIG_RTAI_COMPILER);

    for (irq = nirqs = 0; irq < IPIPE_NR_XIRQS; irq++)
	if (rthal_realtime_irq[irq].handler != NULL)
	    nirqs++;

    if (nirqs == 0)
	{
	p += sprintf(p,"No real-time interrupt registered");
	goto out;
	}

    p += sprintf(p,"\nRT-IRQ");

    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	p += sprintf(p,"        CPU%d",cpuid);

    for (irq = 0; irq < IPIPE_NR_XIRQS; irq++)
	{
	if (rthal_realtime_irq[irq].handler == NULL)
	    continue;

	p += sprintf(p,"\n%3d:",irq);

	for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	    p += sprintf(p,"  %12lu",rthal_realtime_irq[irq].hits[cpuid]);
	}

 out:

    p += sprintf(p,"\n");

    len = p - page;

    if (len <= off + count)
	*eof = 1;

    *start = page + off;

    len -= off;

    if (len > count)
	len = count;

    if (len < 0)
	len = 0;

    return len;
}

static int rthal_proc_register (void)

{
    struct proc_dir_entry *ent;

    rthal_proc_root = create_proc_entry("rtai",S_IFDIR, 0);

    if (!rthal_proc_root)
	{
	printk(KERN_ERR "RTAI[hal]: Unable to initialize /proc/rtai.\n");
	return -1;
        }

    rthal_proc_root->owner = THIS_MODULE;

    ent = create_proc_entry("rthal",S_IFREG|S_IRUGO|S_IWUSR,rthal_proc_root);

    if (!ent)
	{
	printk(KERN_ERR "RTAI[hal]: Unable to initialize /proc/rtai/rthal.\n");
	return -1;
        }

    ent->read_proc = rthal_read_proc;

    return 0;
}

static void rthal_proc_unregister (void)

{
    remove_proc_entry("rthal",rthal_proc_root);
    remove_proc_entry("rtai",0);
}

#endif /* CONFIG_PROC_FS */

int __rthal_init (void)

{
    adattr_t attr;
    int err;

    if (num_online_cpus() > RTHAL_NR_CPUS)
	{
	printk(KERN_ERR "RTAI[hal]: Too many processors found -- need RTHAL_NR_CPUS >= %d.\n",
	       num_online_cpus());
	return 1;
	}

    /* Allocate a virtual interrupt to handle sysreqs within the Linux
       domain. */
    rthal_sysreq_virq = adeos_alloc_irq();

    if (!rthal_sysreq_virq)
	{
	printk(KERN_ERR "RTAI[hal]: No virtual interrupt available.\n");
	return 1;
	}

    adeos_virtualize_irq(rthal_sysreq_virq,
			 &rthal_ssrq_trampoline,
			 NULL,
			 IPIPE_HANDLE_MASK);

    if (rthal_cpufreq_arg == 0)
	{
	adsysinfo_t sysinfo;
	adeos_get_sysinfo(&sysinfo);
	/* FIXME: 4Ghz barrier is close... */
	rthal_cpufreq_arg = (unsigned long)sysinfo.cpufreq;
	}

    rthal_tunables.cpu_freq = rthal_cpufreq_arg;

    if (rthal_timerfreq_arg == 0)
#ifdef CONFIG_X86_LOCAL_APIC
	rthal_timerfreq_arg = apic_read(APIC_TMICT) * HZ;
#else /* !CONFIG_X86_LOCAL_APIC */
	rthal_timerfreq_arg = CLOCK_TICK_RATE;
#endif /* CONFIG_X86_LOCAL_APIC */

    rthal_tunables.timer_freq = rthal_timerfreq_arg;

#ifdef CONFIG_PROC_FS
    rthal_proc_register();
#endif /* CONFIG_PROC_FS */

    /* Let Adeos do its magic for our real-time domain. */
    adeos_init_attr(&attr);
    attr.name = "RTAI";
    attr.domid = RTHAL_DOMAIN_ID;
    attr.entry = &rthal_domain_entry;
    attr.priority = ADEOS_ROOT_PRI + 100; /* Precede Linux in the pipeline */

    err = adeos_register_domain(&rthal_domain,&attr);

    if (!err)
	rthal_init_done = 1;

    return err;
}

void __rthal_exit (void)

{
#ifdef CONFIG_PROC_FS
    rthal_proc_unregister();
#endif

    if (rthal_sysreq_virq)
	{
	adeos_virtualize_irq(rthal_sysreq_virq,NULL,NULL,0);
	adeos_free_irq(rthal_sysreq_virq);
	}

    if (rthal_init_done)
	adeos_unregister_domain(&rthal_domain);

    printk(KERN_INFO "RTAI[hal]: Unloaded.\n");
}

/*@}*/

module_init(__rthal_init);
module_exit(__rthal_exit);

EXPORT_SYMBOL(rthal_request_irq);
EXPORT_SYMBOL(rthal_release_irq);
EXPORT_SYMBOL(rthal_enable_irq);
EXPORT_SYMBOL(rthal_disable_irq);
EXPORT_SYMBOL(rthal_request_linux_irq);
EXPORT_SYMBOL(rthal_release_linux_irq);
EXPORT_SYMBOL(rthal_pend_linux_irq);
EXPORT_SYMBOL(rthal_request_srq);
EXPORT_SYMBOL(rthal_release_srq);
EXPORT_SYMBOL(rthal_pend_linux_srq);
EXPORT_SYMBOL(rthal_set_irq_affinity);
EXPORT_SYMBOL(rthal_reset_irq_affinity);
EXPORT_SYMBOL(rthal_request_timer);
EXPORT_SYMBOL(rthal_release_timer);
EXPORT_SYMBOL(rthal_set_trap_handler);
EXPORT_SYMBOL(rthal_calibrate_timer);

#ifndef CONFIG_X86_TSC
EXPORT_SYMBOL(rthal_get_8254_tsc);
#endif /* !CONFIG_X86_TSC */

EXPORT_SYMBOL(rthal_critical_enter);
EXPORT_SYMBOL(rthal_critical_exit);
EXPORT_SYMBOL(rthal_set_linux_task_priority);

EXPORT_SYMBOL(rthal_linux_context);
EXPORT_SYMBOL(rthal_domain);
EXPORT_SYMBOL(rthal_proc_root);
EXPORT_SYMBOL(rthal_tunables);
EXPORT_SYMBOL(rthal_cpu_realtime);
