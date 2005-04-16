/**
 *   @ingroup hal
 *   @file
 *
 *   Generic Real-Time HAL.
 *   Copyright &copy; 2005 Philippe Gerum.
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
 * @addtogroup hal
 *@{*/

#ifndef _RTAI_ASM_GENERIC_HAL_H
#define _RTAI_ASM_GENERIC_HAL_H

#ifdef __KERNEL__

#include <rtai_config.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#define RTHAL_DOMAIN_ID  0x52544149

#define RTHAL_NR_CPUS  ADEOS_NR_CPUS

#define RTHAL_NR_SRQS  BITS_PER_LONG

#define RTHAL_TIMER_FREQ  (rthal_tunables.timer_freq)
#define RTHAL_CPU_FREQ    (rthal_tunables.cpu_freq)

#define rthal_cli()                     adeos_stall_pipeline_from(&rthal_domain)
#define rthal_sti()                     adeos_unstall_pipeline_from(&rthal_domain)
#define rthal_local_irq_save(x)         ((x) = !!adeos_test_and_stall_pipeline_from(&rthal_domain))
#define rthal_local_irq_restore(x)      adeos_restore_pipeline_from(&rthal_domain,(x))
#define rthal_local_irq_flags(x)        ((x) = !!adeos_test_pipeline_from(&rthal_domain))
#define rthal_local_irq_test()          (!!adeos_test_pipeline_from(&rthal_domain))
#define rthal_local_irq_sync(x)         ((x) = !!adeos_test_and_unstall_pipeline_from(&rthal_domain))

#define rthal_hw_lock(flags)            adeos_hw_local_irq_save(flags)
#define rthal_hw_unlock(flags)          adeos_hw_local_irq_restore(flags)
#define rthal_hw_enable()               adeos_hw_sti()
#define rthal_hw_disable()              adeos_hw_cli()

#define rthal_linux_sti()                adeos_unstall_pipeline_from(adp_root)
#define rthal_linux_cli()                adeos_stall_pipeline_from(adp_root)
#define rthal_linux_local_irq_save(x)    ((x) = !!adeos_test_and_stall_pipeline_from(adp_root))
#define rthal_linux_local_irq_restore(x) adeos_restore_pipeline_from(adp_root,x)
#define rthal_linux_local_irq_restore_nosync(x,cpuid) adeos_restore_pipeline_nosync(adp_root,x,cpuid)

#define rthal_spin_lock(lock)    adeos_spin_lock(lock)
#define rthal_spin_unlock(lock)  adeos_spin_unlock(lock)

#define rthal_spin_lock_irq(lock) \
do {  \
    rthal_cli(); \
    rthal_spin_lock(lock); \
} while(0)

#define rthal_spin_unlock_irq(lock) \
do {  \
    rthal_spin_unlock(lock); \
    rthal_sti(); \
} while(0)

#define rthal_spin_lock_irqsave(lock,flags) \
do {  \
    rthal_local_irq_save(flags); \
    rthal_spin_lock(lock); \
} while(0)

#define rthal_spin_unlock_irqrestore(lock,flags) \
do {  \
    rthal_spin_unlock(lock); \
    rthal_local_irq_restore(flags); \
} while(0)

typedef void (*rthal_irq_handler_t)(unsigned irq,
				    void *cookie);

struct rthal_calibration_data {

    unsigned long cpu_freq;
    unsigned long timer_freq;
};

typedef int (*rthal_trap_handler_t)(adevinfo_t *evinfo);

extern unsigned long rthal_cpufreq_arg;

extern unsigned long rthal_timerfreq_arg;

extern adomain_t rthal_domain;

extern struct rthal_calibration_data rthal_tunables;

extern volatile int rthal_sync_op;

extern volatile unsigned long rthal_cpu_realtime;

extern rthal_trap_handler_t rthal_trap_handler;

extern int rthal_realtime_faults[RTHAL_NR_CPUS][ADEOS_NR_FAULTS];

extern void rthal_domain_entry(int iflag);

extern int rthal_arch_init(void);

extern void rthal_arch_cleanup(void);

    /* Private interface -- Internal use only */

unsigned long rthal_critical_enter(void (*synch)(void));

void rthal_critical_exit(unsigned long flags);

    /* Public interface */

#define rthal_printk    printk /* This is safe over Adeos */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int rthal_irq_request(unsigned irq,
		      void (*handler)(unsigned irq, void *cookie),
		      void *cookie);

int rthal_irq_release(unsigned irq);

int rthal_irq_enable(unsigned irq);

int rthal_irq_disable(unsigned irq);

int rthal_irq_host_request(unsigned irq,
			    irqreturn_t (*handler)(int irq,
						   void *dev_id,
						   struct pt_regs *regs), 
			    char *name,
			    void *dev_id);

int rthal_irq_host_release(unsigned irq,
			    void *dev_id);

int rthal_irq_host_pend(unsigned irq);

int rthal_apc_alloc(const char *name,
		      void (*handler)(void *cookie),
		      void *cookie);

int rthal_apc_free(int apc);

int rthal_apc_schedule(int apc);

int rthal_irq_affinity(unsigned irq,
			   cpumask_t cpumask,
			   cpumask_t *oldmask);

int rthal_timer_request(void (*handler)(void),
			unsigned long nstick);

void rthal_timer_release(void);

rthal_trap_handler_t rthal_trap_catch(rthal_trap_handler_t handler);

unsigned long rthal_timer_calibrate(void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

/*@}*/

#endif /* !_RTAI_ASM_GENERIC_HAL_H */
