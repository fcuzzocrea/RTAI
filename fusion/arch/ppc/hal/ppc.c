/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for PPC.
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
 *   RTAI/fusion is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License as
 *   published by the Free Software Foundation, Inc., 675 Mass Ave,
 *   Cambridge MA 02139, USA; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   RTAI/fusion is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *   General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 *   02111-1307, USA.
 */

/**
 * \ingroup hal
 *
 * PowerPC-specific HAL services.
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
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/unistd.h>
#include <nucleus/asm/hal.h>
#ifdef CONFIG_PROC_FS
#include <linux/proc_fs.h>
#endif /* CONFIG_PROC_FS */
#include <stdarg.h>

static int rthal_periodic_p;

int rthal_timer_request (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;
    int err;

    flags = rthal_critical_enter(NULL);

    if (nstick > 0)
	{
	/* Periodic setup --
	   Use the built-in Adeos service directly. */
	err = adeos_tune_timer(nstick,0);
	rthal_periodic_p = 1;
	}
    else
	{
	/* Oneshot setup. */
	disarm_decr[adeos_processor_id()] = 1;
	rthal_periodic_p = 0;
#ifdef CONFIG_40x
        mtspr(SPRN_TCR,mfspr(SPRN_TCR) & ~TCR_ARE); /* Auto-reload off. */
#endif /* CONFIG_40x */
	rthal_timer_program_shot(tb_ticks_per_jiffy);
	}

    rthal_irq_release(RTHAL_TIMER_IRQ);

    err = rthal_irq_request(RTHAL_TIMER_IRQ,
			    (rthal_irq_handler_t)handler,
			    NULL);

    rthal_critical_exit(flags);

    return err;
}

void rthal_timer_release (void)

{
    unsigned long flags;

    flags = rthal_critical_enter(NULL);

    if (rthal_periodic_p)
	adeos_tune_timer(0,ADEOS_RESET_TIMER);
    else
	{
	disarm_decr[adeos_processor_id()] = 0;
#ifdef CONFIG_40x
	mtspr(SPRN_TCR,mfspr(SPRN_TCR)|TCR_ARE); /* Auto-reload on. */
	mtspr(SPRN_PIT,tb_ticks_per_jiffy);
#else /* !CONFIG_40x */
	set_dec(tb_ticks_per_jiffy);
#endif /* CONFIG_40x */
	}

    rthal_irq_release(RTHAL_TIMER_IRQ);

    rthal_critical_exit(flags);
}

unsigned long rthal_timer_calibrate (void)

{
    return 1000000000 / RTHAL_CPU_FREQ;
}

static void rthal_trap_fault (adevinfo_t *evinfo)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    if (evinfo->domid == RTHAL_DOMAIN_ID)
	{
	rthal_realtime_faults[cpuid][evinfo->event]++;

	if (rthal_trap_handler != NULL &&
	    test_bit(cpuid,&rthal_cpu_realtime) &&
	    rthal_trap_handler(evinfo) != 0)
	    return;
	}

    adeos_propagate_event(evinfo);
}

void rthal_domain_entry (int iflag)

{
    unsigned trapnr;

#if !defined(CONFIG_ADEOS_NOTHREADS)
    if (!iflag)
	goto spin;
#endif /* !CONFIG_ADEOS_NOTHREADS */

    /* Trap all faults. */
    for (trapnr = 0; trapnr < ADEOS_NR_FAULTS; trapnr++)
	adeos_catch_event(trapnr,&rthal_trap_fault);

    printk(KERN_INFO "RTAI: hal/ppc loaded.\n");

#if !defined(CONFIG_ADEOS_NOTHREADS)
 spin:

    for (;;)
	adeos_suspend_domain();
#endif /* !CONFIG_ADEOS_NOTHREADS */
}

int rthal_arch_init (void)

{
    if (rthal_cpufreq_arg == 0)
	{
	adsysinfo_t sysinfo;
	adeos_get_sysinfo(&sysinfo);
	/* The CPU frequency is expressed as the timebase frequency
	   for this port. */
	rthal_cpufreq_arg = (unsigned long)sysinfo.cpufreq;
	}

    if (rthal_timerfreq_arg == 0)
	rthal_timerfreq_arg = rthal_tunables.cpu_freq;

    return 0;
}

void rthal_arch_cleanup (void)

{
    /* Nothing to cleanup so far. */
}

/*@}*/

EXPORT_SYMBOL(rthal_switch_context);

#ifdef CONFIG_RTAI_HW_FPU
EXPORT_SYMBOL(rthal_init_fpu);
EXPORT_SYMBOL(rthal_save_fpu);
EXPORT_SYMBOL(rthal_restore_fpu);
#endif /* CONFIG_RTAI_HW_FPU */
