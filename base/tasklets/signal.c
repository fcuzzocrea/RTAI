/*
 * Copyright (C) 2006 Paolo Mantegazza <mantegazza@aero.polimi.it>
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


#include <linux/kernel.h>
#include <linux/module.h>

#include <rtai_schedcore.h>
#include <rtai_signal.h>

MODULE_LICENSE("GPL");
#define MODULE_NAME "RTAI_SIGNALS"

#define RT_SCHED_SIGSUSP  (1 << 15)

#define RT_SIGNALS ((struct rt_signal_t *)task->rt_signals)
struct rt_signal_t { unsigned long flags; RT_TASK *sigtask; };

RTAI_SYSCALL_MODE static int rt_request_signal_(RT_TASK *sigtask, RT_TASK *task, long signal)
{
	int retval;
	if (signal >= 0 && sigtask && task) {
		if (!task->rt_signals) {
			task->rt_signals = rt_malloc(MAXSIGNALS*sizeof(struct rt_signal_t));
			task->pstate = 0;
		}
		RT_SIGNALS[signal].flags = (1 << SIGNAL_ENBIT);
		RT_SIGNALS[signal].sigtask = sigtask;
		retval = 0;
	} else {
		retval = -EINVAL;
	}
	task->retval = retval;
	rt_task_resume(task);
	return retval;
}
EXPORT_SYMBOL(rt_request_signal_);

static inline void rt_exec_signal(RT_TASK *sigtask, RT_TASK *task)
{
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	if (!(--sigtask->suspdepth)) {
		if (task) {
			sigtask->priority = task->priority; 
 			if (!task->pstate++) {
				rem_ready_task(task);
				task->state |= RT_SCHED_SIGSUSP;
			}
		}
		sigtask->state &= ~RT_SCHED_SIGSUSP;
		sigtask->retval = (long)task;
		enq_ready_task(sigtask);
		RT_SCHEDULE(sigtask, rtai_cpuid());
	}
	rt_global_restore_flags(flags);
}

/**
 * Release a signal previously requested for a task.
 *
 * @param signal, >= 0, is the signal.
 *
 * @param task is the task for which the signal was previously requested.
 *
 * A call of this function will release a signal previously requested for 
 * a task.
 *
 * @retval 0 on success.
 * @return -EINVAL in case of error.
 *
 */

RTAI_SYSCALL_MODE int rt_release_signal(long signal, RT_TASK *task)
{
	if (task == NULL) {
		task = RT_CURRENT;
	}
	if (signal >= 0 && RT_SIGNALS && RT_SIGNALS[signal].sigtask) {
		RT_SIGNALS[signal].sigtask->priority = task->priority; 
		rt_exec_signal(RT_SIGNALS[signal].sigtask, 0);
		return 0;
	}
	return -EINVAL;
}
EXPORT_SYMBOL(rt_release_signal);

/**
 * Trigger a signal for a task (i.e. send a signal to the task), executing 
 * the related handler.
 *
 * @param signal, >= 0, is the signal.
 *
 * @param task is the task to which the signal is sent.
 *
 * A call of this function will stop the task served by signal, if executing, 
 * till the triggered handler has finished its execution, carried out at the 
 * same priority and on the same CPU of the task it is serving.
 *
 */

RTAI_SYSCALL_MODE void rt_trigger_signal(long signal, RT_TASK *task)
{
	if (task == NULL) {
		task = RT_CURRENT;
	}
	if (signal >= 0 && RT_SIGNALS && RT_SIGNALS[signal].sigtask) {
		do {
			if (test_and_clear_bit(SIGNAL_ENBIT, &RT_SIGNALS[signal].flags)) {
				rt_exec_signal(RT_SIGNALS[signal].sigtask, task);
				test_and_set_bit(SIGNAL_ENBIT, &RT_SIGNALS[signal].flags);
			} else {
				test_and_set_bit(SIGNAL_PNDBIT, &RT_SIGNALS[signal].flags);
				break;
			}
		} while (test_and_clear_bit(SIGNAL_PNDBIT, &RT_SIGNALS[signal].flags));
	}
}
EXPORT_SYMBOL(rt_trigger_signal);

/**
 * Enable a signal for a task.
 *
 * @param signal, >= 0, is the signal.
 *
 * @param task is the task which signal is enabled.
 *
 * A call of this function will enable reception of the related signal by
 * task.
 *
 */

RTAI_SYSCALL_MODE void rt_enable_signal(long signal, RT_TASK *task)
{
	if (task == NULL) {
		task = RT_CURRENT;
	}
	if (signal >= 0 && RT_SIGNALS) {
		set_bit(SIGNAL_ENBIT, &RT_SIGNALS[signal].flags);
	}
}
EXPORT_SYMBOL(rt_enable_signal);

/**
 * disable a signal for a task.
 *
 * @param signal, >= 0, is the signal.
 *
 * @param task is the task which signal is enabled.
 *
 * A call of this function will disable reception of the related signal by
 * task.
 *
 */

RTAI_SYSCALL_MODE void rt_disable_signal(long signal, RT_TASK *task)
{
	if (task == NULL) {
		task = RT_CURRENT;
	}
	if (signal >= 0 && RT_SIGNALS) {
		clear_bit(SIGNAL_ENBIT, &RT_SIGNALS[signal].flags);
	}
}
EXPORT_SYMBOL(rt_disable_signal);

static RTAI_SYSCALL_MODE int rt_signal_helper(RT_TASK *task)
{
	if (task) {
		rt_task_suspend(task);
		return task->retval;
	}
	return (RT_CURRENT)->runnable_on_cpus;
}

RTAI_SYSCALL_MODE int rt_wait_signal(RT_TASK *sigtask, RT_TASK *task)
{
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	if (!sigtask->suspdepth++) {
		sigtask->state |= RT_SCHED_SIGSUSP;
		rem_ready_current(sigtask);
		if (task->pstate > 0 && !(--task->pstate) && (task->state &= ~RT_SCHED_SIGSUSP) == RT_SCHED_READY) {
                       	enq_ready_task(task);
       		}
		rt_schedule();
	}
	rt_global_restore_flags(flags);
	return sigtask->retval;
}
EXPORT_SYMBOL(rt_wait_signal);

static void signal_suprt_fun(struct sigsuprt_t *funarg)
{		
	struct sigsuprt_t arg = *funarg;

	arg.sigtask = RT_CURRENT;
	if (!rt_request_signal_(arg.sigtask, arg.task, arg.signal)) {
		while (rt_wait_signal(arg.sigtask, arg.task)) {
			arg.sighdl(arg.signal, arg.task);
		}
	} else {
		rt_task_resume(arg.task);
	}
}

/**
 * Install a handler for catching RTAI real time async signals.
 *
 * @param signal, >= 0, is the signal.
 *
 * @param sighdl is the handler that will execute upon signal reception.
 *
 * RTAI real time signal handlers are executed within a host hard real time
 * thread, assigned to the same CPU of the receiving task, while the task 
 * receiving the signal is kept stopped. No difference between kernel and 
 * user space, the usual symmetric usage.
 * If the request is succesfull the function will return with signal reception 
 * enabled.
 *
 * @retval 0 on success.
 * @return -EINVAL in case of error.
 *
 */

int rt_request_signal(long signal, void (*sighdl)(long, RT_TASK *))
{
	if (signal >= 0 && sighdl) {
		RT_TASK *task = RT_CURRENT;
		struct sigsuprt_t arg = { NULL, task, signal, sighdl, task->runnable_on_cpus };
		if ((task = rt_malloc(sizeof(RT_TASK)))) {
			rt_task_init_cpuid(task, (void *)signal_suprt_fun, (long)&arg, SIGNAL_TASK_STACK_SIZE, arg.task->priority, 0, 0, arg.cpuid);
			rt_task_resume(task);
			rt_task_suspend(arg.task);
			return arg.task->retval;
		}
	}
	return -EINVAL;
}
EXPORT_SYMBOL(rt_request_signal);

static struct rt_fun_entry rtai_signals_fun[] = {
	[SIGNAL_HELPER]  = { 1, rt_signal_helper   }, // internal, not for users
	[SIGNAL_WAITSIG] = { 1, rt_wait_signal     }, // internal, not for users
	[SIGNAL_REQUEST] = { 1, rt_request_signal_ }, // internal, not for users
	[SIGNAL_RELEASE] = { 1, rt_release_signal  },
	[SIGNAL_ENABLE]  = { 1, rt_enable_signal   },
	[SIGNAL_DISABLE] = { 1, rt_disable_signal  },
	[SIGNAL_TRIGGER] = { 1, rt_trigger_signal  }
};

int __rtai_signals_init(void)
{
	if (set_rt_fun_ext_index(rtai_signals_fun, RTAI_SIGNALS_IDX)) {
		printk("Wrong or already used LXRT extension: %d.\n", RTAI_SIGNALS_IDX);
		return -EACCES;
	}
	printk("%s: loaded.\n", MODULE_NAME);
	return 0;
}

void __rtai_signals_exit(void)
{
	reset_rt_fun_ext_index(rtai_signals_fun, RTAI_SIGNALS_IDX);
	printk("%s: unloaded.\n", MODULE_NAME);
}

#ifndef CONFIG_RTAI_TASKLETS_BUILTIN
module_init(__rtai_signals_init);
module_exit(__rtai_signals_exit);
#endif /* !CONFIG_RTAI_TASKLETS_BUILTIN */
