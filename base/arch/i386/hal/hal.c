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

#include <asm/rtai_hal.h>

#ifdef RTAI_DIAG_TSC_SYNC

/*
	Hacked from arch/ia64/kernel/smpboot.c.
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>

MODULE_LICENSE("GPL");

volatile long rtai_tsc_ofst[RTAI_NR_CPUS];

static inline long long readtsc(void)
{
	long long t;
	__asm__ __volatile__("rdtsc" : "=A" (t));
	return t;
}

#define MASTER	(0)
#define SLAVE	(SMP_CACHE_BYTES/8)

#define NUM_ITERS  8

static DEFINE_SPINLOCK(tsc_sync_lock);
static DEFINE_SPINLOCK(tsclock);
static volatile long long go[SLAVE + 1];

static void sync_master(void *arg)
{
	unsigned long flags, lflags, i;

	if ((unsigned long)arg != hard_smp_processor_id()) {
		return;
	}

	go[MASTER] = 0;
	local_irq_save(flags);
	for (i = 0; i < NUM_ITERS; ++i) {
		while (!go[MASTER]) {
			cpu_relax();
		}
		go[MASTER] = 0;
		spin_lock_irqsave(&tsclock, lflags);
		go[SLAVE] = readtsc();
		spin_unlock_irqrestore(&tsclock, lflags);
	}
	local_irq_restore(flags);
}

static volatile unsigned long long best_t0 = 0, best_t1 = ~0ULL, best_tm = 0;

static inline long long get_delta(long long *rt, long long *master, unsigned int slave)
{
//	unsigned long long best_t0 = 0, best_t1 = ~0ULL, best_tm = 0;
	unsigned long long tcenter, t0, t1, tm;
	long i, lflags;

	for (i = 0; i < NUM_ITERS; ++i) {
		t0 = readtsc();
		go[MASTER] = 1;
		spin_lock_irqsave(&tsclock, lflags);
		while (!(tm = go[SLAVE])) {
			spin_unlock_irqrestore(&tsclock, lflags);
			cpu_relax();
			spin_lock_irqsave(&tsclock, lflags);
		}
		spin_unlock_irqrestore(&tsclock, lflags);
		go[SLAVE] = 0;
		t1 = readtsc();
		if (t1 - t0 < best_t1 - best_t0) {
			best_t0 = t0, best_t1 = t1, best_tm = tm;
		}
	}

	*rt = best_t1 - best_t0;
	*master = best_tm - best_t0;
	tcenter = (best_t0/2 + best_t1/2);
	if (best_t0 % 2 + best_t1 % 2 == 2) {
		++tcenter;
	}

	return rtai_tsc_ofst[slave] = tcenter - best_tm;
}

static void sync_tsc(unsigned int master, unsigned int slave)
{
	unsigned long flags;
	long long delta, rt, master_time_stamp;

	go[MASTER] = 1;
	if (smp_call_function(sync_master, (void *)slave, 1, 0) < 0) {
//		printk(KERN_ERR "sync_tsc: failed to get attention of CPU %u!\n", master);
		return;
	}
	while (go[MASTER]) {
		cpu_relax();	/* wait for master to be ready */
	}
	spin_lock_irqsave(&tsc_sync_lock, flags);
	delta = get_delta(&rt, &master_time_stamp, slave);
	spin_unlock_irqrestore(&tsc_sync_lock, flags);

//	printk(KERN_INFO "CPU %u: synced its TSC with CPU %u (master time stamp %llu cycles, < - OFFSET %lld cycles - > , max double tsc read span %llu cycles)\n", slave, master, master_time_stamp, delta, rt);
}

//#define MASTER_CPU  0
#define SLEEP       1000 // ms
static volatile int end = 1;

static void kthread_fun(void *null)
{
	int k;
	set_cpus_allowed(current, cpumask_of_cpu(RTAI_MASTER_TSC_CPU));
	end = 0;
	while (!end) {
		for (k = 0; k < num_online_cpus(); k++) {
			if (k != RTAI_MASTER_TSC_CPU) {
				sync_tsc(RTAI_MASTER_TSC_CPU, k);
			}
		}
		msleep(SLEEP);
	}
	end = 0;
}

void init_tsc_sync(void)
{
	kernel_thread((void *)kthread_fun, NULL, 0);
	while(end) {
		msleep(100);
	}
}

void cleanup_tsc_sync(void)
{
	end = 1;
	while(end) {
		msleep(100);
	}
}

EXPORT_SYMBOL(rtai_tsc_ofst);

#endif

#undef INCLUDED_BY_HAL_C
#define INCLUDED_BY_HAL_C
#ifdef RTAI_DUOSS
#include "hal.immed"
#else
#include "hal.piped"
#endif
#include "rtc.c"
