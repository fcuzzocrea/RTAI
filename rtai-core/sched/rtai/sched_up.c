/**
 * @file
 * Scheduling function for uni-processor.
 * @author Paolo Mantegazza
 *
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 1999-2003 Paolo Mantegazza
 * <mantegazza@aero.polimi.it> 
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
 *
 * @ingroup tasks timer
 */

/*
ACKNOWLEDGMENTS: 
- Steve Papacharalambous (stevep@zentropix.com) has contributed a very 
  informative proc filesystem procedure.
- Stuart Hughes (sehughes@zentropix.com) has helped in porting this 
  module to 2.4.xx.
- Stefano Picerno (stefanopp@libero.it) for suggesting a simple fix to 
  distinguish a timeout from an abnormal retrun in timed sem waits.
- Geoffrey Martin (gmartin@altersys.com) for a fix to functions with timeouts.
*/

/**
 * @defgroup sched RTAI schedulers modules
 *
 * RTAI schedulers.
 *
 * The functions described here are provided for :
 * - uniprocessor (UP) scheduler;
 * - symmetric multi processors (SMP) scheduler;
 * - multi uni processors (MUP) scheduler.
 * .
 *
 * For more details,
 * see the @ref sched_overview "the overview of RTAI schedulers".
 */

/**
 * @ingroup sched
 * @defgroup tasks Task functions
 */

/**
 * @ingroup sched
 * @defgroup timer Timer functions
 */

#ifdef CONFIG_RTAI_MAINTAINER_PMA
#define ALLOW_RR        1
#define ONE_SHOT 	0
#define PREEMPT_ALWAYS	0
#define LINUX_FPU 	1
#else /* STANDARD SETTINGS */
#define ALLOW_RR        1
#define ONE_SHOT 	0
#define PREEMPT_ALWAYS	0
#define LINUX_FPU 	1
#endif

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

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
#include <rtai_trace.h>
#include <rtai_schedcore.h>

#undef DECLARE_RT_CURRENT

#undef ASSIGN_RT_CURRENT

#undef RT_CURRENT

MODULE_LICENSE("GPL");

/* +++++++++++++++++ WHAT MUST BE AVAILABLE EVERYWHERE ++++++++++++++++++++++ */

RT_TASK rt_smp_linux_task[1];

RT_TASK *rt_smp_current[1];

RTIME rt_smp_time_h[1];

int rt_smp_oneshot_timer[1];

struct klist_t wake_up_srq;

/* +++++++++++++++ END OF WHAT MUST BE AVAILABLE EVERYWHERE +++++++++++++++++ */

#define TIMER_FREQ FREQ_8254

#ifdef CONFIG_PROC_FS
static int rtai_proc_sched_register(void);
static void rtai_proc_sched_unregister(void);
#endif

#define rt_current (rt_smp_current[0])

static int sched_rqsted;

static RT_TASK *fpu_task;

#define rt_linux_task (rt_smp_linux_task[0])

DEFINE_LINUX_CR0

#undef rt_time_h
#define rt_time_h (rt_smp_time_h[0])

static int rt_half_tick;

#undef oneshot_timer
#define oneshot_timer (rt_smp_oneshot_timer[0])

static int oneshot_running;

static int shot_fired;

static int preempt_always;

static RT_TASK *wdog_task;

static int rt_next_tid = 1;       /* Next task ID */

#ifdef CONFIG_RTAI_ADEOS
static unsigned sched_virq;
#endif /* CONFIG_RTAI_ADEOS */

#define MAX_FRESTK_SRQ  64
static struct { int srq, in, out; void *mp[MAX_FRESTK_SRQ]; } frstk_srq;

/* ++++++++++++++++++++++++++++++++ TASKS ++++++++++++++++++++++++++++++++++ */

#define TASK_TO_SCHEDULE() \
	do { prio = (new_task = rt_linux_task.rnext)->priority; } while(0);


asmlinkage static void rt_startup(void(*rt_thread)(int), int data)
{
	hard_sti();
	rt_current->exectime[1] = rdtsc();
	rt_thread(data);
	rt_task_delete(rt_current);
}

/**
 * @ingroup tasks
 * @anchor rt_task_init
 * Creates a new real time task.
 *
 * The newly created real time task is initially in a suspend
 * state. It can be made active by calling: rt_task_make_periodic,
 * rt_task_make_periodic_relative_ns, rt_task_resume.
 *
 * When used with the MUP scheduler rt_task_init automatically selects
 * which CPU the task will run on, while with the SMP scheduler the
 * task defaults to using any of the available CPUs. This assignment
 * may be changed by calling rt_set_runnable_on_cpus.
 *
 * @param task is a pointer to an RT_TASK type structure whose space
 *	  must be provided by the application. It must be kept during
 *	  the whole lifetime of the real time task. 
 *
 * @param rt_thread is the entry point of the task function.
 *
 * @param data The parent task can pass a single integer value data to
 *	  the new task being created. Recall that an appropriately
 *	  type casting allows data to be a pointer to whatever data
 *	  structure one would like to pass to the task, so you can
 *	  indirectly pass whatever you want to the task.
 * 
 * @param stack_size is the size of the stack to be used by the new
 *		     task. In sizing it, recall to make room for any
 *		     real time interrupt handler, as real time
 *		     interrupts run on the stack of the task they
 *		     interrupt. So try to avoid being too sparing.
 *
 * @param priority is the priority to be given to the task. The
 *	  highest priority is 0, while the lowest is
 *	  RT_SCHED_LOWEST_PRIORITY. 
 *
 * @param uses_fpu is a flag. A nonzero value indicates that the task
 *		   will use the floating point unit.
 *
 * @param signal is a function that is called, within the task
 *	  environment and with interrupts disabled, when the task
 * 	  becomes the current running task after a context
 *	  switch. Note however that signal is not called at the very
 *	  first scheduling of the task. Such a function can be
 *	  assigned and/or changed dynamically whenever needed (see
 *	  function rt_task_signal_handler.)
 *
 * @return 0 on success. A negative value on failure as described
 * below: 
 * - @b EINVAL: task structure pointed by task is already in use;
 * - @b ENOMEM: stack_size bytes could not be allocated for the stack.
 *
 * See also: @ref rt_task_init_cpuid().
 */
int rt_task_init(RT_TASK *task, void (*rt_thread)(int), int data,
			int stack_size, int priority, int uses_fpu,
			void(*signal)(void))
{
	int *st, i;
	unsigned long flags;

	if (task->magic == RT_TASK_MAGIC || priority < 0) {
		return -EINVAL;
	}
// If the task struct is unaligned, we'll get problems later
	if ((unsigned long)task & 0xf){
		return -EFAULT;
	}
#ifndef CONFIG_RTAI_FPU_SUPPORT
	if (uses_fpu) {
		return -EINVAL;
	}
#endif
	if (!(st = (int *)sched_malloc(stack_size))) {
		return -ENOMEM;
	}
	if (wdog_task && wdog_task != task && priority == RT_SCHED_HIGHEST_PRIORITY) {
	    	rt_printk("Highest priority reserved for RTAI watchdog\n");
	    	return -EBUSY;
	}

	memset(task, 0, sizeof(*task));

	task->bstack = task->stack = (int *)(((unsigned long)st + stack_size - 0x10) & ~0xF);
        task->stack[0] = 0;
	task->uses_fpu = uses_fpu ? 1 : 0;
	*(task->stack_bottom = st) = 0;
	task->runnable_on_cpus = 1;
        task->lnxtsk = 0;
	task->magic = RT_TASK_MAGIC; 
	task->policy = 0;
	task->is_hard = 1;
	task->suspdepth = 1;
	task->state = (RT_SCHED_SUSPENDED | RT_SCHED_READY);
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
	task->tid = rt_next_tid++;
	task->exectime[0] = 0;
	task->system_data_ptr = 0;

	TRACE_RTAI_TASK(TRACE_RTAI_EV_TASK_INIT, task->tid,
			(uint32_t)rt_thread, priority);
	init_arch_stack();

	hard_save_flags_and_cli(flags);
	init_fp_env();
	rt_linux_task.prev->next = task;
	task->prev = rt_linux_task.prev;
	task->next = 0;
	rt_linux_task.prev = task;
	hard_restore_flags(flags);

	return 0;
}


/**
 * @ingroup tasks
 * @anchor rt_task_init_cpuid
 * Creates a new real time task and assigns it to a single specific
 * CPU. 
 *
 * The newly created real time task is initially in a suspend
 * state. It can be made active by calling: rt_task_make_periodic,
 * rt_task_make_periodic_relative_ns, rt_task_resume.
 *
 *
 * When used with the MUP scheduler rt_task_init automatically selects
 * which CPU the task will run on, while with the SMP scheduler the
 * task defaults to using any of the available CPUs. This assignment
 * may be changed by calling rt_set_runnable_on_cpus or
 * rt_set_runnable_on_cpuid. If cpuid is invalid rt_task_init_cpuid
 * falls back to automatic CPU selection.
 *
 * Whatever scheduler is used on multiprocessor systems
 * rt_task_init_cpuid allows to create a task and assign it to a
 * single specific CPU cpuid from its very beginning, without any need
 * to call rt_set_runnable_on_cpuid later on.
 *
 * @param task is a pointer to an RT_TASK type structure whose space
 *	  must be provided by the application. It must be kept during
 *	  the whole lifetime of the real time task.  
 *
 * @param rt_thread is the entry point of the task function.
 *
 * @param data The parent task can pass a single integer value data to
 *	       the new task being created. Recall that an
 *	       appropriately type casting allows data to be a pointer
 *	       to whatever data structure one would like to pass to
 *	       the task, so you can indirectly pass whatever you want
 *	       to the task.
 *
 * @param stack_size is the size of the stack to be used by the new
 *	  task. In sizing it recall to make room for any real time
 *	  interrupt handler, as real time interrupts run on the stack
 *	  of the task they interrupt. So try to avoid being too
 *	  sparing.
 *
 * @param priority is the priority to be given to the task. The
 *	  highest priority is 0, while the lowest is
 *	  RT_SCHED_LOWEST_PRIORITY.
 *
 * @param uses_fpu is a flag. A nonzero value indicates that the task
 *	  will use the floating point unit.
 *
 * @param signal is a function that is called, within the task
 *	  environment and with interrupts disabled, when the task
 *	  becomes the current running task after a context
 *	  switch. Note however that signal is not called at the very
 *	  first scheduling of the task. Such a function can be
 *	  assigned and/or changed dynamically whenever needed (see
 *	  function rt_task_signal_handler.)
 *
 * @param cpuid FIXME
 *
 * @return 0 on success. A negative value on failure as described
 * below: 
 * - @b EINVAL: task structure pointed by task is already in use;
 * - @b ENOMEM: stack_size bytes could not be allocated for the stack.
 *
 * See also: @ref rt_task_init().
 */
int rt_task_init_cpuid(RT_TASK *task, void (*rt_thread)(int), int data,
			int stack_size, int priority, int uses_fpu,
			void(*signal)(void), unsigned int cpuid)
{
	return rt_task_init(task, rt_thread, data, stack_size, priority, 
							uses_fpu, signal);
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


/**
 * @ingroup tasks
 * @anchor rt_set_runnable_on_cpus
 * @brief Assign CPUs to a task.
 *
 * rt_set_runnable_on_cpus selects one or more CPUs which are allowed
 * to run task @e task. 
 * rt_set_runnable_on_cpus behaves differently for MUP and SMP
 * schedulers. Under the SMP scheduler bit<n> of cpu_mask enables the
 * task to run on CPU<n>. Under the MUP scheduler it selects the CPU
 * with less running tasks among those allowed by cpu_mask.
 * Recall that with MUP a task must be bounded to run on a single CPU.
 * If no CPU, as selected by cpu_mask or cpuid, is available, both
 * functions choose a possible CPU automatically, following the same
 * rule as above. 
 *
 * @note This call has no effect on UniProcessor (UP) systems.
 *
 * See also: @ref rt_set_runnable_on_cpuid().
 */
void rt_set_runnable_on_cpus(RT_TASK *task, unsigned long runnable_on_cpus) { }


/**
 * @ingroup tasks
 * @anchor rt_set_runnable_on_cpuid
 * @brief Assign CPUs to a task.
 *
 * rt_set_runnable_on_cpuid select one or more CPUs which are allowed
 * to run task @e task. 
 *
 * rt_set_runnable_on_cpuid assigns a task to a single specific CPU.
 * If no CPU, as selected by cpu_mask or cpuid, is available, both
 * functions choose a possible CPU automatically, following the same
 * rule as above. 
 *
 * @note This call has no effect on UniProcessor (UP) systems.
 *
 * See also: @ref rt_set_runnable_on_cpus().
 */
void rt_set_runnable_on_cpuid(RT_TASK *task, unsigned int cpuid) { }


int rt_check_current_stack(void)
{
	char *sp;

	if (rt_current != &rt_linux_task) {
		sp = get_stack_pointer();
		return (sp - (char *)(rt_current->stack_bottom));
	} else {
		return -0x7FFFFFFF;
	}
}


#if ALLOW_RR
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
	}

#define RR_SPREMP() \
	if (new_task->policy > 0) { \
		preempt = 1; \
		if (new_task->yield_time < intr_time) { \
			intr_time = new_task->yield_time; \
		} \
	} else { \
		preempt = 0; \
	}

#define RR_TPREMP() \
	if (new_task->policy > 0) { \
		preempt = 1; \
		if (new_task->yield_time < rt_times.intr_time) { \
			rt_times.intr_time = new_task->yield_time; \
		} \
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

#define ANTICIPATE

#define EXECTIME
#ifdef EXECTIME
RTIME switch_time;
#define KEXECTIME() \
do { \
	RTIME now; \
	now = rdtsc(); \
	if (!rt_current->lnxtsk) { \
		rt_current->exectime[0] += (now - switch_time); \
	} \
	switch_time = now; \
} while (0)
#else
#define KEXECTIME()
#endif

void rt_schedule(void)
{
	RT_TASK *task, *new_task;
	RTIME intr_time, now;
	int prio, delay, preempt;

#ifdef CONFIG_RTAI_ADEOS
	if (adp_current != &rtai_domain)
	    {
	    adeos_trigger_irq(sched_virq);
	    return;
	    }
#endif /* CONFIG_RTAI_ADEOS */

	sched_rqsted = 1;
	prio = RT_SCHED_LINUX_PRIORITY;
	task = new_task = &rt_linux_task;
	RR_YIELD();
	if (oneshot_running) {
#ifdef ANTICIPATE
		rt_time_h = rdtsc() + (RTIME)rt_half_tick;
		wake_up_timed_tasks(0);
#endif
		TASK_TO_SCHEDULE();
		RR_SETYT();

		intr_time = shot_fired ? rt_times.intr_time :
			    rt_times.intr_time + (RTIME)rt_times.linux_tick;
		RR_SPREMP();
		task = &rt_linux_task;
		while ((task = task->tnext) != &rt_linux_task) {
			if (task->priority <= prio && task->resume_time < intr_time) {
				intr_time = task->resume_time;
				preempt = 1;
				break;
			}
		}
		if (preempt || (!shot_fired && prio == RT_SCHED_LINUX_PRIORITY)) {
			shot_fired = 1;
			if (preempt) {
				rt_times.intr_time = intr_time;
			}
			delay = (int)(rt_times.intr_time - (now = rdtsc())) - tuned.latency;
			if (delay >= tuned.setup_time_TIMER_CPUNIT) {
				delay = imuldiv(delay, TIMER_FREQ, tuned.cpu_freq);
			} else {
				delay = tuned.setup_time_TIMER_UNIT;
				rt_times.intr_time = now + (RTIME)tuned.setup_time_TIMER_CPUNIT;
			}
			rt_set_timer_delay(delay);
		}
	} else {
		TASK_TO_SCHEDULE();
		RR_SETYT();
	}

	if (new_task != rt_current) {
		TRACE_RTAI_SCHED_CHANGE(rt_current->tid, new_task->tid, rt_current->state);

		if (rt_current == &rt_linux_task) {
			rt_switch_to_real_time(0);
			save_cr0_and_clts(linux_cr0);
		}
		if (new_task->uses_fpu) {
			enable_fpu();
			if (new_task != fpu_task) {
				save_fpenv(fpu_task->fpu_reg);
				fpu_task = new_task;
				restore_fpenv(fpu_task->fpu_reg);
			}
		}

		KEXECTIME();

		if (new_task == &rt_linux_task) {
			restore_cr0(linux_cr0);
			rt_switch_to_linux(0);
			/* From now on, the Linux stage is re-enabled,
			   but not sync'ed until we have actually
			   switched to the Linux task, so that we
			   don't end up running the Linux IRQ handlers
			   on behalf of a non-Linux stack
			   context... */
		}

		rt_switch_to(new_task);

		if (rt_current->signal) {
			(*rt_current->signal)();
		}
	}
}

void rt_spv_RMS(int cpuid)
{
	RT_TASK *task;
	int prio;
	prio = 0;
	task = &rt_linux_task;
	while ((task = task->next)) {
		RT_TASK *task, *htask;
		RTIME period;
		htask = 0;
		task = &rt_linux_task;
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
ret:	task = &rt_linux_task;
	while ((task = task->next)) {
		if (task->priority < 0) {
			task->priority = task->base_priority;
		}
	}
	return;
}


/**
 * @ingroup tasks
 * @anchor rt_sched_lock
 * @brief Lock the scheduling of tasks.
 *
 * rt_sched_lock, lock on the CPU on which they are called, any
 * scheduler activity, thus preventing a higher priority task to
 * preempt a lower priority one. They can be nested, provided unlocks
 * are paired to locks in reversed order. It can be used for
 * synchronization access to data among tasks. Note however that under
 * MP the lock is active only for the CPU on which it has been issued,
 * so it cannot be used to avoid races with tasks that can run on any
 * other available CPU. 
 * Interrupts are not affected by such calls. Any task that needs
 * rescheduling while a scheduler lock is in placewill be only at the
 * issuing of the last unlock 
 * 
 * @note To be used only with RTAI24.x.xx.
 *
 * See also: @ref rt_sched_unlock().
 */
void rt_sched_lock(void)
{
	unsigned long flags;

	hard_save_flags_and_cli(flags);
	if (rt_current->priority >= 0) {
		rt_current->sched_lock_priority = rt_current->priority;
		sched_rqsted = rt_current->priority = -1;
	} else {
		rt_current->priority--;
	}
	hard_restore_flags(flags);
}


/**
 * @ingroup tasks
 * @anchor rt_sched_unlock
 * @brief Unlock the scheduling of tasks.
 *
 * rt_sched_unlock, unlock on the CPU on which they are called, any
 * scheduler activity, thus preventing a higher priority task to
 * preempt a lower priority one. They can be nested, provided unlocks
 * are paired to locks in reversed order. It can be used for
 * synchronization access to data among tasks. Note however that under
 * MP the lock is active only for the CPU on which it has been issued,
 * so it cannot be used to avoid races with tasks that can run on any
 * other available CPU. 
 * Interrupts are not affected by such calls. Any task that needs
 * rescheduling while a scheduler lock is in placewill be only at the
 * issuing of the last unlock 
 * 
 * @note To be used only with RTAI24.x.xx.
 *
 * See also: @ref rt_sched_unlock().
 */
void rt_sched_unlock(void)
{
	unsigned long flags;

	hard_save_flags_and_cli(flags);
	if (rt_current->priority < 0 && !(++rt_current->priority)) {
		if ((rt_current->priority = rt_current->sched_lock_priority) != RT_SCHED_LINUX_PRIORITY) {
			(rt_current->rprev)->rnext = rt_current->rnext;
			(rt_current->rnext)->rprev = rt_current->rprev;
			enq_ready_task(rt_current);
		}
		if (sched_rqsted > 0) {
			rt_schedule();
		}
	}
	hard_restore_flags(flags);
}


/**
 * @ingroup tasks
 * @anchor rt_task_delete
 * Delete a real time task.
 *
 * rt_task_delete deletes a real time task previously created by
 * @ref rt_task_init() or @ref rt_task_init_cpuid().
 *
 * @param task is the pointer to the task structure. If task task was
 *	  waiting on a queue, i.e. semaphore, mailbox, etc, it is
 *	  removed from such a queue and messaging tasks pending on its
 *	  message queue are unblocked with an error return.
 * 
 * @return 0 on success. A negative value on failure as described
 * below: 
 * - @b EINVAL: task does not refer to a valid task.
 */
int rt_task_delete(RT_TASK *task)
{
	unsigned long flags;
	QUEUE *q;

	if (task->magic != RT_TASK_MAGIC || task->priority == RT_SCHED_LINUX_PRIORITY) {
		return -EINVAL;
	}

	TRACE_RTAI_TASK(TRACE_RTAI_EV_TASK_DELETE, task->tid, 0, 0);

	hard_save_flags_and_cli(flags);
	if (!(task->owndres & SEMHLF) || task == rt_current || rt_current->priority == RT_SCHED_LINUX_PRIORITY) {
		call_exit_handlers(task);
		rem_timed_task(task);
		if (task->blocked_on) {
			(task->queue.prev)->next = task->queue.next;
			(task->queue.next)->prev = task->queue.prev;
			if (task->state & RT_SCHED_SEMAPHORE) {
				if (!((SEM *)(task->blocked_on))->type) {
					((SEM *)(task->blocked_on))->count++;
				} else {
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
		if (!((task->prev)->next = task->next)) {
			rt_linux_task.prev = task->prev;
		} else {
			(task->next)->prev = task->prev;
		}
		if (fpu_task == task) {
			/* XXX Don't we lose the linux FPU context here? */
			fpu_task = &rt_linux_task;
		}
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
	hard_restore_flags(flags);
	return 0;
}

static void rt_timer_handler(void)
{
	RT_TASK *task, *new_task;
	RTIME now;
	int prio, delay, preempt;

	TRACE_RTAI_TIMER(TRACE_RTAI_EV_TIMER_HANDLE_EXPIRY, 0, 0);

	sched_rqsted = 1;
	DO_TIMER_PROPER_OP();
	prio = RT_SCHED_LINUX_PRIORITY;
	task = new_task = &rt_linux_task;
	rt_times.tick_time = oneshot_timer ? rdtsc() : rt_times.intr_time;
	rt_time_h = rt_times.tick_time + (RTIME)rt_half_tick;
	if (rt_times.tick_time >= rt_times.linux_time) {
		rt_times.linux_time += (RTIME)rt_times.linux_tick;
		rt_pend_linux_irq(TIMER_8254_IRQ);
	}
	wake_up_timed_tasks(0);
	RR_YIELD();
	TASK_TO_SCHEDULE();
	RR_SETYT();

	if (oneshot_timer) {
		rt_times.intr_time = rt_times.linux_time > rt_times.tick_time ?
		rt_times.linux_time : rt_times.tick_time + (RTIME)rt_times.linux_tick;
		RR_TPREMP();

		task = &rt_linux_task;
		while ((task = task->tnext) != &rt_linux_task) {
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
				rt_times.intr_time = now + (RTIME)tuned.setup_time_TIMER_CPUNIT;
			}
			rt_set_timer_delay(delay);
		}
	} else {
		rt_times.intr_time += (RTIME)rt_times.periodic_tick;
		rt_set_timer_delay(0);
	}

	if (new_task != rt_current) {
		if (rt_current == &rt_linux_task) {
			rt_switch_to_real_time(0);
			save_cr0_and_clts(linux_cr0);
		}
		if (new_task->uses_fpu) {
			enable_fpu();
			if (new_task != fpu_task) {
				save_fpenv(fpu_task->fpu_reg);
				fpu_task = new_task;
				restore_fpenv(fpu_task->fpu_reg);
			}
		}
		TRACE_RTAI_SCHED_CHANGE(rt_current->tid, new_task->tid, rt_current->state);

		KEXECTIME();
		rt_switch_to(new_task);
		if (rt_current->signal) {
			(*rt_current->signal)();
		}
	}
}


static irqreturn_t recover_jiffies(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned long flags;
	hard_save_flags_and_cli(flags);
	if (rt_times.tick_time >= rt_times.linux_time) {
		rt_times.linux_time += rt_times.linux_tick;
		rt_pend_linux_irq(TIMER_8254_IRQ);
	}
	hard_restore_flags(flags);
	return RTAI_LINUX_IRQ_HANDLED;
} 


int rt_is_hard_timer_running(void)
{
	return (rt_time_h > 0);
}


/**
 * @ingroup timer
 * @anchor rt_set_periodic_mode
 * @brief Set timer mode.
 *
 * rt_set_periodic_mode sets the periodic mode for the timer. It
 * consists of a fixed frequency timing of the tasks in multiple of
 * the period set with a call to @ref start_rt_timer(). The resolution
 * is that of the 8254 (1193180 Hz) on a UP machine, or if the 8254
 * based SMP scheduler is being used. For the SMP scheduler timed by
 * the local APIC timer and for the MUP scheduler the timer resolution
 * is that of the local APIC timer frequency, generally the bus
 * frequency divided 16. Any timing request not being an integer
 * multiple of the set timer period is satisfied at the closest period
 * tick. It is the default mode when no call is made to set the
 * oneshot mode. 
 *
 * @note Stopping the timer by @ref stop_rt_timer() sets the timer back
 * into its default (periodic) mode. Always call @ref
 * rt_set_oneshot_mode() before each @ref start_rt_timer() if you want to
 * be sure to have it oneshot on multiple insmod without rmmoding the
 * RTAI scheduler in use. 
 */
void rt_set_periodic_mode(void)
{
	stop_rt_timer();
	oneshot_timer = oneshot_running = 0;
}


/**
 * @ingroup timer
 * @anchor rt_set_oneshot_mode
 * @brief Set timer mode.
 *
 * rt_set_periodic_mode sets the periodic mode for the timer. It
 * consists of a fixed frequency timing of the tasks in multiple of
 * the period set with a call to @ref start_rt_timer(). The resolution
 * is that of the 8254 (1193180 Hz) on a UP machine, or if the 8254
 * based SMP scheduler is being used. For the SMP scheduler timed by
 * the local APIC timer and for the MUP scheduler the timer resolution
 * is that of the local APIC timer frequency, generally the bus
 * frequency divided 16. Any timing request not being an integer
 * multiple of the set timer period is satisfied at the closest period
 * tick. It is the default mode when no call is made to set the
 * oneshot mode. 
 *
 * @note Stopping the timer by @ref stop_rt_timer() sets the timer back
 * into its default (periodic) mode. Always call @ref
 * rt_set_oneshot_mode() before each @ref start_rt_timer() if you want to
 * be sure to have it oneshot on multiple insmod without rmmoding the
 * RTAI scheduler in use. 
 */
void rt_set_oneshot_mode(void)
{
	stop_rt_timer();
	oneshot_timer = 1;
}


int rt_get_timer_cpu(void)
{
	return -EINVAL;
}


DECLR_8254_TSC_EMULATION;

/**
 * @ingroup timer
 * @anchor start_rt_timer
 * @brief Start timer.
 *
 * start_rt_timer starts the timer with a period @e period. The
 * period is in internal count units and is required only for the
 * periodic mode. In the oneshot mode the period value is ignored.
 * This functions uses the 8254 with the UP and the 8254 based SMP
 * scheduler. 
 * Otherwise it uses a single local APIC with the APIC based SMP
 * schedulers and an APIC for each CPU with the MUP scheduler. In the
 * latter case all local APIC timers are paced in the same way,
 * according to the timer mode set.
 *
 * @return The period in internal count units.
 */
RTIME start_rt_timer(int period)
{
	unsigned long flags;

	hard_save_flags_and_cli(flags);
	if (oneshot_timer) {
		SETUP_8254_TSC_EMULATION;
		rt_request_timer(rt_timer_handler, 0, 0);
		tuned.timers_tol[0] = rt_half_tick = tuned.latency;
		oneshot_running = shot_fired = 1;
	} else {
		rt_request_timer(rt_timer_handler, period > LATCH ? LATCH : period, 0);
		tuned.timers_tol[0] = rt_half_tick = (rt_times.periodic_tick + 1)>>1;
	}
	rt_time_h = rt_times.tick_time + rt_half_tick;
	hard_restore_flags(flags);
	rt_request_linux_irq(TIMER_8254_IRQ, recover_jiffies, "rtai_jif_chk", recover_jiffies);
	return period;
}


RTIME start_rt_timer_cpuid(int period, int cpuid)
{
	return start_rt_timer(period);
}


/**
 * @ingroup timer
 * @anchor start_rt_apic_timer
 * @brief Start local apic timer.
 * 
 * start_rt_apic_timers starts local APIC timers according to what is
 * found in @e setup_data.
 *
 * @param setup_mode is a pointer to an array of structures
 *        apic_timer_setup_data, see function rt_setup_apic_timers
 *        (FIXME) in RTAI module functions described further on in
 *        this manual.
 * @param rcvr_jiffies_cpuid is the CPU number whose time log has to
 *  	  be used to keep Linux timing and pacing in tune.
 *	  This function is specific to the MUP scheduler. If it is
 *	  called with either the UP or SMP scheduler it will use:
 *	  - a periodic timer if all local APIC timers are periodic
 *	    with the same period;
 *	  - a oneshot timer if all the local APIC timers are oneshot, 
 *	    or have different timing modes, are periodic with
 *	    different periods. 
 */
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


/**
 * @ingroup timer
 * @anchor stop_rt_timer
 * @brief Stop timer.
 *
 * stop_rt_timer stops the timer. The timer mode is set to periodic.
 *
 * @return The period in internal count units.
 */
void stop_rt_timer(void)
{
	unsigned long flags;
	rt_free_linux_irq(TIMER_8254_IRQ, recover_jiffies);
	hard_save_flags_and_cli(flags);
	CLEAR_8254_TSC_EMULATION;
	rt_free_timer();
	oneshot_timer = oneshot_running = 0;
	hard_restore_flags(flags);
}


int rt_sched_type(void)
{
	return RT_SCHED_UP;
}


/**
 * @ingroup tasks
 * @anchor rt_preempt_always
 * @brief Enable hard preemption
 *
 * In the oneshot mode the next timer expiration is programmed after a
 * timer shot by choosing among the timed tasks the one with a
 * priority higher than the task chosen to run as current, with the
 * constraint of always assuring a correct Linux timing. In such a
 * view there is no need to fire the timer immediately. In fact it can
 * happen that the current task can be so fast to get suspended and
 * rerun before the one that was devised to time the next shot when it
 * was made running. In such a view @b RTAI schedulers try to shoot
 * only when strictly needed. This minimizes the number of slow setups
 * of the 8254 timer used with UP and 8254 based SMP
 * schedulers. While such a policy minimizes the number of actual
 * shots, greatly enhancing efficiency, it can be unsuitable when an
 * application has to be guarded against undesired program loops or
 * other unpredicted error causes.
 * Calling these functions with a nonzero value assures that a timed
 * high priority preempting task is always programmed to be fired
 * while another task is currently running. The default is no
 * immediate preemption in oneshot mode, i.e. firing of the next shot
 * programmed only when strictly needed to satisfy tasks timings.
 *
 * @note With UP and SMP schedulers there is always only a timing
 * source so that cpu_idinrt_preempt_always_cpuid is not used. With
 * the MUP scheduler you have an independent timer for each CPU, so
 * rt_preempt_always applies to all the CPUs while
 * rt_preempt_always_cpuid should be used when preemption is to be
 * forced only on a specific CPU. 
 */
void rt_preempt_always(int yes_no)
{
	preempt_always = yes_no ? 1 : 0;
}


/**
 * @ingroup tasks
 * @anchor rt_preempt_always_cpuid
 * @brief Enable hard preemption
 *
 * In the oneshot mode the next timer expiration is programmed after a
 * timer shot by choosing among the timed tasks the one with a
 * priority higher than the task chosen to run as current, with the
 * constraint of always assuring a correct Linux timing. In such a
 * view there is no need to fire the timer immediately. In fact it can
 * happen that the current task can be so fast to get suspended and
 * rerun before the one that was devised to time the next shot when it
 * was made running. In such a view @b RTAI schedulers try to shoot
 * only when strictly needed. This minimizes the number of slow setups
 * of the 8254 timer used with UP and 8254 based SMP
 * schedulers. While such a policy minimizes the number of actual
 * shots, greatly enhancing efficiency, it can be unsuitable when an
 * application has to be guarded against undesired program loops or
 * other unpredicted error causes.
 * Calling these functions with a nonzero value assures that a timed
 * high priority preempting task is always programmed to be fired
 * while another task is currently running. The default is no
 * immediate preemption in oneshot mode, i.e. firing of the next shot
 * programmed only when strictly needed to satisfy tasks timings.
 *
 * @note With UP and SMP schedulers there is always only a timing
 * source so that cpu_idinrt_preempt_always_cpuid is not used. With
 * the MUP scheduler you have an independent timer for each CPU, so
 * rt_preempt_always applies to all the CPUs while
 * rt_preempt_always_cpuid should be used when preemption is to be
 * forced only on a specific CPU. 
 */
void rt_preempt_always_cpuid(int yes_no, unsigned int cpuid)
{
	rt_preempt_always(yes_no);
}


RT_TRAP_HANDLER rt_set_task_trap_handler( RT_TASK *task,
					  unsigned int vec,
					  RT_TRAP_HANDLER handler)
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
        if (!rt_current) {
		return 0;
	}

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

static int Latency = LATENCY_8254;
MODULE_PARM(Latency, "i");

static int SetupTimeTIMER = SETUP_TIME_8254;
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

/**
 * @ingroup timer
 * @anchor count2nano
 * @brief Convert internal count units to nanoseconds.
 *
 * This function converts the time of timercounts internal count units
 * into nanoseconds.
 * Remember that the count units are related to the time base being 
 * used (see functions @ref rt_set_oneshot_mode() and @ref
 * rt_set_periodic_mode() for an explanation).
 *
 * @param counts internal count units.
 *
 * @return The given time in nanoseconds is returned.
 */
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


/**
 * @ingroup timer
 * @anchor nano2count
 * @brief Convert nanoseconds to internal count units.
 *
 * This function converts the time of nanosecs @e nanoseconds into
 * internal counts units.
 * Remember that the count units are related to the time base being
 * used (see functions @ref rt_set_oneshot_mode() and @ref
 * rt_set_periodic_mode() for an explanation).
 *
 * The versions ending with_cpuid are to be used with the MUP
 * scheduler since with such a scheduler it is possible to have
 * independent timers, i.e. periodic of different periods or a mixing
 * of periodic and oneshot, so that it is impossible to establish
 * which conversion units should be used in the case one asks for a
 * conversion from any CPU for any other CPU. All these functions have
 * the same behavior with UP and SMP schedulers. 
 *
 * @param ns Number of nanoseconds.
 *
 * @return The given time in nanoseconds is returned.
 */
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


/**
 * @ingroup timer
 * @anchor count2nano_cpuid
 * @brief Convert internal count units to nanoseconds.
 *
 * This function converts the time of timercounts internal count units
 * into nanoseconds.
 * It is to be used with the MUP scheduler since with such a scheduler
 * it is possible to have independent timers, i.e. periodic of
 * different periods or a mixing of periodic and oneshot, so that it
 * is impossible to establish which conversion units should be used in
 * the case one asks for a conversion from any CPU for any other
 * CPU. All these functions have the same behavior with UP and SMP
 * schedulers.
 *
 * @param counts internal count units. 
 *
 * @param cpuid Identifier of the CPU (FIXME).
 *
 * @return The given time in nanoseconds is returned.
 */
RTIME count2nano_cpuid(RTIME counts, unsigned int cpuid)
{
	return count2nano(counts);
}


/**
 * @ingroup timer
 * @anchor nano2count_cpuid
 * @brief Convert nanoseconds to internal count units.
 *
 * This function converts the time of nanosecs @e nanoseconds into
 * internal counts units.
 * Remember that the count units are related to the time base being
 * used (see functions @ref rt_set_oneshot_mode() and @ref
 * rt_set_periodic_mode() for an explanation).
 *
 * This function is to be used with the MUP scheduler since with such
 * a scheduler it is possible to have independent timers,
 * i.e. periodic of different periods or a mixing  of periodic and
 * oneshot, so that it is impossible to establish which conversion
 * units should be used in the case one asks for a conversion from
 * any CPU for any other CPU. All these functions have the same
 * behavior with UP and SMP schedulers.
 *
 * @param ns Number of nanoseconds.
 *
 * @param cpuid Identifier of the CPU (FIXME).
 *
 * @return The given time in nanoseconds is returned.
 */
RTIME nano2count_cpuid(RTIME ns, unsigned int cpuid)
{
	return nano2count(ns);
}

/* +++++++++++++++++++++++++++++++ TIMINGS ++++++++++++++++++++++++++++++++++ */

/**
 * @ingroup timer
 * @anchor rt_get_time
 * @brief Get the current time.
 *
 * rt_get_time returns the time, in internal count units, since
 * start_rt_timer was called. In periodic mode this number is in
 * multiples of the periodic tick. In oneshot mode it is directly the
 * TSC count for CPUs having a time stamp clock (TSC), while it is a
 * (FIXME) on 8254 units for those not having it (see functions @ref
 * rt_set_oneshot_mode() and @ref rt_set_periodic_mode() for an
 * explanation). 
 *
 * @return The current time in internal count units is returned.
 */
RTIME rt_get_time(void)
{
	return oneshot_timer ? rdtsc() : rt_times.tick_time;
}



/**
 * @ingroup timer
 * @anchor rt_get_time_cpuid
 * @brief Get the current time.
 *
 * rt_get_time_cpuid returns the time, in internal count units, since
 * start_rt_timer was called. In periodic mode this number is in
 * multiples of the periodic tick. In oneshot mode it is directly the
 * TSC count for CPUs having a time stamp clock (TSC), while it is a
 * (FIXME) on 8254 units for those not having it (see functions @ref
 * rt_set_oneshot_mode() and @ref rt_set_periodic_mode() for an
 * explanation). 
 * This version ending with _cpuid must be used with the MUP
 * scheduler when there is the need to declare from which cpuid the 
 * time must be gotten (FIXME). In fact one can need to get the time
 * of another CPU and timers can differ from CPU to CPU. (FIXME)
 * All these functions have the same behavior with UP and SMP
 * schedulers.
 *
 * @param cpuid corresponds to the CPUI identifier.
 *
 * @return The current time in internal count units is returned.
 */
RTIME rt_get_time_cpuid(unsigned int cpuid)
{
	return oneshot_timer ? rdtsc(): rt_times.tick_time;
}


/**
 * @ingroup timer
 * @anchor rt_get_time_ns
 * @brief Get the current time.
 *
 * rt_get_time_ns is the same as @ref rt_get_time() but the returned
 * time is converted to nanoseconds. 
 *
 * @return The current time in internal count units is returned.
 */
RTIME rt_get_time_ns(void)
{
	return oneshot_timer ?
	       llimd(rdtsc(), 1000000000, tuned.cpu_freq) :
	       llimd(rt_times.tick_time, 1000000000, TIMER_FREQ);
}


/**
 * @ingroup timer
 * @anchor rt_get_time_ns_cpuid
 * @brief Get the current time.
 *
 * rt_get_time_ns is the same as rt_get_time but the returned time is
 * converted to nanoseconds.
 * The version ending with _cpuid must be used with the MUP scheduler
 * when there is the need to declare from which cpuidthe time must be
 * got. In fact one can need to get the time of another CPU and timers
 * can differ from CPU to CPU. 
 * All these functions have the same behavior with UP and SMP
 * schedulers. 
 *
 * @param cpuid corresponds to the CPUI identifier.
 *
 * @return The current time in internal count units is returned.
 */
RTIME rt_get_time_ns_cpuid(unsigned int cpuid)
{
	return oneshot_timer ?
	       llimd(rdtsc(), 1000000000, tuned.cpu_freq) :
	       llimd(rt_times.tick_time, 1000000000, TIMER_FREQ);
}


/**
 * @ingroup timer
 * @anchor rt_get_cpu_time_ns
 * @brief Get the current time.
 *
 * rt_get_cpu_time_ns always returns the CPU time in nanoseconds
 * whatever timer is in use.
 *
 * @return The current time in internal count units is returned.
 */
RTIME rt_get_cpu_time_ns(void)
{
	return llimd(rdtsc(), 1000000000, tuned.cpu_freq);
}

/* +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */

RT_TASK *rt_get_base_linux_task(RT_TASK **base_linux_tasks)
{
        return (base_linux_tasks[0] = &rt_linux_task);
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
    	RT_TASK	*task;

    	if (wdog_task) return (RT_TASK**) -EBUSY;
	task = &rt_linux_task;
	while ((task = task->next)) {
	    	if (task != wd && task->priority == RT_SCHED_HIGHEST_PRIORITY) {
		    	return (RT_TASK**) -EBUSY;
		}
	}
    	wdog_task = wd;
	return (RT_TASK**) 0;
}

void rt_deregister_watchdog(RT_TASK *wd, int cpuid)
{
    	if (wdog_task != wd) return;
    	wdog_task = NULL;
}

/* ++++++++++++++ SCHEDULER ENTRIES AND RELATED INITIALISATION ++++++++++++++ */

struct rt_fun_entry rt_fun_lxrt[MAX_LXRT_FUN];

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

	task = &rt_linux_task;
	PROC_PRINT("\nRTAI Uniprocessor Real Time Task Scheduler.\n\n");
	PROC_PRINT("    Calibrated CPU Frequency: %lu Hz\n", (unsigned long)tuned.cpu_freq);
	PROC_PRINT("    Calibrated timer interrupt to scheduler latency: %d ns\n", imuldiv(tuned.latency - tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));
	PROC_PRINT("    Calibrated one shot setup time: %d ns\n\n",
                  imuldiv(tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));
        PROC_PRINT("Number of RT CPUs in system: %d\n\n", NR_RT_CPUS);

        PROC_PRINT("Priority  Period(ns)  FPU  Sig  State  Task  RT_TASK *  TIME\n" );
        PROC_PRINT("-------------------------------------------------------------\n" );
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
                PROC_PRINT("%-9d %-11lu %-4s %-4s 0x%-4x %-4d  %p   %-lu\n",
                               task->priority,
                               (unsigned long)count2nano(task->period),
                               task->uses_fpu ? "Yes" : "No",
                               task->signal ? "Yes" : "No",
                               task->state,
                               i, task, t);
		i++;
	}

        PROC_PRINT("TIMED\n");
        task = &rt_linux_task;
        while ((task = task->tnext) != &rt_linux_task) {
                PROC_PRINT("> %p ", task);
        }
        PROC_PRINT("\nREADY\n");
        task = &rt_linux_task;
        while ((task = task->rnext) != &rt_linux_task) {
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

static int __rtai_up_init(void)
{
	sched_mem_init();
	init_sched_entries();
	rt_linux_task.uses_fpu = LinuxFpu ? 1 : 0;
	rt_linux_task.magic = 0;
	rt_linux_task.policy = 0;
	rt_linux_task.state = RT_SCHED_READY;
	rt_linux_task.msg_queue.prev = &(rt_linux_task.msg_queue);
	rt_linux_task.msg_queue.next = &(rt_linux_task.msg_queue);
	rt_linux_task.msg_queue.task = &rt_linux_task;
	rt_linux_task.msg = 0;
	rt_linux_task.ret_queue.prev = &(rt_linux_task.ret_queue);
	rt_linux_task.ret_queue.next = &(rt_linux_task.ret_queue);
	rt_linux_task.ret_queue.task = NOTHING;
	rt_linux_task.priority = rt_linux_task.base_priority = RT_SCHED_LINUX_PRIORITY;
	rt_linux_task.signal = 0;
	rt_linux_task.prev = &rt_linux_task;
	rt_linux_task.next = 0;
	rt_linux_task.resume_time = RT_TIME_END;
	rt_linux_task.tprev = rt_linux_task.tnext =
	rt_linux_task.rprev = rt_linux_task.rnext = &rt_linux_task;
	rt_current = &rt_linux_task;
	fpu_task = &rt_linux_task;
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

	printk(KERN_INFO "RTAI[sched_up]: loaded.\n");
	printk(KERN_INFO "RTAI[sched_up]: fpu=%s, timer=%s.\n",
	       LinuxFpu ? "yes" : "no",
	       oneshot_timer ? "oneshot" : "periodic");
	printk(KERN_INFO "RTAI[sched_up]: standard tick=%d hz, CPU freq=%lu hz.\n",
	       HZ,
	       (unsigned long)tuned.cpu_freq);
	printk(KERN_INFO "RTAI[sched_up]: timer setup=%d ns, resched latency=%d ns.\n",
	       imuldiv(tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq),
	       imuldiv(tuned.latency - tuned.setup_time_TIMER_CPUNIT, 1000000000, tuned.cpu_freq));

	return rtai_init_features(); /* see rtai_schedcore.h */
}


extern void krtai_objects_release(void);

static void __rtai_up_exit(void)
{
	RT_SET_RTAI_TRAP_HANDLER(NULL);
	stop_rt_timer();
	while (rt_linux_task.next) {
		rt_task_delete(rt_linux_task.next);
	}
	krtai_objects_release();
#ifdef CONFIG_PROC_FS
        rtai_proc_sched_unregister();
#endif
	while (frstk_srq.out != frstk_srq.in);
	if (rt_free_srq(frstk_srq.srq) < 0) {
		printk("MEM SRQ: frstk_srq %d illegal or already free.\n", frstk_srq.srq);
	}

#ifdef CONFIG_RTAI_ADEOS
	if (sched_virq)
	    {
	    adeos_virtualize_irq_from(&rtai_domain,sched_virq,NULL,NULL,0);
	    adeos_free_irq(sched_virq);
	    }
#endif /* CONFIG_RTAI_ADEOS */

	rtai_cleanup_features();
	sched_mem_end();
#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
	rt_set_ihook(NULL);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */
	rt_umount_rtai();
	printk(KERN_INFO "RTAI[sched_up]: unloaded.\n");
}

module_init(__rtai_up_init);
module_exit(__rtai_up_exit);
