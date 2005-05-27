/**
 *   @ingroup hal
 *   @file
 *
 *   Adeos-based Real-Time Abstraction Layer for ia64.
 *
 *   Copyright &copy; 2002-2004 Philippe Gerum
 *   Copyright &copy; 2004 The HYADES project <http://www.hyades-itea.org>
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
 * ia64-specific HAL services.
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

static void rthal_adjust_before_relay (unsigned irq, void *cookie)
{
    __adeos_itm_next[adeos_processor_id()] = ia64_get_itc();
    adeos_propagate_irq(irq);
}

static void rthal_set_itv(void)
{
    __adeos_itm_next[adeos_processor_id()] = ia64_get_itc();
    ia64_set_itv(irq_to_vector(__adeos_tick_irq));
}

static void rthal_timer_set_irq (unsigned tick_irq)
{
    unsigned long flags;

    flags = rthal_critical_enter(&rthal_set_itv);
    __adeos_tick_irq = tick_irq;
    rthal_set_itv();
    rthal_critical_exit(flags);
}

int rthal_timer_request (void (*handler)(void),
			 unsigned long nstick)
{
    unsigned long flags;

    flags = rthal_critical_enter(NULL);

    rthal_irq_release(RTHAL_TIMER_IRQ);
    
    adeos_tune_timer(nstick, nstick ? 0 : ADEOS_GRAB_TIMER);

    if (rthal_irq_request(RTHAL_TIMER_IRQ,
                          (rthal_irq_handler_t) handler,
                          NULL) < 0)
        {
        rthal_critical_exit(flags);
        return -EINVAL;
        }

    if (rthal_irq_request(RTHAL_HOST_TIMER_IRQ,
                          &rthal_adjust_before_relay,
                          NULL) < 0)
        {
        rthal_critical_exit(flags);
        return -EINVAL;
        }

    rthal_critical_exit(flags);

    rthal_timer_set_irq(RTHAL_TIMER_IRQ);

    return 0;
}

void rthal_timer_release (void)

{
    unsigned long flags;

    rthal_timer_set_irq(RTHAL_HOST_TIMER_IRQ);
    adeos_tune_timer(0, ADEOS_RESET_TIMER);
    flags = rthal_critical_enter(NULL);        
    rthal_irq_release(RTHAL_TIMER_IRQ);
    rthal_irq_release(RTHAL_HOST_TIMER_IRQ);
    rthal_critical_exit(flags);
}

unsigned long rthal_timer_calibrate (void)

{
    unsigned long flags, delay;
    rthal_time_t t, dt;
    int i;

    delay = RTHAL_CPU_FREQ; /* 1s */
    
    flags = rthal_critical_enter(NULL);

    t = rthal_rdtsc();

    for (i = 0; i < 10000; i++)
        rthal_timer_program_shot(delay);

    dt = rthal_rdtsc() - t;

    rthal_critical_exit(flags);

    return rthal_imuldiv(dt,100000,RTHAL_CPU_FREQ);
}

static void rthal_trap_fault (adevinfo_t *evinfo)

{
    adeos_declare_cpuid;

    adeos_load_cpuid();

    if (evinfo->domid == RTHAL_DOMAIN_ID)
	{
	struct pt_regs *regs = ((ia64trapinfo_t *)evinfo->evdata)->regs;

	rthal_realtime_faults[cpuid][evinfo->event]++;

        if (evinfo->event == ADEOS_FPDIS_TRAP)/* (FPU) Device not available. */
            {
            unsigned long ip = regs->cr_iip + ia64_psr(regs)->ri;
            print_symbol("Invalid use of FPU in RTAI domain at %s\n",ip);
            }

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

    printk(KERN_INFO "RTAI: hal/ia64 loaded.\n");

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
EXPORT_SYMBOL(rthal_prepare_stack);
