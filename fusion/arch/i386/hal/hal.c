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
 *   Copyright &copy; 2002 Philippe Gerum.
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
 * @defgroup hal HAL/x86.
 *
 * Basic x86-dependent services used by the real-time nucleus.
 *
 *@{*/

#include <linux/version.h>
#include <linux/slab.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/console.h>
#include <linux/kallsyms.h>
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
#include <nucleus/asm/hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>
#include <smi.h>

MODULE_LICENSE("GPL");

static unsigned long rthal_cpufreq_arg;
module_param_named(cpufreq,rthal_cpufreq_arg,ulong,0444);

static unsigned long rthal_timerfreq_arg;
module_param_named(timerfreq,rthal_timerfreq_arg,ulong,0444);

extern struct desc_struct idt_table[];

adomain_t rthal_domain;

static struct {

    void (*handler)(unsigned irq, void *cookie);
    void *cookie;
    unsigned long hits[RTHAL_NR_CPUS];

} rthal_realtime_irq[IPIPE_NR_IRQS];

static struct {

    unsigned long flags;
    int count;

} rthal_linux_irq[IPIPE_NR_XIRQS];

static struct {

    void (*handler)(void);
    unsigned label;

} rthal_sysreq_table[RTHAL_NR_SRQS];

static int rthal_realtime_faults[RTHAL_NR_CPUS][ADEOS_NR_FAULTS];

static int rthal_init_done;

static unsigned rthal_sysreq_virq;

static unsigned long rthal_sysreq_map = 1; /* #0 is invalid. */

static unsigned long rthal_sysreq_pending;

static unsigned long rthal_sysreq_running;

static spinlock_t rthal_ssrq_lock = SPIN_LOCK_UNLOCKED;

static volatile int rthal_sync_op;

static atomic_t rthal_sync_count = ATOMIC_INIT(1);

static rthal_trap_handler_t rthal_trap_handler;

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
    long long sync_time;
    adeos_declare_cpuid;

    switch (rthal_sync_op)
	{
	case 1:
            adeos_load_cpuid();

	    p = &rthal_timer_mode[cpuid];

            sync_time = rthal_timers_sync_time;

            /* Stagger local timers on SMP systems, to avoid tick handler
               stupidly spinning while running on other CPU. */
            if(p->mode)
                sync_time += rthal_imuldiv(p->count, cpuid, num_online_cpus());

            while (rthal_rdtsc() < sync_time)
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
	apic_read(APIC_LVTT);
        apic_write_around(APIC_LVTT,APIC_LVT_TIMER_PERIODIC|LOCAL_TIMER_VECTOR);
        apic_read(APIC_TMICT);
        apic_write_around(APIC_TMICT,RTHAL_APIC_ICOUNT);
	}

    dt = rthal_rdtsc() - t;

    rthal_critical_exit(flags);

    return rthal_imuldiv(dt,100000,RTHAL_CPU_FREQ);
}

int rthal_request_timer (void (*handler)(void),
			 unsigned long nstick)
{
    struct rthal_apic_data *p;
    long long sync_time;
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

    adeos_load_cpuid();

    p = &rthal_timer_mode[cpuid];

    sync_time = rthal_timers_sync_time;

    if(p->mode)
        sync_time += rthal_imuldiv(p->count, cpuid, num_online_cpus());
    
    while (rthal_rdtsc() < sync_time)
	;

    if (p->mode)
	rthal_setup_periodic_apic(p->count,RTHAL_APIC_TIMER_VECTOR);
    else
	rthal_setup_oneshot_apic(p->count,RTHAL_APIC_TIMER_VECTOR);

    rthal_request_irq(RTHAL_APIC_TIMER_IPI,
		      (rthal_irq_handler_t)handler,
		      NULL);

    rthal_critical_exit(flags);

    rthal_request_linux_irq(RTHAL_8254_IRQ,
			    &rthal_broadcast_to_local_timers,
			    "rthal_broadcast_timer",
			    &rthal_broadcast_to_local_timers);

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

    outb(0x34,PIT_MODE);

    t = rthal_rdtsc();

    for (i = 0; i < 10000; i++)
	{ 
	outb(LATCH & 0xff,PIT_CH0);
	outb(LATCH >> 8,PIT_CH0);
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
	outb(0x34,PIT_MODE);
	outb(period & 0xff,PIT_CH0);
	outb(period >> 8,PIT_CH0);
	}
    else
	{
	/* Oneshot setup for 8254 channel #0. */
	outb(0x30,PIT_MODE);
	outb(LATCH & 0xff,PIT_CH0);
	outb(LATCH >> 8,PIT_CH0);
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
    outb(0x34,PIT_MODE);
    outb(LATCH & 0xff,PIT_CH0);
    outb(LATCH >> 8,PIT_CH0);
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
	printk(KERN_WARNING "RTAI: Nested critical sync will fail.\n");

    return flags;
}

void rthal_critical_exit (unsigned long flags)

{
    atomic_inc(&rthal_sync_count);
    adeos_critical_exit(flags);
}

static void rthal_irq_trampoline (unsigned irq)

{
    rthal_realtime_irq[irq].hits[adeos_processor_id()]++;
    rthal_realtime_irq[irq].handler(irq,rthal_realtime_irq[irq].cookie);
}

int rthal_request_irq (unsigned irq,
		       void (*handler)(unsigned irq, void *cookie),
		       void *cookie)
{
    unsigned long flags;
    int err = 0;

    if (handler == NULL || irq >= IPIPE_NR_IRQS)
	return -EINVAL;

    flags = rthal_critical_enter(NULL);

    if (rthal_realtime_irq[irq].handler != NULL)
	{
	err = -EBUSY;
	goto unlock_and_exit;
	}

    err = adeos_virtualize_irq_from(&rthal_domain,
				    irq,
				    &rthal_irq_trampoline,
				    NULL,
				    IPIPE_DYNAMIC_MASK);
    if (!err)
	{
	rthal_realtime_irq[irq].handler = handler;
	rthal_realtime_irq[irq].cookie = cookie;
	}

 unlock_and_exit:

    rthal_critical_exit(flags);

    return err;
}

int rthal_release_irq (unsigned irq)

{
    int err = 0;
	
    if (irq >= IPIPE_NR_IRQS)
	return -EINVAL;

    err = adeos_virtualize_irq_from(&rthal_domain,
			      irq,
			      NULL,
			      NULL,
			      IPIPE_PASS_MASK);
    if (!err)
        xchg(&rthal_realtime_irq[irq].handler,NULL);

    return err;
}

/**
 * Enable an IRQ source.
 *
 */
int rthal_enable_irq (unsigned irq)

{
    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    irq_desc[irq].handler->enable(irq);

    return 0;
}

/**
 * Disable an IRQ source.
 *
 */
int rthal_disable_irq (unsigned irq)
{

    if (irq >= IPIPE_NR_XIRQS)
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
    int err = 0;

    if (irq >= IPIPE_NR_XIRQS || !handler)
	return -EINVAL;

    flags = rthal_spin_lock_irqsave(&irq_desc[irq].lock);

    if (rthal_linux_irq[irq].count++ == 0 && irq_desc[irq].action)
	{
	rthal_linux_irq[irq].flags = irq_desc[irq].action->flags;
	irq_desc[irq].action->flags |= SA_SHIRQ;
	}

    rthal_spin_unlock_irqrestore(flags,&irq_desc[irq].lock);

    err = request_irq(irq,handler,SA_SHIRQ,name,dev_id);

    return err;
}

int rthal_release_linux_irq (unsigned irq, void *dev_id)

{
    unsigned long flags;

    if (irq >= IPIPE_NR_XIRQS || rthal_linux_irq[irq].count == 0)
	return -EINVAL;

    free_irq(irq,dev_id);

    flags = rthal_spin_lock_irqsave(&irq_desc[irq].lock);

    if (--rthal_linux_irq[irq].count == 0 && irq_desc[irq].action)
	irq_desc[irq].action->flags = rthal_linux_irq[irq].flags;

    rthal_spin_unlock_irqrestore(flags,&irq_desc[irq].lock);

    return 0;
}

int rthal_pend_linux_irq (unsigned irq)

{
    return adeos_propagate_irq(irq);
}

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

int rthal_set_irq_affinity (unsigned irq, cpumask_t cpumask, cpumask_t *oldmask)

{
    cpumask_t _oldmask;

    if (irq >= IPIPE_NR_XIRQS)
	return -EINVAL;

    _oldmask = adeos_set_irq_affinity(irq,cpumask);

    if (oldmask)
	*oldmask = _oldmask;

    return cpus_empty(_oldmask) ? -EINVAL : 0;
}

#else /* !CONFIG_SMP */

int rthal_set_irq_affinity (unsigned irq, cpumask_t cpumask, cpumask_t *oldmask) {

    return 0;
}

#endif /* CONFIG_SMP */

#ifndef CONFIG_X86_TSC

static rthal_time_t rthal_tsc_8254;

static int rthal_last_8254_counter2;

/* TSC emulation using PIT channel #2. */

void rthal_setup_8254_tsc (void)

{
    unsigned long flags;
    int count;

    rthal_hw_lock(flags);

    outb_p(0x0,PIT_MODE);
    count = inb_p(PIT_CH0);
    count |= inb_p(PIT_CH0) << 8;
    outb_p(0xb4,PIT_MODE);
    outb_p(RTHAL_8254_COUNT2LATCH & 0xff,PIT_CH2);
    outb_p(RTHAL_8254_COUNT2LATCH >> 8,PIT_CH2);
    rthal_tsc_8254 = count + LATCH * jiffies;
    rthal_last_8254_counter2 = 0; 
    /* Gate high, disable speaker */
    outb_p((inb_p(0x61)&~0x2)|1,0x61);

    rthal_hw_unlock(flags);
}

rthal_time_t rthal_get_8254_tsc (void)

{
    unsigned long flags;
    int delta, count;
    rthal_time_t t;

    rthal_hw_lock(flags);

    outb(0xd8,PIT_MODE);
    count = inb(PIT_CH2);
    delta = rthal_last_8254_counter2 - (count |= (inb(PIT_CH2) << 8));
    rthal_last_8254_counter2 = count;
    rthal_tsc_8254 += (delta > 0 ? delta : delta + RTHAL_8254_COUNT2LATCH);
    t = rthal_tsc_8254;

    rthal_hw_unlock(flags);

    return t;
}

#endif /* !CONFIG_X86_TSC */

rthal_trap_handler_t rthal_set_trap_handler (rthal_trap_handler_t handler) {

    return (rthal_trap_handler_t)xchg(&rthal_trap_handler,handler);
}

static void rthal_trap_fault (adevinfo_t *evinfo)

{
    adeos_declare_cpuid;

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
    a GPF.
    2) NMI is not pipelined by Adeos. */

    adeos_load_cpuid();

    if (evinfo->domid == RTHAL_DOMAIN_ID)
	{
	rthal_realtime_faults[cpuid][evinfo->event]++;

	if (evinfo->event == 7)
	    {
	    struct pt_regs *regs = (struct pt_regs *)evinfo->evdata;
            print_symbol("Invalid use of FPU in RTAI context at %s\n",regs->eip);
	    }

	if (rthal_trap_handler != NULL &&
	    test_bit(cpuid,&rthal_cpu_realtime) &&
	    rthal_trap_handler(evinfo) != 0)
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

static void rthal_domain_entry (int iflag)

{
    unsigned trapnr;

#if !defined(CONFIG_ADEOS_NOTHREADS)
    if (!iflag)
	goto spin;
#endif /* !CONFIG_ADEOS_NOTHREADS */

    /* Trap all faults. */
    for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++)
	adeos_catch_event(trapnr,&rthal_trap_fault);

    printk(KERN_INFO "RTAI: hal/x86 loaded.\n");

#if !defined(CONFIG_ADEOS_NOTHREADS)
 spin:

    for (;;)
	adeos_suspend_domain();
#endif /* !CONFIG_ADEOS_NOTHREADS */
}

#ifdef CONFIG_PROC_FS

struct proc_dir_entry *rthal_proc_root;

static ssize_t hal_read_proc (char *page,
			      char **start,
			      off_t off,
			      int count,
			      int *eof,
			      void *data)
{
    int len, major, minor, patchlevel;

    /* Canonicalize the Adeos relno-candidate information to some
       major.minor.patchlevel format to be parser-friendly. */

    major = ADEOS_MAJOR_NUMBER;

    if (ADEOS_MINOR_NUMBER < 255)
	{
	--major;
	minor = 99;
	patchlevel = ADEOS_MINOR_NUMBER;
	}
    else
	{
	minor = 0;
	patchlevel = 0;
	}

    len = sprintf(page,"%d.%d.%d\n",major,minor,patchlevel);
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static ssize_t compiler_read_proc (char *page,
				   char **start,
				   off_t off,
				   int count,
				   int *eof,
				   void *data)
{
    int len;

    len = sprintf(page,"%s\n",CONFIG_RTAI_COMPILER);
    len -= off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static int irq_read_proc (char *page,
			  char **start,
			  off_t off,
			  int count,
			  int *eof,
			  void *data)
{
    int len = 0, cpuid, irq;
    char *p = page;

    p += sprintf(p,"IRQ  ");

    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	p += sprintf(p,"        CPU%d",cpuid);

    for (irq = 0; irq < IPIPE_NR_IRQS; irq++)
	{
	if (rthal_realtime_irq[irq].handler == NULL)
	    continue;

	p += sprintf(p,"\n%3d:",irq);

	for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	    p += sprintf(p,"  %12lu",rthal_realtime_irq[irq].hits[cpuid]);
	}

    p += sprintf(p,"\n");

    len = p - page - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if (len > count) len = count;
    if (len < 0) len = 0;

    return len;
}

static int faults_read_proc (char *page,
			     char **start,
			     off_t off,
			     int count,
			     int *eof,
			     void *data)
{
    static char *fault_labels[] = {
	[0] = "Divide error",
	[1] = "Debug",
	[2] = NULL,	/* NMI is not pipelined. */
	[3] = "Int3",
	[4] = "Overflow",
	[5] = "Bounds",
	[6] = "Invalid opcode",
	[7] = "FPU not available",
	[8] = "Double fault",
	[9] = "FPU segment overrun",
	[10] = "Invalid TSS",
	[11] = "Segment not present",
	[12] = "Stack segment",
	[13] = "General protection",
	[14] = "Page fault",
	[15] = "Spurious interrupt",
	[16] = "FPU error",
	[17] = "Alignment check",
	[18] = "Machine check",
	[19] = "SIMD error",
    };
    int len = 0, cpuid, trap;
    char *p = page;

    p += sprintf(p,"TRAP ");

    for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	p += sprintf(p,"        CPU%d",cpuid);

    for (trap = 0; trap < 20; trap++)
	{
	if (!fault_labels[trap])
	    continue;

	p += sprintf(p,"\n%3d: ",trap);

	for (cpuid = 0; cpuid < num_online_cpus(); cpuid++)
	    p += sprintf(p,"%12d",
			 rthal_realtime_faults[cpuid][trap]);

	p += sprintf(p,"   (%s)",fault_labels[trap]);
	}

    p += sprintf(p,"\n");

    len = p - page - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if (len > count) len = count;
    if (len < 0) len = 0;

    return len;
}

static struct proc_dir_entry *add_proc_leaf (const char *name,
					     read_proc_t rdproc,
					     write_proc_t wrproc,
					     void *data,
					     struct proc_dir_entry *parent)
{
    int mode = wrproc ? 0644 : 0444;
    struct proc_dir_entry *entry;

    entry = create_proc_entry(name,mode,parent);

    if (entry)
	{
	entry->nlink = 1;
	entry->data = data;
	entry->read_proc = rdproc;
	entry->write_proc = wrproc;
	entry->owner = THIS_MODULE;
	}

    return entry;
}

static int rthal_proc_register (void)

{
    rthal_proc_root = create_proc_entry("rtai",S_IFDIR, 0);

    if (!rthal_proc_root)
	{
	printk(KERN_ERR "RTAI: Unable to initialize /proc/rtai.\n");
	return -1;
        }

    rthal_proc_root->owner = THIS_MODULE;

    add_proc_leaf("hal",
		  &hal_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("compiler",
		  &compiler_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("irq",
		  &irq_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);

    add_proc_leaf("faults",
		  &faults_read_proc,
		  NULL,
		  NULL,
		  rthal_proc_root);
    return 0;
}

static void rthal_proc_unregister (void)

{
    remove_proc_entry("hal",rthal_proc_root);
    remove_proc_entry("compiler",rthal_proc_root);
    remove_proc_entry("irq",rthal_proc_root);
    remove_proc_entry("faults",rthal_proc_root);
    remove_proc_entry("rtai",NULL);
}

#endif /* CONFIG_PROC_FS */

int __rthal_init (void)

{
    adattr_t attr;
    int err;

    rthal_smi_init();
    rthal_smi_disable();
    
#ifdef CONFIG_X86_LOCAL_APIC
    if (!test_bit(X86_FEATURE_APIC,boot_cpu_data.x86_capability))
    {
        printk("RTAI: Local APIC absent or disabled!\n"
	           "      Disable APIC support or pass \"lapic\" as bootparam.\n");
        err = -ENODEV;
        goto out_smi_restore;
    }

#endif /* CONFIG_X86_LOCAL_APIC */

#ifdef CONFIG_SMP
    /* The nucleus also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */

    /* Allocate a virtual interrupt to handle sysreqs within the Linux
       domain. */
    rthal_sysreq_virq = adeos_alloc_irq();

    if (!rthal_sysreq_virq)
    {
        printk(KERN_ERR "RTAI: No virtual interrupt available.\n");
	    err = -EBUSY;
        goto out_smi_restore;
    }

    err = adeos_virtualize_irq(rthal_sysreq_virq,
			 &rthal_ssrq_trampoline,
			 NULL,
			 IPIPE_HANDLE_MASK);
    if (err)
    {
        printk(KERN_ERR "RTAI: Failed to virtualize IRQ.\n");
        goto out_free_irq;
    }

    if (rthal_cpufreq_arg == 0)
#ifdef CONFIG_X86_TSC
	{
	adsysinfo_t sysinfo;
	adeos_get_sysinfo(&sysinfo);
	/* FIXME: 4Ghz barrier is close... */
	rthal_cpufreq_arg = (unsigned long)sysinfo.cpufreq;
	}
#else /* ! CONFIG_X86_TSC */
    rthal_cpufreq_arg = CLOCK_TICK_RATE;
    rthal_setup_8254_tsc();
#endif /* CONFIG_X86_TSC */

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
    else 
    {
        printk(KERN_ERR "RTAI: Domain registration failed.\n");
        goto out_proc_unregister;
    }

    return 0;

out_proc_unregister:
#ifdef CONFIG_PROC_FS
    rthal_proc_unregister();
#endif
    adeos_virtualize_irq(rthal_sysreq_virq,NULL,NULL,0);
   
out_free_irq:
    adeos_free_irq(rthal_sysreq_virq);

out_smi_restore:
    rthal_smi_restore();		

    return err;
}

void __rthal_exit (void)

{
#ifdef CONFIG_SMP
    /* The nucleus also sets the same CPU affinity so that both
       modules keep their execution sequence on SMP boxen. */
    set_cpus_allowed(current,cpumask_of_cpu(0));
#endif /* CONFIG_SMP */

#ifdef CONFIG_PROC_FS
    rthal_proc_unregister();
#endif /* CONFIG_PROC_FS */

    if (rthal_sysreq_virq)
	{
	adeos_virtualize_irq(rthal_sysreq_virq,NULL,NULL,0);
	adeos_free_irq(rthal_sysreq_virq);
	}

    if (rthal_init_done)
	adeos_unregister_domain(&rthal_domain);

    rthal_smi_restore();
    
    printk(KERN_INFO "RTAI: hal/x86 unloaded.\n");
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
EXPORT_SYMBOL(rthal_request_timer);
EXPORT_SYMBOL(rthal_release_timer);
EXPORT_SYMBOL(rthal_set_trap_handler);
EXPORT_SYMBOL(rthal_calibrate_timer);

#ifndef CONFIG_X86_TSC
EXPORT_SYMBOL(rthal_get_8254_tsc);
#endif /* !CONFIG_X86_TSC */

EXPORT_SYMBOL(rthal_critical_enter);
EXPORT_SYMBOL(rthal_critical_exit);

EXPORT_SYMBOL(rthal_domain);
EXPORT_SYMBOL(rthal_tunables);
EXPORT_SYMBOL(rthal_cpu_realtime);
#ifdef CONFIG_PROC_FS
EXPORT_SYMBOL(rthal_proc_root);
#endif /* CONFIG_PROC_FS */
