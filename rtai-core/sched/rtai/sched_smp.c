/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
ACKNOWLEDGMENTS: 
- Steve Papacharalambous (stevep@zentropix.com) has contributed a very 
  informative proc filesystem procedure.
- Stefano Picerno (stefanopp@libero.it) for suggesting a simple fix to
  distinguish a timeout from an abnormal retrun in timed sem waits.
- Geoffrey Martin (gmartin@altersys.com) for a fix to functions with timeouts.
*/


#ifdef CONFIG_RTAI_MAINTAINER_PMA
#define ALLOW_RR	1
#define ONE_SHOT 	0
#define PREEMPT_ALWAYS	0
#define LINUX_FPU 	1
#else /* STANDARD SETTINGS */
#define ALLOW_RR	1
#define ONE_SHOT 	0
#define PREEMPT_ALWAYS	0
#define LINUX_FPU 	1
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/timex.h>
#include <linux/sched.h>

#include <asm/param.h>
#include <asm/system.h>
#include <asm/io.h>
#include <asm/segment.h>

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <rtai_proc_fs.h>
#endif

#include <rtai.h>
#include <asm/rtai_sched.h>
#include <rtai_sched.h>
#include <rtai_schedcore.h>

MODULE_LICENSE("GPL");

#ifdef __USE_APIC__

#define TIMER_CHIP "APIC"
#define TIMER_FREQ RTAI_FREQ_APIC
#define TIMER_LATENCY RTAI_LATENCY_APIC
#define TIMER_SETUP_TIME RTAI_SETUP_TIME_APIC
static volatile unsigned long timer_cpu;
#define set_timer_cpu(cpu_map)  do { timer_cpu = cpu_map; } while (0)
#define is_timer_cpu(cpuid) \
	if (!test_bit((cpuid), &timer_cpu)) { \
		sched_release_global_lock((cpuid)); \
		return; \
	}
int rt_get_timer_cpu(void) { return timer_cpu; }
#define update_linux_timer()
irqreturn_t rtai_broadcast_to_local_timers(int irq,void *dev_id,struct pt_regs *regs);
#define BROADCAST_TO_LOCAL_TIMERS() rtai_broadcast_to_local_timers(-1,NULL,NULL)
#define FREE_LOCAL_TIMERS() rt_free_linux_irq(TIMER_8254_IRQ, &rtai_broadcast_to_local_timers)

#else

#define TIMER_CHIP "8254"
#define TIMER_FREQ RTAI_FREQ_8254
#define TIMER_LATENCY RTAI_LATENCY_8254
#define TIMER_SETUP_TIME RTAI_SETUP_TIME_8254
#define set_timer_cpu(cpu_map) 
#define is_timer_cpu(cpuid)
int rt_get_timer_cpu(void) { return -EINVAL; }
#define update_linux_timer() rt_pend_linux_irq(TIMER_8254_IRQ)
#define BROADCAST_TO_LOCAL_TIMERS()
#define FREE_LOCAL_TIMERS()

#endif

#ifdef CONFIG_PROC_FS
// proc filesystem additions.
static int rtai_proc_sched_register(void);
static void rtai_proc_sched_unregister(void);
// End of proc filesystem additions. 
#endif

/* +++++++++++++++++ WHAT MUST BE AVAILABLE EVERYWHERE ++++++++++++++++++++++ */

RT_TASK rt_smp_linux_task[NR_RT_CPUS];

RT_TASK *rt_smp_current[NR_RT_CPUS];

RTIME rt_smp_time_h[1];

int rt_smp_oneshot_timer[1];

struct klist_t wake_up_srq;

/* +++++++++++++++ END OF WHAT MUST BE AVAILABLE EVERYWHERE +++++++++++++++++ */

static int sched_rqsted[NR_RT_CPUS];

DEFINE_LINUX_SMP_CR0;

#define linux_cr0 (linux_smp_cr0[cpuid])

//#define rt_linux_task (rt_smp_linux_task[cpuid])

#define rt_base_linux_task (rt_smp_linux_task[0])

//static RTIME rt_time_h;
#undef rt_time_h
#define rt_time_h (rt_smp_time_h[0])

static int rt_half_tick;

//static int oneshot_timer;
#undef oneshot_timer
#define oneshot_timer (rt_smp_oneshot_timer[0])

static int oneshot_running;

static int shot_fired;

static int preempt_always;

#ifdef CONFIG_RTAI_ADEOS
static unsigned sched_virq;
#endif /* CONFIG_RTAI_ADEOS */

static rwlock_t task_list_lock = RW_LOCK_UNLOCKED;

static RT_TASK *wdog_task[NR_RT_CPUS];

#define MAX_FRESTK_SRQ  64
static struct { int srq, in, out; void *mp[MAX_FRESTK_SRQ]; } frstk_srq;

/* ++++++++++++++++++++++++++++++++ TASKS ++++++++++++++++++++++++++++++++++ */

#define TASK_TO_SCHEDULE_ON_IPI() \
	while ((task = task->rnext) != &rt_base_linux_task) { \
		if (!task->running && test_bit(cpuid, &(task->runnable_on_cpus))) { \
			prio = (new_task = task)->priority; \
			break; \
		} \
	}

#define TASK_TO_SCHEDULE() \
	while ((task = task->rnext) != &rt_base_linux_task) { \
		if (!task->running) { \
			if (test_bit(cpuid, &(task->runnable_on_cpus))) { \
				prio = (new_task = task)->priority; \
				while ((task = task->rnext) != &rt_base_linux_task) { \
					if (!task->running) { \
						cpus_with_ready_tasks |= task->runnable_on_cpus; \
					} \
				} \
				break; \
			} else { \
				cpus_with_ready_tasks |= task->runnable_on_cpus; \
			} \
		} \
	}

asmlinkage static void rt_startup(void(*rt_thread)(int), int data)
{
	extern int rt_task_delete(RT_TASK *);
	rt_global_sti();
	RT_CURRENT->exectime[1] = rdtsc();
	rt_thread(data);
	rt_task_delete(rt_smp_current[hard_cpu_id()]);
}

#ifdef CONFIG_SMP

unsigned long sqilter = 0;

#define cpu_present_map (CPUMASK(cpu_online_map))

static inline void sched_get_global_lock(int cpuid)
{
	if (!test_and_set_bit(cpuid, locked_cpus)) {
		while (test_and_set_bit(31, locked_cpus)) {
#ifdef STAGGER
			STAGGER(cpuid);
#endif
		}
	}
}


static inline void sched_release_global_lock(int cpuid)
{
	if (test_and_clear_bit(cpuid, locked_cpus)) {
		test_and_clear_bit(31, locked_cpus);
#ifdef STAGGER
		STAGGER(cpuid);
#endif
	}
}


static inline void smp_send_sched_ipi(unsigned long dest)
{
	if (dest) {
		send_sched_ipi(dest);
	}
}

#define rt_request_sched_ipi()  rt_request_cpu_own_irq(SCHED_IPI, rt_schedule_on_schedule_ipi)

#define rt_free_sched_ipi()     rt_free_cpu_own_irq(SCHED_IPI)

#else

static unsigned long cpu_present_map = 1;

#define sched_get_global_lock(cpuid)

#define sched_release_global_lock(cpuid)

#define smp_send_sched_ipi(dest)

#define rt_request_sched_ipi()

#define rt_free_sched_ipi()

#endif


int rt_task_init(RT_TASK *task, void (*rt_thread)(int), int data,
			int stack_size, int priority, int uses_fpu,
			void(*signal)(void))
{
	unsigned long flags;
	int cpuid, *st, i, ok;

	if (task->magic == RT_TASK_MAGIC || priority < 0) {
		return -EINVAL;
	}
	if (!(st = (int *)sched_malloc(stack_size))) {   
		return -ENOMEM;
	}
	if (priority == RT_SCHED_HIGHEST_PRIORITY) {	    // watchdog reserves highest
	    for (i = ok = 0; i < NR_RT_CPUS; i++) { // priority task on each CPU
		if (!wdog_task[i] || wdog_task[i] == task) {
		    ok = 1;
		    break;
		}
	    }
	    if (!ok) {
		rt_printk("Highest priority reserved for RTAI watchdog\n");
		return -EBUSY;
	    }
	}

	task->bstack = task->stack = (int *)(((unsigned long)st + stack_size - 0x10) & ~0xF);
	task->stack[0] = 0;
	task->uses_fpu = uses_fpu ? 1 : 0;
	task->runnable_on_cpus = cpu_present_map;
	*(task->stack_bottom = st) = 0;
	task->lnxtsk = 0;
	task->magic = RT_TASK_MAGIC; 
	task->policy = 0;
	task->is_hard = 1;
	task->suspdepth = 1;
	task->state = (RT_SCHED_SUSPENDED | RT_SCHED_READY);
	task->running = 0;
	task->owndres = 0;
	task->priority = task->base_priority = priority;
	task->prio_passed_to = 0;
	task->period = 0;
	task->resume_time = RT_TIME_END;
	task->queue.prev = &(task->queue);      
	task->queue.next = &(task->queue);      
	task->queue.task = task;
	task->msg_queue.prev = &(task->msg_queue);      
	task->msg_queue.next = &(task->msg_queue);      
	task->msg_queue.task = task;    
	task->msg = 0;  
        task->ret_queue.prev = &(task->ret_queue);
        task->ret_queue.next = &(task->ret_queue);
        task->ret_queue.task = NOTHING;
        task->tprev = task->tnext =
        task->rprev = task->rnext = task;
	task->blocked_on = NOTHING;        
	task->signal = signal;
	for (i = 0; i < RTAI_NR_TRAPS; i++) {
		task->task_trap_handler[i] = NULL;
	}
	task->tick_queue        = NOTHING;
	task->trap_handler_data = NOTHING;
	task->resync_frame = 0;
	task->ExitHook = 0;
	task->exectime[0] = 0;
	task->system_data_ptr = 0;

	init_arch_stack();

	flags = rt_global_save_flags_and_cli();
	cpuid = hard_cpu_id();
#define fpu_task (&rt_linux_task)  // needed just by the very next line of code
	init_fp_env();
	task->next = 0;
	read_lock(&task_list_lock);
	rt_base_linux_task.prev->next = task;
	task->prev = rt_base_linux_task.prev;
	rt_base_linux_task.prev = task;
	read_unlock(&task_list_lock);
	rt_global_restore_flags(flags);

	return 0;
}


int rt_task_init_cpuid(RT_TASK *task, void (*rt_thread)(int), int data,
			int stack_size, int priority, int uses_fpu,
			void(*signal)(void), unsigned int cpuid)
{
	int retval;

	if (num_online_cpus() <= 1) {
                 cpuid = 0;
        }
	retval = rt_task_init(task, rt_thread, data, stack_size, priority, 
							uses_fpu, signal);
	rt_set_runnable_on_cpus(task, 1L << cpuid);
	return retval;
}


int rt_kthread_init_cpuid(RT_TASK *task, void (*rt_thread)(int), int data,
                        int stack_size, int priority, int uses_fpu,
                        void(*signal)(void), unsigned int cpuid)
{
	return 0;
}


int rt_kthread_init(RT_TASK *task, void (*rt_thread)(int), int data,
                        int stack_size, int priority, int uses_fpu,
                        void(*signal)(void))
{
	return 0;
}


void rt_set_runnable_on_cpus(RT_TASK *task, unsigned long runnable_on_cpus)
{
	unsigned long flags;
	flags = rt_global_save_flags_and_cli();
	if (!(task->runnable_on_cpus = (runnable_on_cpus & cpu_present_map))) {
		task->runnable_on_cpus = 1;
	}
	rt_global_restore_flags(flags);
}


void rt_set_runnable_on_cpuid(RT_TASK *task, unsigned int cpuid)
{
	rt_set_runnable_on_cpus(task, 1L << cpuid);
}


int rt_check_current_stack(void)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;
	char *sp;
 
	hard_save_flags_and_cli(flags);
	if ((ASSIGN_RT_CURRENT) != &rt_linux_task) {
		sp = get_stack_pointer();
		hard_restore_flags(flags);
		return (sp - (char *)(rt_current->stack_bottom));
	} else {
		hard_restore_flags(flags);
		return -0x7FFFFFFF;
	}
}


#if ALLOW_RR
static RTIME global_yield_time = RT_TIME_END;
static int global_policy;

#define RR_YIELD() \
if (rt_current->policy > 0) { \
	rt_current->rr_remaining = rt_current->yield_time - rt_times.tick_time; \
	if (rt_current->rr_remaining <= 0) { \
		rt_current->rr_remaining = rt_current->rr_quantum; \
		if (rt_current->state == RT_SCHED_READY) { \
			RT_TASK *task; \
			task = rt_current->rnext; \
			while (rt_current->priority == task->priority) { \
				task = task->rnext; \
			} \
			if (task != rt_current->rnext) { \
				rt_current->running = 0; \
				(rt_current->rprev)->rnext = rt_current->rnext; \
				(rt_current->rnext)->rprev = rt_current->rprev; \
				task->rprev = (rt_current->rprev = task->rprev)->rnext = rt_current; \
				rt_current->rnext = task; \
			} \
		} \
	} \
}

#define RR_SETYT() \
if (new_task->policy > 0) { \
	new_task->yield_time = rt_time_h + new_task->rr_remaining; \
	global_policy = 1; \
	if (new_task->yield_time < global_yield_time) { \
		global_yield_time = new_task->yield_time; \
	} \
}

#define RR_SPREMP() \
if (global_policy > 0) { \
	preempt = 1; \
	if (global_yield_time < intr_time) { \
		RTIME t; \
		t = intr_time; \
		intr_time = global_yield_time; \
		global_yield_time = t; \
	} else { \
		global_yield_time = intr_time; \
	} \
	global_policy = 0; \
} else { \
	preempt = 0; \
}

#define RR_TPREMP() \
if (global_policy > 0) { \
	preempt = 1; \
	if (global_yield_time < rt_times.intr_time) { \
		RTIME t; \
		t = rt_times.intr_time; \
		rt_times.intr_time = global_yield_time; \
		global_yield_time = t; \
	} else { \
		global_yield_time = rt_times.intr_time; \
	} \
	global_policy = 0; \
} else { \
   preempt = (preempt_always || prio == RT_SCHED_LINUX_PRIORITY);	\
}
#else
#define RR_YIELD()

#define RR_SETYT()

#define RR_SPREMP() \
do { preempt = 0; } while (0)

#define RR_TPREMP() \
    do { preempt = (preempt_always || prio == RT_SCHED_LINUX_PRIORITY); } while (0)
#endif

#define EXECTIME
#ifdef EXECTIME
static RTIME switch_time[NR_RT_CPUS];
#define KEXECTIME() \
do { \
	RTIME now; \
	now = rdtsc(); \
	if (!rt_current->lnxtsk) { \
		rt_current->exectime[0] += (now - switch_time[cpuid]); \
	} \
	switch_time[cpuid] = now; \
} while (0)
#else
#define KEXECTIME()
#endif

#ifdef CONFIG_SMP
//#define CAUTIOUS
#ifdef CAUTIOUS

static void rt_schedule_on_schedule_ipi(void)
{
	DECLARE_RT_CURRENT;
	RT_TASK *task, *new_task;
	int prio;

	prio = RT_SCHED_LINUX_PRIORITY;
	ASSIGN_RT_CURRENT;
	sched_rqsted[cpuid] = 1;
	new_task = &rt_linux_task;
	task = &rt_base_linux_task;

	sched_get_global_lock(cpuid);
	rt_current->running = 0;
	RR_YIELD();
        TASK_TO_SCHEDULE_ON_IPI();
	RR_SETYT();
	new_task->running = new_task->state = RT_SCHED_READY;

	if (new_task != rt_current) {
		if (rt_current == &rt_linux_task) {
			rt_switch_to_real_time(cpuid);
			save_cr0_and_clts(linux_cr0);
		}
		if (rt_current->uses_fpu) {
			enable_fpu();
			save_fpenv(rt_current->fpu_reg);
			if (new_task->uses_fpu) {
				restore_fpenv(new_task->fpu_reg);
			}
		} else if (new_task->uses_fpu) {
			enable_fpu();
			restore_fpenv(new_task->fpu_reg);
		}
		KEXECTIME();
		rt_exchange_tasks(rt_smp_current[cpuid], new_task);
		sched_release_global_lock(hard_cpu_id());
		if (rt_current->signal) {
			(*rt_current->signal)();
		}
		return;
	} else {
		sched_release_global_lock(cpuid);
	}
	return;
}

#else

static void rt_schedule_on_schedule_ipi(void)
{
	DECLARE_RT_CURRENT;
	RT_TASK *task, *new_task;
	int prio;

	ASSIGN_RT_CURRENT;
	sched_rqsted[cpuid] = 1;
	do {
		prio = RT_SCHED_LINUX_PRIORITY;
		new_task = &rt_linux_task;
		task = &rt_base_linux_task;

		read_lock(&task_list_lock);
		RR_YIELD();
		TASK_TO_SCHEDULE_ON_IPI();
		RR_SETYT();
		read_unlock(&task_list_lock);

		if (rt_current->running && rt_current->priority <= prio) {
			return;
		}
		sched_get_global_lock(cpuid);
		if (new_task->state == RT_SCHED_READY && !new_task->running) {
			rt_current->running = 0;
			new_task->running = new_task->state = RT_SCHED_READY;
			if (rt_current == &rt_linux_task) {
				rt_switch_to_real_time(cpuid);
				save_cr0_and_clts(linux_cr0);
			}
			if (rt_current->uses_fpu) {
				enable_fpu();
				save_fpenv(rt_current->fpu_reg);
				if (new_task->uses_fpu) {
					restore_fpenv(new_task->fpu_reg);
				}
			} else if (new_task->uses_fpu) {
				enable_fpu();
				restore_fpenv(new_task->fpu_reg);
			}
			KEXECTIME();
			rt_exchange_tasks(rt_smp_current[cpuid], new_task);
			sched_release_global_lock(hard_cpu_id());
			if (rt_current->signal) {
				(*rt_current->signal)();
			}
			return;
		}
		sched_release_global_lock(cpuid);
	} while (1);
}

#endif
#endif

#define ANTICIPATE

void rt_schedule(void)
{
	DECLARE_RT_CURRENT;
	RT_TASK *task, *new_task;
	RTIME intr_time, now;
	unsigned int cpus_with_ready_tasks;
	int prio, delay, preempt;

#ifdef CONFIG_RTAI_ADEOS
	if (adp_current != &rtai_domain)
	    {
	    adeos_trigger_irq(sched_virq);
	    return;
	    }
#endif /* CONFIG_RTAI_ADEOS */

	task = &rt_base_linux_task;
	cpus_with_ready_tasks = 0;
	prio = RT_SCHED_LINUX_PRIORITY;
	ASSIGN_RT_CURRENT;
	new_task = &rt_linux_task;
	rt_current->running = 0;
	RR_YIELD();
	if (oneshot_running) {
#ifdef ANTICIPATE
		rt_time_h = rdtsc() + rt_half_tick;
		wake_up_timed_tasks(0);
#endif
                TASK_TO_SCHEDULE();
		RR_SETYT();

		intr_time = shot_fired ? rt_times.intr_time :
			    rt_times.intr_time + rt_times.linux_tick;
		RR_SPREMP();
		task = &rt_base_linux_task;
                while ((task = task->tnext) != &rt_base_linux_task) {
                        if (task->priority <= prio && task->resume_time < intr_time) {
                                intr_time = task->resume_time;
                                preempt = 1;
                                break;
                        }
                }

		if (preempt || (!shot_fired && (prio == RT_SCHED_LINUX_PRIORITY))) {
			shot_fired = 1;
			if (preempt) {
				rt_times.intr_time = intr_time;
			}
			delay = (int)(rt_times.intr_time - (now = rdtsc())) - tuned.latency;
			if (delay >= tuned.setup_time_TIMER_CPUNIT) {
				delay = imuldiv(delay, TIMER_FREQ, tuned.cpu_freq);
			} else {
				delay = tuned.setup_time_TIMER_UNIT;
				rt_times.intr_time = now + (tuned.setup_time_TIMER_CPUNIT);
			}
			set_timer_cpu(1 << cpuid);
			rt_set_timer_delay(delay);
		}
	} else {
                TASK_TO_SCHEDULE();
		RR_SETYT();
	}

	new_task->running = new_task->state = RT_SCHED_READY;
	smp_send_sched_ipi(cpus_with_ready_tasks & ~(1 << cpuid));

	if (new_task != rt_current) {
		if (rt_current == &rt_linux_task) {
			rt_switch_to_real_time(cpuid);
			save_cr0_and_clts(linux_cr0);
		}
		if (rt_current->uses_fpu) {
			enable_fpu();
			save_fpenv(rt_current->fpu_reg);
			if (new_task->uses_fpu) {
				restore_fpenv(new_task->fpu_reg);
			}
		} else if (new_task->uses_fpu) {
			enable_fpu();
			restore_fpenv(new_task->fpu_reg);
		}
		KEXECTIME();
		if (new_task == &rt_linux_task) {
			restore_cr0(linux_cr0);
			rt_switch_to_linux(cpuid);
			/* From now on, the Linux stage is re-enabled,
			   but not sync'ed until we have actually
			   switched to the Linux task, so that we
			   don't end up running the Linux IRQ handlers
			   on behalf of a non-Linux stack
			   context... */
		}
		rt_exchange_tasks(rt_smp_current[cpuid], new_task);
		if (rt_current->signal) {
			sched_release_global_lock(cpuid = hard_cpu_id());
			(*rt_current->signal)();
			sched_get_global_lock(cpuid);
		}
	}
}



void rt_spv_RMS(int cpuid)
{
	RT_TASK *task;
	int prio;
	prio = 0;
	task = &rt_base_linux_task;
	while ((task = task->next)) {
		RT_TASK *task, *htask;
		RTIME period;
		htask = 0;
		task = &rt_base_linux_task;
		period = RT_TIME_END;
		while ((task = task->next)) {
			if (task->priority >= 0 && task->policy >= 0 && task->period && task->period < period) {
				period = (htask = task)->period;
			}
		}
		if (htask) {
			htask->priority = -1;
			htask->base_priority = prio++;
		} else {
			goto ret;
		}
	}
ret:	task = &rt_base_linux_task;
	while ((task = task->next)) {
		if (task->priority < 0) {
			task->priority = task->base_priority;
		}
	}
	return;
}


void rt_sched_lock(void)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (rt_current->priority >= 0) {
		rt_current->sched_lock_priority = rt_current->priority;
		sched_rqsted[cpuid] = rt_current->priority = -1;
	} else {
		rt_current->priority--;
	}
	rt_global_restore_flags(flags);
}


void rt_sched_unlock(void)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (rt_current->priority < 0 && !(++rt_current->priority)) {
		if ((rt_current->priority = rt_current->sched_lock_priority) != RT_SCHED_LINUX_PRIORITY) {
			(rt_current->rprev)->rnext = rt_current->rnext;
			(rt_current->rnext)->rprev = rt_current->rprev;
			enq_ready_task(rt_current);
		}
		if (sched_rqsted[cpuid] > 0) {
			rt_schedule();
		}
	}
	rt_global_restore_flags(flags);
}


int rt_task_delete(RT_TASK *task)
{
	RT_TASK *rt_current;
	unsigned long flags;
	QUEUE *q;

	if (task->magic != RT_TASK_MAGIC || task->priority == RT_SCHED_LINUX_PRIORITY) {
		return -EINVAL;
	}

	flags = rt_global_save_flags_and_cli();
	rt_current = RT_CURRENT;
	if (!(task->owndres & SEMHLF) || task == rt_current || rt_current->priority == RT_SCHED_LINUX_PRIORITY) {
		call_exit_handlers(task);
		rem_timed_task(task);
		if (task->blocked_on) {
			(task->queue.prev)->next = task->queue.next;
			(task->queue.next)->prev = task->queue.prev;
			if (task->state & RT_SCHED_SEMAPHORE) {
				((SEM *)(task->blocked_on))->count++;
				if (((SEM *)(task->blocked_on))->type && ((SEM *)(task->blocked_on))->count > 1) {
					((SEM *)(task->blocked_on))->count = 1;
				}
			}
		}
		q = &(task->msg_queue);
		while ((q = q->next) != &(task->msg_queue)) {
			rem_timed_task(q->task);
			if ((q->task)->state != RT_SCHED_READY && ((q->task)->state &= ~(RT_SCHED_SEND | RT_SCHED_RPC | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(q->task);
			}       
			(q->task)->blocked_on = 0;
		}       
                q = &(task->ret_queue);
       	        while ((q = q->next) != &(task->ret_queue)) {
			rem_timed_task(q->task);
               	        if ((q->task)->state != RT_SCHED_READY && ((q->task)->state &= ~(RT_SCHED_RETURN | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(q->task);
			}       
			(q->task)->blocked_on = 0;
                }
		write_lock(&task_list_lock);
		if (!((task->prev)->next = task->next)) {
			rt_base_linux_task.prev = task->prev;
		} else {
			(task->next)->prev = task->prev;
		}
		write_unlock(&task_list_lock);
		frstk_srq.mp[frstk_srq.in] = task->stack_bottom;
		frstk_srq.in = (frstk_srq.in + 1) & (MAX_FRESTK_SRQ - 1);
		task->magic = 0;
		rt_pend_linux_srq(frstk_srq.srq);
		rem_ready_task(task);
		task->state = 0;
		if (task == rt_current) {
			rt_schedule();
		}
	} else {
		task->suspdepth = -0x7FFFFFFF;
	}
	rt_global_restore_flags(flags);
	return 0;
}


static void rt_timer_handler(void)
{
	DECLARE_RT_CURRENT;
	RTIME now;
	RT_TASK *task, *new_task;
	unsigned int cpus_with_ready_tasks;
	int prio, delay, preempt; 

	ASSIGN_RT_CURRENT;
	sched_rqsted[cpuid] = 1;
	task = &rt_base_linux_task;
	prio = RT_SCHED_LINUX_PRIORITY;
	cpus_with_ready_tasks = 0;
	new_task = &rt_linux_task;

	sched_get_global_lock(cpuid);
	is_timer_cpu(cpuid);
#ifdef CONFIG_X86_REMOTE_DEBUG
	if (oneshot_timer) {	// Resync after possibly hitting a breakpoint
	    	rt_times.intr_time = rdtsc();
	}
#endif
	rt_times.tick_time = rt_times.intr_time;
	rt_time_h = rt_times.tick_time + rt_half_tick;
	if (rt_times.tick_time >= rt_times.linux_time) {
		rt_times.linux_time += rt_times.linux_tick;
		update_linux_timer();
	}
	rt_current->running = 0;
	wake_up_timed_tasks(0);
	RR_YIELD();
	TASK_TO_SCHEDULE();
	RR_SETYT();

	if (oneshot_timer) {
		rt_times.intr_time = rt_times.linux_time > rt_times.tick_time ?
		rt_times.linux_time : rt_times.tick_time + rt_times.linux_tick;
		RR_TPREMP();

		task = &rt_base_linux_task;
                while ((task = task->tnext) != &rt_base_linux_task) {
                        if (task->priority <= prio && task->resume_time < rt_times.intr_time) {
                                rt_times.intr_time = task->resume_time;
                                preempt = 1;
                                break;
                        }
                }
		if ((shot_fired = preempt)) {
			delay = (int)(rt_times.intr_time - (now = rdtsc())) - tuned.latency;
			if (delay >= tuned.setup_time_TIMER_CPUNIT) {
				delay = imuldiv(delay, TIMER_FREQ, tuned.cpu_freq);
			} else {
				delay = tuned.setup_time_TIMER_UNIT;
				rt_times.intr_time = now + (tuned.setup_time_TIMER_CPUNIT);
			}
			set_timer_cpu(1 << cpuid);
			rt_set_timer_delay(delay);
		}
	} else {
		rt_times.intr_time += rt_times.periodic_tick;
		rt_set_timer_delay(0);
	}
	new_task->running = new_task->state = RT_SCHED_READY;
	smp_send_sched_ipi(cpus_with_ready_tasks & ~(1 << cpuid));

	if (new_task != rt_current) {
		if (rt_current == &rt_linux_task) {
			rt_switch_to_real_time(cpuid);
			save_cr0_and_clts(linux_cr0);
		}
		if (rt_current->uses_fpu) {
			enable_fpu();
			save_fpenv(rt_current->fpu_reg);
			if (new_task->uses_fpu) {
				restore_fpenv(new_task->fpu_reg);
			}
		} else if (new_task->uses_fpu) {
			enable_fpu();
			restore_fpenv(new_task->fpu_reg);
		}
		KEXECTIME();
		rt_exchange_tasks(rt_smp_current[cpuid], new_task);
		sched_release_global_lock(hard_cpu_id());
		if (rt_current->signal) {
			(*rt_current->signal)();
		}
		return;
	} else {
		sched_release_global_lock(cpuid);
	}
	return;
}


static irqreturn_t recover_jiffies(int irq, void *dev_id, struct pt_regs *regs)
{
	rt_global_cli();
	if (rt_times.tick_time >= rt_times.linux_time) {
		rt_times.linux_time += rt_times.linux_tick;
		rt_pend_linux_irq(TIMER_8254_IRQ);
	}
	rt_global_sti();
        BROADCAST_TO_LOCAL_TIMERS();
	return RTAI_LINUX_IRQ_HANDLED;
} 


int rt_is_hard_timer_running(void)
{
	return (rt_time_h > 0);
}


void rt_set_periodic_mode(void)
{
	stop_rt_timer();
	oneshot_timer = oneshot_running = 0;
}


void rt_set_oneshot_mode(void)
{
	stop_rt_timer();
	oneshot_timer = 1;
}


RTIME start_rt_timer(int period)
{
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	if (oneshot_timer) {
		rt_request_timer(rt_timer_handler, 0, TIMER_CHIP == "APIC");
		tuned.timers_tol[0] = rt_half_tick = (tuned.latency + 1)>>1;
		oneshot_running = shot_fired = 1;
	} else {
		rt_request_timer(rt_timer_handler, period > LATCH? LATCH: period, TIMER_CHIP == "APIC");
		tuned.timers_tol[0] = rt_half_tick = (rt_times.periodic_tick + 1)>>1;
	}
	set_timer_cpu(1 << hard_cpu_id());
	rt_time_h = rt_times.tick_time + rt_half_tick;
	rt_global_restore_flags(flags);
        FREE_LOCAL_TIMERS();
	rt_request_linux_irq(TIMER_8254_IRQ, recover_jiffies, "rtai_jif_chk", recover_jiffies);
	return period;
}


#ifdef __USE_APIC__

RTIME start_rt_timer_cpuid(int period, int cpuid)
{
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	if (oneshot_timer) {
		rt_request_timer_cpuid(rt_timer_handler, 0, cpuid);
		tuned.timers_tol[0] = rt_half_tick = (tuned.latency + 1)>>1;
		oneshot_running = shot_fired = 1;
	} else {
		rt_request_timer_cpuid(rt_timer_handler, period, cpuid);
		tuned.timers_tol[0] = rt_half_tick = (rt_times.periodic_tick + 1)>>1;
	}
	set_timer_cpu(1 << cpuid);
	rt_time_h = rt_times.tick_time + rt_half_tick;
        FREE_LOCAL_TIMERS();
	rt_global_restore_flags(flags);
	rt_request_linux_irq(TIMER_8254_IRQ, recover_jiffies, "rtai_jif_chk", recover_jiffies);
	return period;
}

#else

RTIME start_rt_timer_cpuid(int period, int cpuid)
{
	return start_rt_timer(period);
}

#endif


void start_rt_apic_timers(struct apic_timer_setup_data *setup_mode, unsigned int rcvr_jiffies_cpuid)
{
	int cpuid, period;

	period = 0;
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		period += setup_mode[cpuid].mode;
	}
	if (period == NR_RT_CPUS) {
		period = 2000000000;
		for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
			if (setup_mode[cpuid].count < period) {
				period = setup_mode[cpuid].count;
			}
		}
		start_rt_timer(nano2count(period));	
	} else {
		rt_set_oneshot_mode();
		start_rt_timer(0);	
	}
}


void stop_rt_timer(void)
{
	unsigned long flags;
	rt_free_linux_irq(TIMER_8254_IRQ, recover_jiffies);
	rt_free_timer();
	flags = rt_global_save_flags_and_cli();
	set_timer_cpu(cpu_present_map);
	oneshot_timer = oneshot_running = 0;
	rt_global_restore_flags(flags);
	rt_busy_sleep(10000000);
}


static inline RT_TASK *__whoami(void)
{
	RT_TASK *rt_current;
	unsigned long flags;
	hard_save_flags_and_cli(flags);
	rt_current = RT_CURRENT;
	hard_restore_flags(flags);
	return rt_current;
}


int rt_sched_type(void)
{
	return RT_SCHED_SMP;
}


void rt_preempt_always(int yes_no)
{
	preempt_always = yes_no ? 1 : 0;
}


void rt_preempt_always_cpuid(int yes_no, unsigned int cpuid)
{
	rt_preempt_always(yes_no);
}


RT_TRAP_HANDLER rt_set_task_trap_handler( RT_TASK *task, unsigned int vec, RT_TRAP_HANDLER handler)
{
	RT_TRAP_HANDLER old_handler;

	if (!task || (vec >= RTAI_NR_TRAPS)) {
		return (RT_TRAP_HANDLER) -EINVAL;
	}
	old_handler = task->task_trap_handler[vec];
	task->task_trap_handler[vec] = handler;
	return old_handler;
}


int rt_trap_handler(int vec, int signo, struct pt_regs *regs, void *dummy_data)
{
	RT_TASK *rt_current;

	if (!(rt_current = __whoami())) return 0;

	if (rt_current->task_trap_handler[vec]) {
		return rt_current->task_trap_handler[vec]( vec, 
							   signo,
							   regs, 
							   rt_current);
	}

	rt_printk("Default Trap Handler: vector %d: Suspend RT task %p\n", vec,rt_current);
	rt_task_suspend(rt_current);
	rt_task_delete(rt_current); // In case the suspend does not work ?

	return 1;
}

static int OneShot = ONE_SHOT;
MODULE_PARM(OneShot, "i");

static int Preempt_Always = PREEMPT_ALWAYS;
MODULE_PARM(Preempt_Always, "i");

static int LinuxFpu = LINUX_FPU;
MODULE_PARM(LinuxFpu, "i");

static int Latency = TIMER_LATENCY;
MODULE_PARM(Latency, "i");

static int SetupTimeTIMER = TIMER_SETUP_TIME;
MODULE_PARM(SetupTimeTIMER, "i");

static void frstk_srq_handler(void)
{
	while (frstk_srq.out != frstk_srq.in) {
		sched_free(frstk_srq.mp[frstk_srq.out]);
		frstk_srq.out = (frstk_srq.out + 1) & (MAX_FRESTK_SRQ - 1);
	}
}

static void init_sched_entries(void);

/* ++++++++++++++++++++++++++ TIME CONVERSIONS +++++++++++++++++++++++++++++ */

RTIME count2nano(RTIME counts)
{
	int sign;

	if (counts >= 0) {
		sign = 1;
	} else {
		sign = 0;
		counts = - counts;
	}
	counts = oneshot_timer ?
		 llimd(counts, 1000000000, tuned.cpu_freq):
		 llimd(counts, 1000000000, TIMER_FREQ);
	return sign ? counts : - counts;
}


RTIME nano2count(RTIME ns)
{
	int sign;

	if (ns >= 0) {
		sign = 1;
	} else {
		sign = 0;
		ns = - ns;
	}
	ns =  oneshot_timer ?
	      llimd(ns, tuned.cpu_freq, 1000000000) :
	      llimd(ns, TIMER_FREQ, 1000000000);
	return sign ? ns : - ns;
}


RTIME count2nano_cpuid(RTIME counts, unsigned int cpuid)
{
	return count2nano(counts);
}


RTIME nano2count_cpuid(RTIME ns, unsigned int cpuid)
{
	return nano2count(ns);
}

/* +++++++++++++++++++++++++++++++ TIMINGS ++++++++++++++++++++++++++++++++++ */

RTIME rt_get_time(void)
{
	return oneshot_timer ? rdtsc(): rt_times.tick_time;
}


RTIME rt_get_time_cpuid(unsigned int cpuid)
{
	return oneshot_timer ? rdtsc(): rt_times.tick_time;
}


RTIME rt_get_time_ns(void)
{
	return oneshot_timer ?
	       llimd(rdtsc(), 1000000000, tuned.cpu_freq) :
	       llimd(rt_times.tick_time, 1000000000, TIMER_FREQ);
}


RTIME rt_get_time_ns_cpuid(unsigned int cpuid)
{
	return oneshot_timer ?
	       llimd(rdtsc(), 1000000000, tuned.cpu_freq) :
	       llimd(rt_times.tick_time, 1000000000, TIMER_FREQ);
}


RTIME rt_get_cpu_time_ns(void)
{
	return llimd(rdtsc(), 1000000000, tuned.cpu_freq);
}


void rt_task_drift_wait_period(void)
{
	RT_TASK *rt_current;
	RTIME now;
	long flags;

	flags = rt_global_save_flags_and_cli();
	now = oneshot_timer ? rdtsc(): rt_times.tick_time;
	rt_current = RT_CURRENT;
	rt_current->resume_time += rt_current->period;
	if ((rt_current->resume_time + rt_half_tick) > now) {
		rt_current->state |= RT_SCHED_DELAYED;
		rt_schedule();
	} else {
		rt_current->resume_time = now;
	}
	rt_global_restore_flags(flags);
}

/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */

RT_TASK *rt_get_base_linux_task(RT_TASK **base_linux_tasks)
{
        base_linux_tasks[0] = &rt_base_linux_task;
        return rt_smp_linux_task;
}

RT_TASK *rt_alloc_dynamic_task(void)
{
#ifdef CONFIG_RTAI_MALLOC
        return rt_malloc(sizeof(RT_TASK)); // For VC's, proxies and C++ support.
#else
	return NULL;
#endif
}

RT_TASK **rt_register_watchdog(RT_TASK *wd, int cpuid)
{
    	RT_TASK *task;

	if (wdog_task[cpuid]) return (RT_TASK**) -EBUSY;
	task = &rt_base_linux_task;
	while ((task = task->next)) {
	    	if (task != wd && task->priority == RT_SCHED_HIGHEST_PRIORITY) {
		    	return (RT_TASK**) -EBUSY;
		}
	}
	wdog_task[cpuid] = wd;
	return rt_smp_current;
}

void rt_deregister_watchdog(RT_TASK *wd, int cpuid)
{
    	if (wdog_task[cpuid] != wd) return;
	wdog_task[cpuid] = NULL;
}

/* ++++++++++++++ SCHEDULER ENTRIES AND RELATED INITIALISATION ++++++++++++++ */

#if CONFIG_RTAI_NETRPC

#include <rtai_registry.h>

static struct rt_native_fun_entry rt_sched_entries[] = {
	{ { 0, rt_named_task_init },		    NAMED_TASK_INIT },  
	{ { 0, rt_named_task_init_cpuid },	    NAMED_TASK_INIT_CPUID },  
	{ { 0, rt_named_task_delete },	 	    NAMED_TASK_DELETE },  
	{ { 1, rt_task_yield },			    YIELD },  
	{ { 1, rt_task_suspend },		    SUSPEND },
	{ { 1, rt_task_resume },		    RESUME },
	{ { 1, rt_task_make_periodic },		    MAKE_PERIODIC },
	{ { 1, rt_task_wait_period },		    WAIT_PERIOD },
	{ { 1, rt_sleep },			    SLEEP },
	{ { 1, rt_sleep_until },		    SLEEP_UNTIL },
	{ { 0, start_rt_timer },		    START_TIMER },
	{ { 0, stop_rt_timer },			    STOP_TIMER },
	{ { 0, rt_get_time },			    GET_TIME },
	{ { 0, count2nano },			    COUNT2NANO },
	{ { 0, nano2count },			    NANO2COUNT },
	{ { 0, rt_busy_sleep },			    BUSY_SLEEP },
	{ { 0, rt_set_periodic_mode },		    SET_PERIODIC_MODE },
	{ { 0, rt_set_oneshot_mode },		    SET_ONESHOT_MODE },
	{ { 0, rt_task_signal_handler },	    SIGNAL_HANDLER  },
	{ { 0, rt_task_use_fpu },		    TASK_USE_FPU },
	{ { 0, rt_linux_use_fpu },		    LINUX_USE_FPU },
	{ { 0, rt_preempt_always },		    PREEMPT_ALWAYS_GEN },
	{ { 0, rt_get_time_ns },		    GET_TIME_NS },
	{ { 0, rt_get_cpu_time_ns },		    GET_CPU_TIME_NS },
	{ { 0, rt_set_runnable_on_cpus },	    SET_RUNNABLE_ON_CPUS },
	{ { 0, rt_set_runnable_on_cpuid },	    SET_RUNNABLE_ON_CPUID },
	{ { 0, rt_get_timer_cpu },		    GET_TIMER_CPU },
	{ { 0, start_rt_apic_timers },		    START_RT_APIC_TIMERS },
	{ { 0, rt_preempt_always_cpuid },	    PREEMPT_ALWAYS_CPUID },
	{ { 0, count2nano_cpuid },		    COUNT2NANO_CPUID },
	{ { 0, nano2count_cpuid },		    NANO2COUNT_CPUID },
	{ { 0, rt_get_time_cpuid },		    GET_TIME_CPUID },
	{ { 0, rt_get_time_ns_cpuid },		    GET_TIME_NS_CPUID },
	{ { 1, rt_task_make_periodic_relative_ns }, MAKE_PERIODIC_NS },
	{ { 0, rt_set_sched_policy },		    SET_SCHED_POLICY },
	{ { 1, rt_task_set_resume_end_times },	    SET_RESUME_END },
	{ { 0, rt_spv_RMS },			    SPV_RMS },
	{ { 0, rt_task_wakeup_sleeping },	    WAKEUP_SLEEPING },
	{ { 1, rt_change_prio },		    CHANGE_TASK_PRIO },
	{ { 0, rt_set_resume_time },  		    SET_RESUME_TIME },
	{ { 0, rt_set_period },			    SET_PERIOD },
	{ { 0, rt_is_hard_timer_running },	    HARD_TIMER_RUNNING },
	{ { 0, rt_get_adr },			    GET_ADR },
	{ { 0, rt_get_name },			    GET_NAME },
	{ { 0, 0 },			            000 }
};

static void nihil(void) { };
struct rt_fun_entry rt_fun_lxrt[MAX_LXRT_FUN];

void reset_rt_fun_entries(struct rt_native_fun_entry *entry)
{
	while (entry->fun.fun) {
		if (entry->index >= MAX_LXRT_FUN) {
			rt_printk("*** RESET ENTRY %d FOR USER SPACE CALLS EXCEEDS ALLOWD TABLE SIZE %d, NOT USED ***\n", entry->index, MAX_LXRT_FUN);
		} else {
			rt_fun_lxrt[entry->index] = (struct rt_fun_entry){ 1, nihil };
		}
		entry++;
        }
}

int set_rt_fun_entries(struct rt_native_fun_entry *entry)
{
	int error;
	error = 0;
	while (entry->fun.fun) {
		if (rt_fun_lxrt[entry->index].fun != nihil) {
			rt_printk("*** SUSPICIOUS ENTRY ASSIGNEMENT FOR USER SPACE CALL AT %d, DUPLICATED INDEX OR REPEATED INITIALIZATION ***\n", entry->index);
			error = -1;
		} else if (entry->index >= MAX_LXRT_FUN) {
			rt_printk("*** ASSIGNEMENT ENTRY %d FOR USER SPACE CALLS EXCEEDS ALLOWED TABLE SIZE %d, NOT USED ***\n", entry->index, MAX_LXRT_FUN);
			error = -1;
		} else {
			rt_fun_lxrt[entry->index] = entry->fun;
		}
		entry++;
        }
	if (error) {
		reset_rt_fun_entries(entry);
	}
	return 0;
}

static void init_sched_entries(void)
{
	int i;
	for (i = 0; i < MAX_LXRT_FUN; i++) {
		rt_fun_lxrt[i] = (struct rt_fun_entry) { 1, nihil };
	}
	set_rt_fun_entries(rt_sched_entries);
}

#else

void reset_rt_fun_entries(struct rt_native_fun_entry *entry)
{
}

int set_rt_fun_entries(struct rt_native_fun_entry *entry)
{
	return 0;
}

static void init_sched_entries(void)
{
}

#endif

int set_rt_fun_ext_index(struct rt_fun_entry *fun, int idx)
{
	return -EACCES;
}

void reset_rt_fun_ext_index( struct rt_fun_entry *fun, int idx)
{
	return;
}

/* ++++++++++++++++++++++++++ SCHEDULER PROC FILE +++++++++++++++++++++++++++ */

#ifdef CONFIG_PROC_FS
/* ----------------------< proc filesystem section >----------------------*/

static int rtai_read_sched(char *page, char **start, off_t off, int count,
                           int *eof, void *data)
{
	PROC_PRINT_VARS;
	unsigned int i = 1;
	unsigned long t;
	RT_TASK *task;

	task = &rt_base_linux_task;
	PROC_PRINT("\nRTAI SMP Real Time Task Scheduler.\n\n");
	PROC_PRINT("    Calibrated CPU Frequency: %lu Hz\n", tuned.cpu_freq);
	PROC_PRINT("    Calibrated 8254 interrupt to scheduler latency: %d ns\n", imuldiv(tuned.latency - tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));
	PROC_PRINT("    Calibrated one shot setup time: %d ns\n\n",
                  imuldiv(tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));
        PROC_PRINT("Number of RT CPUs in system: %d\n\n", NR_RT_CPUS);

        PROC_PRINT("Priority  Period(ns)  FPU  Sig  State  CPUmap  Task  RT_TASK *  TIME\n" );
        PROC_PRINT("---------------------------------------------------------------------\n" );
/*
* Display all the active RT tasks and their state.
*
* Note: As a temporary hack the tasks are given an id which is
*       the order they appear in the task list, needs fixing!
*/
	while ((task = task->next)) {
/*
* The display for the task period is set to an integer (%d) as 64 bit
* numbers are not currently handled correctly by the kernel routines.
* Hence the period display will be wrong for time periods > ~4 secs.
*/
			if ((!task->lnxtsk || task->is_hard) && task->exectime[1]) {
				t = 1000UL*(unsigned long)llimd(task->exectime[0], 10, tuned.cpu_freq)/(unsigned long)llimd(rdtsc() - task->exectime[1], 10, tuned.cpu_freq);
			} else {
			       t = 0;
			}
                PROC_PRINT("%-9d %-11lu %-4s %-4s 0x%-4x %-7lx %-4d  %p    %-lu\n",
                               task->priority,
                               (unsigned long)count2nano(task->period),
                               task->uses_fpu ? "Yes" : "No",
                               task->signal ? "Yes" : "No",
                               task->state,
                               task->runnable_on_cpus,
                               i, task, t);
		i++;
	}

	PROC_PRINT("TIMED\n");
	task = &rt_base_linux_task;
	while ((task = task->tnext) != &rt_base_linux_task) {
                PROC_PRINT("> %p ", task);
	}
	PROC_PRINT("\nREADY\n");
	task = &rt_base_linux_task;
	while ((task = task->rnext) != &rt_base_linux_task) {
                PROC_PRINT("> %p ", task);
	}
	PROC_PRINT("\n");

	PROC_PRINT_DONE;

}  /* End function - rtai_read_sched */


static int rtai_proc_sched_register(void) 
{
        struct proc_dir_entry *proc_sched_ent;


        proc_sched_ent = create_proc_entry("scheduler", S_IFREG|S_IRUGO|S_IWUSR, rtai_proc_root);
        if (!proc_sched_ent) {
                printk("Unable to initialize /proc/rtai/scheduler\n");
                return(-1);
        }
        proc_sched_ent->read_proc = rtai_read_sched;
        return(0);
}  /* End function - rtai_proc_sched_register */


static void rtai_proc_sched_unregister(void) 
{
        remove_proc_entry("scheduler", rtai_proc_root);
}  /* End function - rtai_proc_sched_unregister */

/* ------------------< end of proc filesystem section >------------------*/
#endif /* CONFIG_PROC_FS */

int __rtai_smp_init(void)
{
	int cpuid;
	sched_mem_init();
	init_sched_entries();
	for (cpuid = 0; cpuid < NR_RT_CPUS; cpuid++) {
		rt_linux_task.stack = 0;
		rt_linux_task.uses_fpu = LinuxFpu ? 1 : 0;
		rt_linux_task.policy = 0;
		rt_linux_task.magic = 0;
		rt_linux_task.runnable_on_cpus = 1 << cpuid;
		rt_linux_task.running = rt_linux_task.state = RT_SCHED_READY;
		rt_linux_task.msg_queue.prev = &(rt_linux_task.msg_queue);
		rt_linux_task.msg_queue.next = &(rt_linux_task.msg_queue);
		rt_linux_task.msg_queue.task = &rt_linux_task;
		rt_linux_task.msg = 0;
		rt_linux_task.ret_queue.prev = &(rt_linux_task.ret_queue);
		rt_linux_task.ret_queue.next = &(rt_linux_task.ret_queue);
		rt_linux_task.ret_queue.task = NOTHING;
		rt_linux_task.priority = RT_SCHED_LINUX_PRIORITY;
		rt_linux_task.base_priority = RT_SCHED_LINUX_PRIORITY;
		rt_linux_task.signal = 0;
		rt_linux_task.prev = &rt_base_linux_task;
		rt_linux_task.resume_time = RT_TIME_END;
		rt_linux_task.tprev = rt_linux_task.tnext =
		rt_linux_task.rprev = rt_linux_task.rnext = &rt_linux_task;
		rt_linux_task.next = 0;
		rt_smp_current[cpuid] = &rt_linux_task;
	}
	set_timer_cpu(cpu_present_map);
	tuned.latency = imuldiv(Latency, tuned.cpu_freq, 1000000000);
	tuned.setup_time_TIMER_CPUNIT = imuldiv( SetupTimeTIMER, 
						 tuned.cpu_freq, 
						 1000000000);
	tuned.setup_time_TIMER_UNIT   = imuldiv( SetupTimeTIMER, 
						 TIMER_FREQ, 
						 1000000000);
	tuned.timers_tol[0] = 0;
	oneshot_timer = OneShot ? 1 : 0;
	oneshot_running = 0;
	preempt_always = Preempt_Always ? 1 : 0;
#ifdef CONFIG_PROC_FS
	rtai_proc_sched_register();
#endif
	rt_mount_rtai();
// 0x7dd763ad == nam2num("MEMSRQ").
	if ((frstk_srq.srq = rt_request_srq(0x7dd763ad, frstk_srq_handler, 0)) < 0) {
		printk("MEM SRQ: no sysrq available.\n");
		return frstk_srq.srq;
	}
	frstk_srq.in = frstk_srq.out = 0;
	rt_request_sched_ipi();
	RT_SET_RTAI_TRAP_HANDLER(rt_trap_handler);
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	rt_set_ihook(&rtai_handle_isched_lock);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
#ifdef CONFIG_RTAI_ADEOS
	sched_virq = adeos_alloc_irq();

	adeos_virtualize_irq_from(&rtai_domain,
				  sched_virq,
				  (void (*)(unsigned))&rt_schedule,
				  NULL,
				  IPIPE_HANDLE_MASK);
#endif /* CONFIG_RTAI_ADEOS */

	printk(KERN_INFO "RTAI[sched_smp]: loaded.\n");
	printk(KERN_INFO "RTAI[sched_smp]: fpu=%s, timer=%s (%s).\n",
	       LinuxFpu ? "yes" : "no",
	       oneshot_timer ? "oneshot" : "periodic",
#ifdef __USE_APIC__
	       "APIC"
#else /* __USE_APIC__ */
	       "8254-PIT"
#endif /* __USE_APIC__ */
	       );
	printk(KERN_INFO "RTAI[sched_smp]: standard tick=%d hz, CPU freq=%lu hz.\n",
	       HZ,
	       (unsigned long)tuned.cpu_freq);
	printk(KERN_INFO "RTAI[sched_smp]: timer setup=%d ns, resched latency=%d ns.\n",
	       imuldiv(tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq),
	       imuldiv(tuned.latency - tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));

	return rtai_init_features(); /* see rtai_schedcore.h */
}


extern void krtai_objects_release(void);

void __rtai_smp_exit(void)
{
	RT_SET_RTAI_TRAP_HANDLER(NULL);
	stop_rt_timer();
	while (rt_base_linux_task.next) {
		rt_task_delete(rt_base_linux_task.next);
	}
	krtai_objects_release();

#ifdef CONFIG_RTAI_ADEOS
	if (sched_virq)
	    {
	    adeos_virtualize_irq_from(&rtai_domain,sched_virq,NULL,NULL,0);
	    adeos_free_irq(sched_virq);
	    }
#endif /* CONFIG_RTAI_ADEOS */

	rtai_cleanup_features();
#ifdef CONFIG_PROC_FS
        rtai_proc_sched_unregister();
#endif
	while (frstk_srq.out != frstk_srq.in);
	if (rt_free_srq(frstk_srq.srq) < 0) {
		printk("MEM SRQ: frstk_srq %d illegal or already free.\n", frstk_srq.srq);
	}
	rt_free_sched_ipi();
	sched_mem_end();
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	rt_set_ihook(NULL);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
	rt_umount_rtai();
	printk(KERN_INFO "RTAI[sched_smp]: unloaded.\n");
}

module_init(__rtai_smp_init);
module_exit(__rtai_smp_exit);

/* +++++++++++++++++++++++++++++ SCHEDULER END ++++++++++++++++++++++++++++ */
