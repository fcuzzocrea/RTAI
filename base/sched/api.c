/** 
 * @ingroup lxrt
 * @file
 * Common scheduling function 
 * @author Paolo Mantegazza
 *
 * This file is part of the RTAI project.
 *
 * @note Copyright &copy; 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
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

#include <rtai_schedcore.h>
#include <rtai_registry.h>
#include <linux/module.h>

/* ++++++++++++++++++++++++ COMMON FUNCTIONALITIES ++++++++++++++++++++++++++ */

/* +++++++++++++++++++++++++ PRIORITY MANAGEMENT ++++++++++++++++++++++++++++ */

void rt_set_sched_policy(RT_TASK *task, int policy, int rr_quantum_ns)
{
	if ((task->policy = policy ? 1 : 0)) {
		task->rr_quantum = nano2count_cpuid(rr_quantum_ns, task->runnable_on_cpus);
		if ((task->rr_quantum & 0xF0000000) || !task->rr_quantum) {
#ifdef CONFIG_SMP
			task->rr_quantum = sqilter ? rt_smp_times[task->runnable_on_cpus].linux_tick : rt_times.linux_tick;
#else
			task->rr_quantum = rt_times.linux_tick;
#endif
		}
		task->rr_remaining = task->rr_quantum;
		task->yield_time = 0;
	}
}


/**
 * @anchor rt_get_prio
 * @brief Check a task priority.
 * 
 * rt_get_prio returns the base priority of task @e task.
 *
 * Recall that a task has a base native priority, assigned at its
 * birth or by @ref rt_change_prio(), and an actual, inherited,
 * priority. They can be different because of priority inheritance.
 *
 * @param task is the affected task.
 *
 * @return rt_get_prio returns the priority of task @e task.
 *
 * @note To be used only with RTAI24.x.xx.
 */
int rt_get_prio(RT_TASK *task)
{
	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}
	return task->base_priority;
}


/**
 * @anchor rt_get_inher_prio
 * @brief Check a task priority.
 * 
 * rt_get_prio returns the base priority task @e task has inherited
 * from other tasks, either blocked on resources owned by or waiting
 * to pass a message to task @e task.
 *
 * Recall that a task has a base native priority, assigned at its
 * birth or by @ref rt_change_prio(), and an actual, inherited,
 * priority. They can be different because of priority inheritance.
 *
 * @param task is the affected task.
 *
 * @return rt_get_inher_prio returns the priority of task @e task.
 *
 * @note To be used only with RTAI24.x.xx.
 */
int rt_get_inher_prio(RT_TASK *task)
{
	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}
	return task->base_priority;
}


/**
 * @anchor rt_change_prio
 * @brief Change a task priority.
 * 
 * rt_change_prio changes the base priority of task @e task to @e
 * prio. 
 *
 * Recall that a task has a base native priority, assigned at its
 * birth or by @ref rt_change_prio(), and an actual, inherited,
 * priority. They can be different because of priority inheritance.
 *
 * @param task is the affected task.
 *
 * @param priority is the new priority, it can range within 0 < prio < 
 * RT_SCHED_LOWEST_PRIORITY. 
 *
 * @return rt_change_prio returns the base priority task @e task had
 * before the change.
 *
 * @note To be used only with RTAI24.x.xx (FIXME).
 */
int rt_change_prio(RT_TASK *task, int priority)
{
	unsigned long flags;
	int prio;

	if (task->magic != RT_TASK_MAGIC || priority < 0) {
		return -EINVAL;
	}

	prio = task->base_priority;
	flags = rt_global_save_flags_and_cli();
	if ((task->base_priority = priority) < task->priority) {
		unsigned long schedmap;
		QUEUE *q;
		schedmap = 0;
		do {
			task->priority = priority;
			if (task->state == RT_SCHED_READY) {
				(task->rprev)->rnext = task->rnext;
				(task->rnext)->rprev = task->rprev;
				enq_ready_task(task);
#ifdef CONFIG_SMP
				set_bit(task->runnable_on_cpus & 0x1F, &schedmap);
#else
				schedmap = 1;
#endif
			} else if ((q = task->blocked_on) && !((task->state & RT_SCHED_SEMAPHORE) && ((SEM *)q)->qtype)) {
				(task->queue.prev)->next = task->queue.next;
				(task->queue.next)->prev = task->queue.prev;
				while ((q = q->next) != task->blocked_on && (q->task)->priority <= priority);
				q->prev = (task->queue.prev = q->prev)->next  = &(task->queue);
				task->queue.next = q;
#ifdef CONFIG_SMP
				set_bit(task->runnable_on_cpus & 0x1F, &schedmap);
#else
				schedmap = 1;
#endif
			}
		} while ((task = task->prio_passed_to) && task->priority > priority);
		if (schedmap) {
#ifdef CONFIG_SMP
			if (test_and_clear_bit(hard_cpu_id(), &schedmap)) {
				RT_SCHEDULE_MAP_BOTH(schedmap);
			} else {
				RT_SCHEDULE_MAP(schedmap);
			}
#else
			rt_schedule();
#endif
		}
	}
	rt_global_restore_flags(flags);
	return prio;
}

/* +++++++++++++++++++++ TASK RELATED SCHEDULER SERVICES ++++++++++++++++++++ */


/**
 * @anchor rt_whoami
 * @brief Get the task pointer of the current task.
 *
 * Calling rt_whoami from a task can get a pointer to its own task
 * structure.
 * 
 * @return The pointer to the current task.
 */
RT_TASK *rt_whoami(void)
{
	return _rt_whoami();
}



/**
 * @anchor rt_task_yield
 * Yield the current task.
 *
 * @ref rt_task_yield() stops the current task and takes it at the end
 * of the list of ready tasks having its same priority. The scheduler
 * makes the next ready task of the same priority active.
 *
 * Recall that RTAI schedulers allow only higher priority tasks to
 * preempt the execution of lower priority ones. So equal priority
 * tasks cannot preempt each other and @ref rt_task_yield() should be
 * used if a user needs a cooperative time slicing among equal
 * priority tasks. The implementation of the related policy is wholly
 * in the hand of the user. It is believed that time slicing is too
 * much an overhead for the most demanding real time applications, so
 * it is left up to you.
 */
void rt_task_yield(void)
{
	RT_TASK *rt_current, *task;
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	task = (rt_current = RT_CURRENT)->rnext;
	while (rt_current->priority == task->priority) {
		task = task->rnext;
	}
	if (task != rt_current->rnext) {
		(rt_current->rprev)->rnext = rt_current->rnext;
		(rt_current->rnext)->rprev = rt_current->rprev;
		task->rprev = (rt_current->rprev = task->rprev)->rnext = rt_current;
		rt_current->rnext = task;
		rt_schedule();
	}
	rt_global_restore_flags(flags);
}



/**
 * @anchor rt_task_suspend
 * rt_task_suspend suspends execution of the task task.
 *
 * It will not be executed until a call to @ref rt_task_resume() or
 * @ref rt_task_make_periodic() is made. No account is made for
 * multiple suspends, i.e. a multiply suspended task is made ready as
 * soon as it is rt_task_resumed, thus immediately resuming its
 * execution if it is the highest in priority.
 *
 * @param task pointer to a task structure.
 *
 * @return 0 on success. A negative value on failure as described below:
 * - @b EINVAL: task does not refer to a valid task.
 *
 * @note the new RTAI 24.1.xx (FIXME) development releases take into
 * account multiple suspend and require as many @ref rt_task_resume()
 * as the rt_task_suspends placed on a task.
 */
int rt_task_suspend(RT_TASK *task)
{
	unsigned long flags;

	if (!task) {
		task = RT_CURRENT;
	} else if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}

	flags = rt_global_save_flags_and_cli();
	if (!task->suspdepth++ && !task->owndres) {
		rem_ready_task(task);
		task->state |= RT_SCHED_SUSPENDED;
		if (task == RT_CURRENT) {
			rt_schedule();
		}
	}
	rt_global_restore_flags(flags);
	return 0;
}


/**
 * @anchor rt_task_resume
 * Resume a task.
 *
 * rt_task_resume resumes execution of the task @e task previously
 * suspended by @ref rt_task_suspend(), or makes a newly created task
 * ready to run, if it makes the task ready. Since no account is made
 * for multiple suspend rt_task_resume unconditionally resumes any
 * task it makes ready.
 *
 * @param task pointer to a task structure.
 *
 * @return 0 on success. A negative value on failure as described below:
 * - @b EINVAL: task does not refer to a valid task.
 *
 * @note the new RTAI 24.1.xx (FIXME) development releases take into
 *       account multiple suspend and require as many rt_task_resumes
 *	 as the rt_task_suspends placed on a task.
 */
int rt_task_resume(RT_TASK *task)
{
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}

	flags = rt_global_save_flags_and_cli();
	if (!(--task->suspdepth)) {
		rem_timed_task(task);
		if (((task->state &= ~RT_SCHED_SUSPENDED) & ~RT_SCHED_DELAYED) == RT_SCHED_READY) {
			enq_ready_task(task);
			RT_SCHEDULE(task, hard_cpu_id());
		}
	}
	rt_global_restore_flags(flags);
	return 0;
}


/**
 * @anchor rt_get_task_state
 * Query task state
 *
 * rt_get_task_state returns the state of a real time task.
 *
 * @param task is a pointer to the task structure.
 *
 * Task state is formed by the bitwise OR of one or more of the
 * following flags:
 *
 * @retval READY Task @e task is ready to run (i.e. unblocked).
 * Note that on a UniProcessor machine the currently running task is
 * just in READY state, while on MultiProcessors can be (READY |
 * RUNNING), see below. 
 * @retval SUSPENDED Task @e task blocked waiting for a resume.
 * @retval DELAYED Task @e task blocked waiting for its next running
 * period or expiration of a timeout.
 * @retval SEMAPHORE Task @e task blocked on a semaphore, waiting for
 * the semaphore to be signaled.
 * @retval SEND Task @e task blocked on sending a message, receiver
 * was not in RECEIVE state.
 * @retval RECEIVE Task @e task blocked waiting for incoming messages,
 * sends or rpcs. 
 * @retval RPC Task @e task blocked on a Remote Procedure Call,
 * receiver was not in RECEIVE state.
 * @retval RETURN Task @e task blocked waiting for a return from a
 * Remote Procedure Call, receiver got the RPC but has not replied
 * yet. 
 * @retval RUNNING Task @e task is running, used only for SMP
 * schedulers. 
 *
 * The returned task state is just an approximate information. Timer
 * and other hardware interrupts may cause a change in the state of
 * the queried task before the caller could evaluate the returned
 * value. Caller should disable interrupts if it wants reliable info
 * about an other task.  rt_get_task_state does not perform any check
 * on pointer task.
 */
int rt_get_task_state(RT_TASK *task)
{
	return task->state;
}


/**
 * @anchor rt_linux_use_fpu
 * @brief Set indication of FPU usage.
 *
 * rt_linux_use_fpu informs the scheduler that floating point
 * arithmetic operations will be used also by foreground Linux
 * processes, i.e. the Linux kernel itself (unlikely) and any of its
 * processes. 
 *
 * @param use_fpu_flag If this parameter has a nonzero value, the
 * Floating Point Unit (FPU) context is also switched when @e task or
 * the kernel becomes active.
 * This makes task switching slower, negligibly, on all 32 bits CPUs
 * but 386s and the oldest 486s. 
 * This flag can be set also by rt_task_init when the real time task
 * is created. With UP and MUP schedulers care is taken to avoid
 * useless saves/ restores of the FPU environment. 
 * Under SMP tasks can be moved from CPU to CPU so saves/restores for
 * tasks using the FPU are always carried out. 
 * Note that by default Linux has this flag cleared. Beside by using
 * rt_linux_use_fpu you can change the Linux FPU flag when you insmod
 * any RTAI scheduler module by setting the LinuxFpu command line
 * parameter of the rtai_sched module itself.
 *
 * @return 0 on success. A negative value on failure as described below:
 * - @b EINVAL: task does not refer to a valid task.
 *
 * See also: @ref rt_linux_use_fpu().
 */
void rt_linux_use_fpu(int use_fpu_flag)
{
	int cpuid;
	for (cpuid = 0; cpuid < num_online_cpus(); cpuid++) {
		rt_linux_task.uses_fpu = use_fpu_flag ? 1 : 0;
	}
}


/**
 * @anchor rt_task_use_fpu
 * @brief 
 *
 * rt_task_use_fpu informs the scheduler that floating point
 * arithmetic operations will be used by the real time task @e task.
 *
 * @param task is a pointer to the real time task.
 * 
 * @param use_fpu_flag If this parameter has a nonzero value, the
 * Floating Point Unit (FPU) context is also switched when @e task or
 * the kernel becomes active.
 * This makes task switching slower, negligibly, on all 32 bits CPUs
 * but 386s and the oldest 486s.
 * This flag can be set also by @ref rt_task_init() when the real time
 * task is created. With UP and MUP schedulers care is taken to avoid
 * useless saves/restores of the FPU environment.
 * Under SMP tasks can be moved from CPU to CPU so saves/restores for
 * tasks using the FPU are always carried out. 
 *
 * @return 0 on success. A negative value on failure as described below:
 * - @b EINVAL: task does not refer to a valid task.
 *
 * See also: @ref rt_linux_use_fpu().
 */
int rt_task_use_fpu(RT_TASK *task, int use_fpu_flag)
{
	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}
	task->uses_fpu = use_fpu_flag ? 1 : 0;
	return 0;
}


/**
 * @anchor rt_task_signal_handler
 * @brief Set the signal handler of a task.
 *
 * rt_task_signal_handler installs, or changes, the signal function
 * of a real time task.
 *
 * @param task is a pointer to the real time task.
 *
 * @param handler is the entry point of the signal function.
 *
 * A signal handler function can be set also when the task is newly
 * created with @ref rt_task_init(). The signal handler is a function
 * called within the task environment and with interrupts disabled,
 * when the task becomes the current running task after a context
 * switch, except at its very first scheduling. It allows you to
 * implement whatever signal management policy you think useful, and
 * many other things as well (FIXME).
 *
 * @return 0 on success.A negative value on failure as described below:
 * - @b EINVAL: task does not refer to a valid task.
 */
int rt_task_signal_handler(RT_TASK *task, void (*handler)(void))
{
	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}
	task->signal = handler;
	return 0;
}

/* ++++++++++++++++++++++++++++ MEASURING TIME ++++++++++++++++++++++++++++++ */

void rt_gettimeorig(RTIME time_orig[])
{
	unsigned long flags;
	struct timeval tv;
	hard_save_flags_and_cli(flags);
	do_gettimeofday(&tv);
	time_orig[0] = rdtsc();
	hard_restore_flags(flags);
	time_orig[0] = tv.tv_sec*(long long)tuned.cpu_freq + llimd(tv.tv_usec, tuned.cpu_freq, 1000000) - time_orig[0];
	time_orig[1] = llimd(time_orig[0], 1000000000, tuned.cpu_freq);
}

/* +++++++++++++++++++++++++++ CONTROLLING TIME ++++++++++++++++++++++++++++++ */

/**
 * @anchor rt_task_make_periodic_relative_ns
 * Make a task run periodically.
 *
 * rt_task_make_periodic_relative_ns mark the task @e task, previously
 * created with @ref rt_task_init(), as suitable for a periodic
 * execution, with period @e period, when @ref rt_task_wait_period()
 * is called.
 *
 * The time of first execution is defined through @e start_time or @e
 * start_delay. @e start_time is an absolute value measured in clock
 * ticks. @e start_delay is relative to the current time and measured
 * in nanoseconds. 
 *
 * @param task is a pointer to the task you want to make periodic.
 *
 * @param start_delay is the time, to wait before the task start
 *	  running, in nanoseconds.
 *
 * @param period corresponds to the period of the task, in nanoseconds.
 *
 * @retval 0 on success. A negative value on failure as described below:
 * - @b EINVAL: task does not refer to a valid task.
 *
 * Recall that the term clock ticks depends on the mode in which the hard
 * timer runs. So if the hard timer was set as periodic a clock tick will
 * last as the period set in start_rt_timer, while if oneshot mode is used
 * a clock tick will last as the inverse of the running frequency of the
 * hard timer in use and irrespective of any period used in the call to
 * start_rt_timer.
 */
int rt_task_make_periodic_relative_ns(RT_TASK *task, RTIME start_delay, RTIME period)
{
	long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}
	start_delay = nano2count_cpuid(start_delay, task->runnable_on_cpus);
	period = nano2count_cpuid(period, task->runnable_on_cpus);
	flags = rt_global_save_flags_and_cli();
	task->resume_time = rt_get_time_cpuid(task->runnable_on_cpus) + start_delay;
	task->period = period;
	task->suspdepth = 0;
        if (!(task->state & RT_SCHED_DELAYED)) {
		rem_ready_task(task);
		task->state = (task->state & ~RT_SCHED_SUSPENDED) | RT_SCHED_DELAYED;
		enq_timed_task(task);
}
	RT_SCHEDULE(task, hard_cpu_id());
	rt_global_restore_flags(flags);
	return 0;
}


/**
 * @anchor rt_task_make_periodic
 * Make a task run periodically
 *
 * rt_task_make_periodic mark the task @e task, previously created
 * with @ref rt_task_init(), as suitable for a periodic execution, with
 * period @e period, when @ref rt_task_wait_period() is called.
 *
 * The time of first execution is defined through @e start_time or @e
 * start_delay. @e start_time is an absolute value measured in clock
 * ticks.  @e start_delay is relative to the current time and measured
 * in nanoseconds.
 *
 * @param task is a pointer to the task you want to make periodic.
 *
 * @param start_time is the absolute time to wait before the task start
 *	  running, in clock ticks.
 *
 * @param period corresponds to the period of the task, in clock ticks.
 *
 * @retval 0 on success. A negative value on failure as described
 * below: 
 * - @b EINVAL: task does not refer to a valid task.
 *
 * See also: @ref rt_task_make_periodic_relative_ns().
 * Recall that the term clock ticks depends on the mode in which the hard
 * timer runs. So if the hard timer was set as periodic a clock tick will
 * last as the period set in start_rt_timer, while if oneshot mode is used
 * a clock tick will last as the inverse of the running frequency of the
 * hard timer in use and irrespective of any period used in the call to
 * start_rt_timer.
 *
 */
int rt_task_make_periodic(RT_TASK *task, RTIME start_time, RTIME period)
{
	long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}
	flags = rt_global_save_flags_and_cli();
	task->resume_time = start_time;
	task->period = period;
	task->suspdepth = 0;
        if (!(task->state & RT_SCHED_DELAYED)) {
		rem_ready_task(task);
		task->state = (task->state & ~RT_SCHED_SUSPENDED) | RT_SCHED_DELAYED;
		enq_timed_task(task);
	}
	RT_SCHEDULE(task, hard_cpu_id());
	rt_global_restore_flags(flags);
	return 0;
}


/**
 * @anchor rt_task_wait_period
 * Wait till next period.
 *
 * rt_task_wait_period suspends the execution of the currently running
 * real time task until the next period is reached.
 * The task must have
 * been previously marked for a periodic execution by calling
 * @ref rt_task_make_periodic() or @ref rt_task_make_periodic_relative_ns().
 *
 * @note The task is suspended only temporarily, i.e. it simply gives
 * up control until the next time period.
 */
void rt_task_wait_period(void)
{
	DECLARE_RT_CURRENT;
	long flags;

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (rt_current->resync_frame) { // Request from watchdog
	    	rt_current->resync_frame = 0;
#ifdef CONFIG_SMP
		rt_current->resume_time = oneshot_timer ? rdtsc() : sqilter ? rt_smp_times[cpuid].tick_time : rt_times.tick_time;
#else
		rt_current->resume_time = oneshot_timer ? rdtsc() : rt_times.tick_time;
#endif
	} else if ((rt_current->resume_time += rt_current->period) > rt_time_h) {
		rt_current->state |= RT_SCHED_DELAYED;
		rem_ready_current(rt_current);
		enq_timed_task(rt_current);
		rt_schedule();
	}
	rt_global_restore_flags(flags);
}

void rt_task_set_resume_end_times(RTIME resume, RTIME end)
{
	RT_TASK *rt_current;
	long flags;

	flags = rt_global_save_flags_and_cli();
	rt_current = RT_CURRENT;
	rt_current->policy   = -1;
	rt_current->priority =  0;
	if (resume > 0) {
		rt_current->resume_time = resume;
	} else {
		rt_current->resume_time -= resume;
	}
	if (end > 0) {
		rt_current->period = end;
	} else {
		rt_current->period = rt_current->resume_time - end;
	}
	rt_current->state |= RT_SCHED_DELAYED;
	rem_ready_current(rt_current);
	enq_timed_task(rt_current);
	rt_schedule();
	rt_global_restore_flags(flags);
}

int rt_set_resume_time(RT_TASK *task, RTIME new_resume_time)
{
	long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}

	flags = rt_global_save_flags_and_cli();
	if (task->state & RT_SCHED_DELAYED) {
		if (((task->resume_time = new_resume_time) - (task->tnext)->resume_time) > 0) {
			rem_timed_task(task);
			enq_timed_task(task);
			rt_global_restore_flags(flags);
			return 0;
        	}
        }
	rt_global_restore_flags(flags);
	return -ETIME;
}

int rt_set_period(RT_TASK *task, RTIME new_period)
{
	long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}
	hard_save_flags_and_cli(flags);
	task->period = new_period;
	hard_restore_flags(flags);
	return 0;
}

/**
 * @anchor next_period
 * @brief Get the time a periodic task will be resumed after calling
 *  rt_task_wait_period.
 *
 * this function returns the time when the caller task will run
 * next. Combined with the appropriate @ref rt_get_time function() it
 * can be used for checking the fraction of period used or any period
 * overrun.
 *
 * @return Next period time in internal count units.
 */
RTIME next_period(void)
{
	RT_TASK *rt_current;
	unsigned long flags;
	flags = rt_global_save_flags_and_cli();
	rt_current = RT_CURRENT;
	rt_global_restore_flags(flags);
	return rt_current->resume_time + rt_current->period;
}

/**
 * @anchor rt_busy_sleep
 * @brief Delay/suspend execution for a while.
 *
 * rt_busy_sleep delays the execution of the caller task without
 * giving back the control to the scheduler. This function burns away
 * CPU cycles in a busy wait loop so it should be used only for very
 * short synchronization delays. On machine not having a TSC clock it
 * can lead to many microseconds uncertain busy sleeps because of the
 * need of reading the 8254 timer.
 *
 * @param ns is the number of nanoseconds to wait.
 * 
 * See also: @ref rt_sleep(), @ref rt_sleep_until().
 *
 * @note A higher priority task or interrupt handler can run before
 *	 the task goes to sleep, so the actual time spent in these
 *	 functions may be longer than that specified.
 */
void rt_busy_sleep(int ns)
{
	RTIME end_time;
	end_time = rdtsc() + llimd(ns, tuned.cpu_freq, 1000000000);
	while (rdtsc() < end_time);
}

/**
 * @anchor rt_sleep
 * @brief Delay/suspend execution for a while.
 *
 * rt_sleep suspends execution of the caller task for a time of delay
 * internal count units. During this time the CPU is used by other
 * tasks.
 * 
 * @param delay Corresponds to the time the task is going to be suspended.
 *
 * See also: @ref rt_busy_sleep(), @ref rt_sleep_until().
 *
 * @note A higher priority task or interrupt handler can run before
 *	 the task goes to sleep, so the actual time spent in these
 *	 functions may be longer than the the one specified.
 */
void rt_sleep(RTIME delay)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;
	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((rt_current->resume_time = get_time() + delay) > rt_time_h) {
		rt_current->state |= RT_SCHED_DELAYED;
		rem_ready_current(rt_current);
		enq_timed_task(rt_current);
		rt_schedule();
	}
	rt_global_restore_flags(flags);
}

/**
 * @anchor rt_sleep_until
 * @brief Delay/suspend execution for a while.
 *
 * rt_sleep_until is similar to @ref rt_sleep() but the parameter time
 * is the absolute time till the task have to be suspended. If the
 * given time is already passed this call has no effect.
 * 
 * @param time Absolute time till the task have to be suspended
 *
 * See also: @ref rt_busy_sleep(), @ref rt_sleep_until().
 *
 * @note A higher priority task or interrupt handler can run before
 *	 the task goes to sleep, so the actual time spent in these
 *	 functions may be longer than the the one specified.
 */
void rt_sleep_until(RTIME time)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;
	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((rt_current->resume_time = time) > rt_time_h) {
		rt_current->state |= RT_SCHED_DELAYED;
		rem_ready_current(rt_current);
		enq_timed_task(rt_current);
		rt_schedule();
	}
	rt_global_restore_flags(flags);
}

int rt_task_wakeup_sleeping(RT_TASK *task)
{
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return -EINVAL;
	}

	flags = rt_global_save_flags_and_cli();
	rem_timed_task(task);
	if (task->state != RT_SCHED_READY && (task->state &= ~RT_SCHED_DELAYED) == RT_SCHED_READY) {
		enq_ready_task(task);
		RT_SCHEDULE(task, hard_cpu_id());
	}
	rt_global_restore_flags(flags);
	return 0;
}

int rt_nanosleep(struct timespec *rqtp, struct timespec *rmtp)
{
	RTIME expire;

	if (rqtp->tv_nsec >= 1000000000L || rqtp->tv_nsec < 0 || rqtp->tv_sec < 0) {
		return -EINVAL;
	}
	rt_sleep_until(expire = rt_get_time() + timespec2count(rqtp));
	if ((expire -= rt_get_time()) > 0) {
		if (rmtp) {
			count2timespec(expire, rmtp);
		}
		return -EINTR;
	}
	return 0;
}

/* +++++++++++++++++++ READY AND TIMED QUEUE MANIPULATION +++++++++++++++++++ */

void rt_enq_ready_edf_task(RT_TASK *ready_task)
{
	enq_ready_edf_task(ready_task);
}

void rt_enq_ready_task(RT_TASK *ready_task)
{
	enq_ready_task(ready_task);
}

int rt_renq_ready_task(RT_TASK *ready_task, int priority)
{
	return renq_ready_task(ready_task, priority);
}

void rt_rem_ready_task(RT_TASK *task)
{
	rem_ready_task(task);
}

void rt_rem_ready_current(RT_TASK *rt_current)
{
	rem_ready_current(rt_current);
}

void rt_enq_timed_task(RT_TASK *timed_task)
{
	enq_timed_task(timed_task);
}

void rt_wake_up_timed_tasks(int cpuid)
{
#ifdef CONFIG_SMP
	wake_up_timed_tasks(cpuid & sqilter);
#else
        wake_up_timed_tasks(0);
#endif
}

void rt_rem_timed_task(RT_TASK *task)
{
	rem_timed_task(task);
}

void rt_enqueue_blocked(RT_TASK *task, QUEUE *queue, int qtype)
{
	enqueue_blocked(task, queue, qtype);
}

void rt_dequeue_blocked(RT_TASK *task)
{
	dequeue_blocked(task);
}

int rt_renq_current(RT_TASK *rt_current, int priority)
{
	return renq_current(rt_current, priority);
}

/* ++++++++++++++++++++++++ NAMED TASK INIT/DELETE ++++++++++++++++++++++++++ */

RT_TASK *rt_named_task_init(const char *task_name, void (*thread)(int), int data, int stack_size, int prio, int uses_fpu, void(*signal)(void))
{
	RT_TASK *task;
	unsigned long name;

	if ((task = rt_get_adr(name = nam2num(task_name)))) {
		return task;
	}
        if ((task = rt_malloc(sizeof(RT_TASK))) && !rt_task_init(task, thread, data, stack_size, prio, uses_fpu, signal)) {
		if (rt_register(name, task, IS_TASK, 0)) {
			return task;
		}
		rt_task_delete(task);
	}
	rt_free(task);
	return (RT_TASK *)0;
}

RT_TASK *rt_named_task_init_cpuid(const char *task_name, void (*thread)(int), int data, int stack_size, int prio, int uses_fpu, void(*signal)(void), unsigned int run_on_cpu)
{
	RT_TASK *task;
	unsigned long name;

	if ((task = rt_get_adr(name = nam2num(task_name)))) {
		return task;
	}
        if ((task = rt_malloc(sizeof(RT_TASK))) && !rt_task_init_cpuid(task, thread, data, stack_size, prio, uses_fpu, signal, run_on_cpu)) {
		if (rt_register(name, task, IS_TASK, 0)) {
			return task;
		}
		rt_task_delete(task);
	}
	rt_free(task);
	return (RT_TASK *)0;
}

int rt_named_task_delete(RT_TASK *task)
{
	if (!rt_task_delete(task)) {
		rt_free(task);
	}
	return rt_drg_on_adr(task);
}

/* +++++++++++++++++++++++++++++++ REGISTRY +++++++++++++++++++++++++++++++++ */

static volatile int max_slots;
static struct rt_registry_entry_struct lxrt_list[MAX_SLOTS + 1] = { { 0, 0, 0, 0, 0 }, };
static spinlock_t list_lock = SPIN_LOCK_UNLOCKED;

static inline int registr(unsigned long name, void *adr, int type, struct task_struct *tsk)
{
        unsigned long flags;
        int i, slot;
/*
 * Register a resource. This allows other programs (RTAI and/or user space)
 * to use the same resource because they can find the address from the name.
*/
        // index 0 is reserved for the null slot.
	while ((slot = max_slots) < MAX_SLOTS) {
        	for (i = 1; i <= max_slots; i++) {
                	if (lxrt_list[i].name == name) {
				return 0;
			}
		}
        	flags = rt_spin_lock_irqsave(&list_lock);
                if (slot == max_slots && max_slots < MAX_SLOTS) {
			slot = ++max_slots;
                        lxrt_list[slot].name  = name;
                        lxrt_list[slot].adr   = adr;
                        lxrt_list[slot].tsk   = tsk;
                        lxrt_list[slot].pid   = tsk ? tsk->pid : 0 ;
                        lxrt_list[slot].type  = type;
                        lxrt_list[slot].count = 1;
                        rt_spin_unlock_irqrestore(flags, &list_lock);
                        return slot;
                }
        	rt_spin_unlock_irqrestore(flags, &list_lock);
        }
        return 0;
}

static inline int drg_on_name(unsigned long name)
{
	unsigned long flags;
	int slot;
	for (slot = 1; slot <= max_slots; slot++) {
		flags = rt_spin_lock_irqsave(&list_lock);
		if (lxrt_list[slot].name == name) {
			if (slot < max_slots) {
				lxrt_list[slot] = lxrt_list[max_slots];
			}
			if (max_slots > 0) {
				max_slots--;
			}
			rt_spin_unlock_irqrestore(flags, &list_lock);
			return slot;
		}
		rt_spin_unlock_irqrestore(flags, &list_lock);
	}
	return 0;
} 

static inline int drg_on_name_cnt(unsigned long name)
{
	unsigned long flags;
	int slot, count;
	for (slot = 1; slot <= max_slots; slot++) {
		flags = rt_spin_lock_irqsave(&list_lock);
		if (lxrt_list[slot].name == name && lxrt_list[slot].count > 0 && !(count = --lxrt_list[slot].count)) {
			if (slot < max_slots) {
				lxrt_list[slot] = lxrt_list[max_slots];
			}
			if (max_slots > 0) {
				max_slots--;
			}
			rt_spin_unlock_irqrestore(flags, &list_lock);
			return count;
		}
		rt_spin_unlock_irqrestore(flags, &list_lock);
	}
	return -EFAULT;
} 

static inline int drg_on_adr(void *adr)
{
	unsigned long flags;
	int slot;
	for (slot = 1; slot <= max_slots; slot++) {
		flags = rt_spin_lock_irqsave(&list_lock);
		if (lxrt_list[slot].adr == adr) {
			if (slot < max_slots) {
				lxrt_list[slot] = lxrt_list[max_slots];
			}
			if (max_slots > 0) {
				max_slots--;
			}
			rt_spin_unlock_irqrestore(flags, &list_lock);
			return slot;
		}
		rt_spin_unlock_irqrestore(flags, &list_lock);
	}
	return 0;
} 

static inline int drg_on_adr_cnt(void *adr)
{
	unsigned long flags;
	int slot, count;
	for (slot = 1; slot <= max_slots; slot++) {
		flags = rt_spin_lock_irqsave(&list_lock);
		if (lxrt_list[slot].adr == adr && lxrt_list[slot].count > 0 && !(count = --lxrt_list[slot].count)) {
			if (slot < max_slots) {
				lxrt_list[slot] = lxrt_list[max_slots];
			}
			if (max_slots > 0) {
				max_slots--;
			}
			rt_spin_unlock_irqrestore(flags, &list_lock);
			return count;
		}
		rt_spin_unlock_irqrestore(flags, &list_lock);
	}
	return -EFAULT;
} 

static inline unsigned long get_name(void *adr)
{
	static unsigned long nameseed = 0xfacade;
	int slot;
        if (!adr) {
		unsigned long flags;
		unsigned long name;
		flags = rt_spin_lock_irqsave(&list_lock);
		name = nameseed++;
		rt_spin_unlock_irqrestore(flags, &list_lock);
		return name;
        }
	for (slot = 1; slot <= max_slots; slot++) {
		if (lxrt_list[slot].adr == adr) {
			return lxrt_list[slot].name;
		}
	}
	return 0;
} 

static inline void *get_adr(unsigned long name)
{
	int slot;
	for (slot = 1; slot <= max_slots; slot++) {
		if (lxrt_list[slot].name == name) {
			return lxrt_list[slot].adr;
		}
	}
	return 0;
} 

static inline void *get_adr_cnt(unsigned long name)
{
	unsigned long flags;
	int slot;
	for (slot = 1; slot <= max_slots; slot++) {
		flags = rt_spin_lock_irqsave(&list_lock);
		if (lxrt_list[slot].name == name) {
			++lxrt_list[slot].count;
			rt_spin_unlock_irqrestore(flags, &list_lock);
			return lxrt_list[slot].adr;
		}
		rt_spin_unlock_irqrestore(flags, &list_lock);
	}
	return 0;
} 

static inline int get_type(unsigned long name)
{
        int slot;
        for (slot = 1; slot <= max_slots; slot++) {
                if (lxrt_list[slot].name == name) {
                        return lxrt_list[slot].type;
                }
        }
        return -EINVAL;
}

unsigned long is_process_registered(struct task_struct *tsk)
{
	int slot;
	for (slot = 1; slot <= max_slots; slot++) {
		if (lxrt_list[slot].tsk == tsk) {
			if (lxrt_list[slot].pid == (tsk ? tsk->pid : 0)) {
				return lxrt_list[slot].name;
			}
                }
        }
        return 0;
}

/**
 * @ingroup lxrt
 * Register an object.
 *
 * rt_register registers the object to be identified with @a name, which is
 * pointed by @a adr.
 *
 * @return a positive number on success, 0 on failure.
 */
int rt_register(unsigned long name, void *adr, int type, struct task_struct *t)
{
/*
 * Register a resource. This function provides the service to all RTAI tasks.
*/
	return get_adr(name) ? 0 : registr(name, adr, type, t );
}


/**
 * @ingroup lxrt
 * Deregister an object by its name.
 *
 * rt_drg_on_name deregisters the object identified by its @a name.
 *
 * @return a positive number on success, 0 on failure.
 */
int rt_drg_on_name(unsigned long name)
{
	return drg_on_name(name);
} 

/**
 * @ingroup lxrt
 * Deregister an object by its address.
 *
 * rt_drg_on_adr deregisters the object identified by its @a adr.
 *
 * @return a positive number on success, 0 on failure.
 */
int rt_drg_on_adr(void *adr)
{
	return drg_on_adr(adr);
} 

unsigned long rt_get_name(void *adr)
{
	return get_name(adr);
} 

void *rt_get_adr(unsigned long name)
{
	return get_adr(name);
}

int rt_get_type(unsigned long name)
{
	return get_type(name);
}

int rt_drg_on_name_cnt(unsigned long name)
{
	return drg_on_name_cnt(name);
}

int rt_drg_on_adr_cnt(void *adr)
{
	return drg_on_adr_cnt(adr);
}

void *rt_get_adr_cnt(unsigned long name)
{
	return get_adr_cnt(name);
}

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
void rtai_handle_isched_lock (int nesting) /* Called with interrupts off */

{
    if (nesting == 0)		/* Leaving interrupt context (inner one processed) */
	rt_sched_unlock();
    else
	rt_sched_lock();	/* Entering interrupt context */
}
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

#include <rtai_lxrt.h>

extern struct rt_fun_entry rt_fun_lxrt[];

void krtai_objects_release(void)
{
	int slot;
        struct rt_registry_entry_struct entry;
	char name[8], *type;

	for (slot = 1; slot <= max_slots; slot++) {
                if (rt_get_registry_slot(slot, &entry) && entry.adr) {
			switch (entry.type) {
	                       	case IS_TASK:
					type = "TASK";
					rt_named_task_delete(entry.adr);
					break;
				case IS_SEM:
					type = "SEM ";
					((void (*)(void *))rt_fun_lxrt[NAMED_SEM_DELETE].fun)(entry.adr);
					break;
				case IS_RWL:
					type = "RWL ";
					((void (*)(void *))rt_fun_lxrt[NAMED_RWL_DELETE].fun)(entry.adr);
					break;
				case IS_SPL:
					type = "SPL ";
					((void (*)(void *))rt_fun_lxrt[NAMED_SPL_DELETE].fun)(entry.adr);
					break;
				case IS_MBX:
					type = "MBX ";
					((void (*)(void *))rt_fun_lxrt[NAMED_MBX_DELETE].fun)(entry.adr);
	                       		break;	
				case IS_PRX:
					type = "PRX ";
					((void (*)(void *))rt_fun_lxrt[PROXY_DETACH].fun)(entry.adr);
					rt_drg_on_adr(entry.adr); 
					break;
	                       	default:
					type = "ALIEN";
					break;
			}
			num2nam(entry.name, name);
			rt_printk("SCHED releases registered named %s %s\n", type, name);
		}
	}
}

/* ++++++++++++++++++++ END OF COMMON FUNCTIONALITIES +++++++++++++++++++++++ */

#ifdef CONFIG_PROC_FS
#include <linux/stat.h>
#include <linux/proc_fs.h>
#include <rtai_proc_fs.h>
#include <rtai_nam2num.h>

extern struct proc_dir_entry *rtai_proc_root;

int rt_get_registry_slot(int slot, struct rt_registry_entry_struct* entry)
{
	unsigned long flags;

	if(entry == 0) {
		return 0;
	}
	flags = rt_spin_lock_irqsave(&list_lock);
	if (slot > 0 && slot <= max_slots ) {
		if (lxrt_list[slot].name != 0) {
			*entry = lxrt_list[slot];
			rt_spin_unlock_irqrestore(flags, &list_lock);
			return slot;
		}
	}
	rt_spin_unlock_irqrestore(flags, &list_lock);

	return 0;
}

/* ----------------------< proc filesystem section >----------------------*/

static int rtai_read_lxrt(char *page, char **start, off_t off, int count, int *eof, void *data)
{
	PROC_PRINT_VARS;
	struct rt_registry_entry_struct entry;
	char *type_name[] = { "TASK", "SEM", "RWL", "SPL", "MBX", "PRX", "BITS", "TBX", "HPCK" };
	unsigned int i = 1;
	char name[8];

	PROC_PRINT("\nRTAI LXRT Information.\n\n");
	PROC_PRINT("    MAX_SLOTS = %d\n\n", MAX_SLOTS);

//                  1234 123456 0x12345678 ALIEN  0x12345678 0x12345678   1234567      1234567

	PROC_PRINT("                                         Linux_Owner         Parent PID\n");
	PROC_PRINT("Slot Name   ID         Type   RT_Handle    Pointer   Tsk_PID   MEM_Sz   USG Cnt\n");
	PROC_PRINT("-------------------------------------------------------------------------------\n");
	for (i = 1; i <= max_slots; i++) {
		if (rt_get_registry_slot(i, &entry)) {
			num2nam(entry.name, name);
			PROC_PRINT("%4d %-6.6s 0x%08lx %-6.6s 0x%p 0x%p  %7d   %8d %7d\n",
			i,    			// the slot number
			name,       		// the name in 6 char asci
			entry.name, 		// the name as unsigned long hex
			entry.type >= PAGE_SIZE ? "SHMEM" : 
			entry.type > sizeof(type_name)/sizeof(char *) ? 
			"ALIEN" : 
			type_name[entry.type],	// the Type
			entry.adr,		// The RT Handle
			entry.tsk,   		// The Owner task pointer
			entry.pid,   		// The Owner PID
			entry.type == IS_TASK && ((RT_TASK *)entry.adr)->lnxtsk ? (((RT_TASK *)entry.adr)->lnxtsk)->pid : entry.type >= PAGE_SIZE ? entry.type : 0, entry.count);
		 }
	}
        PROC_PRINT_DONE;
}  /* End function - rtai_read_lxrt */

int rtai_proc_lxrt_register(void)
{
	struct proc_dir_entry *proc_lxrt_ent;


	proc_lxrt_ent = create_proc_entry("RTAI names", S_IFREG|S_IRUGO|S_IWUSR, rtai_proc_root);
	if (!proc_lxrt_ent) {
		printk("Unable to initialize /proc/rtai/lxrt\n");
		return(-1);
	}
	proc_lxrt_ent->read_proc = rtai_read_lxrt;
	return(0);
}  /* End function - rtai_proc_lxrt_register */


void rtai_proc_lxrt_unregister(void)
{
	remove_proc_entry("RTAI names", rtai_proc_root);
}  /* End function - rtai_proc_lxrt_unregister */

/* ------------------< end of proc filesystem section >------------------*/
#endif /* CONFIG_PROC_FS */

#ifdef CONFIG_KBUILD

EXPORT_SYMBOL(rt_set_sched_policy);
EXPORT_SYMBOL(rt_get_prio);
EXPORT_SYMBOL(rt_get_inher_prio);
EXPORT_SYMBOL(rt_change_prio);
EXPORT_SYMBOL(rt_whoami);
EXPORT_SYMBOL(rt_task_yield);
EXPORT_SYMBOL(rt_task_suspend);
EXPORT_SYMBOL(rt_task_resume);
EXPORT_SYMBOL(rt_get_task_state);
EXPORT_SYMBOL(rt_linux_use_fpu);
EXPORT_SYMBOL(rt_task_use_fpu);
EXPORT_SYMBOL(rt_task_signal_handler);
EXPORT_SYMBOL(rt_gettimeorig);
EXPORT_SYMBOL(rt_task_make_periodic_relative_ns);
EXPORT_SYMBOL(rt_task_make_periodic);
EXPORT_SYMBOL(rt_task_wait_period);
EXPORT_SYMBOL(rt_task_set_resume_end_times);
EXPORT_SYMBOL(rt_set_resume_time);
EXPORT_SYMBOL(rt_set_period);
EXPORT_SYMBOL(next_period);
EXPORT_SYMBOL(rt_busy_sleep);
EXPORT_SYMBOL(rt_sleep);
EXPORT_SYMBOL(rt_sleep_until);
EXPORT_SYMBOL(rt_task_wakeup_sleeping);
EXPORT_SYMBOL(rt_nanosleep);
EXPORT_SYMBOL(rt_enq_ready_edf_task);
EXPORT_SYMBOL(rt_enq_ready_task);
EXPORT_SYMBOL(rt_renq_ready_task);
EXPORT_SYMBOL(rt_rem_ready_task);
EXPORT_SYMBOL(rt_rem_ready_current);
EXPORT_SYMBOL(rt_enq_timed_task);
EXPORT_SYMBOL(rt_wake_up_timed_tasks);
EXPORT_SYMBOL(rt_rem_timed_task);
EXPORT_SYMBOL(rt_enqueue_blocked);
EXPORT_SYMBOL(rt_dequeue_blocked);
EXPORT_SYMBOL(rt_renq_current);
EXPORT_SYMBOL(rt_named_task_init);
EXPORT_SYMBOL(rt_named_task_init_cpuid);
EXPORT_SYMBOL(rt_named_task_delete);
EXPORT_SYMBOL(is_process_registered);
EXPORT_SYMBOL(rt_register);
EXPORT_SYMBOL(rt_drg_on_name);
EXPORT_SYMBOL(rt_drg_on_adr);
EXPORT_SYMBOL(rt_get_name);
EXPORT_SYMBOL(rt_get_adr);
EXPORT_SYMBOL(rt_get_type);
EXPORT_SYMBOL(rt_drg_on_name_cnt);
EXPORT_SYMBOL(rt_drg_on_adr_cnt);
EXPORT_SYMBOL(rt_get_adr_cnt);
EXPORT_SYMBOL(rt_get_registry_slot);

EXPORT_SYMBOL(rt_task_init);
EXPORT_SYMBOL(rt_task_init_cpuid);
EXPORT_SYMBOL(rt_set_runnable_on_cpus);
EXPORT_SYMBOL(rt_set_runnable_on_cpuid);
EXPORT_SYMBOL(rt_check_current_stack);
EXPORT_SYMBOL(rt_schedule);
EXPORT_SYMBOL(rt_spv_RMS);
EXPORT_SYMBOL(rt_sched_lock);
EXPORT_SYMBOL(rt_sched_unlock);
EXPORT_SYMBOL(rt_task_delete);
EXPORT_SYMBOL(rt_is_hard_timer_running);
EXPORT_SYMBOL(rt_set_periodic_mode);
EXPORT_SYMBOL(rt_set_oneshot_mode);
EXPORT_SYMBOL(rt_get_timer_cpu);
EXPORT_SYMBOL(start_rt_timer);
EXPORT_SYMBOL(stop_rt_timer);
EXPORT_SYMBOL(start_rt_timer_cpuid);
EXPORT_SYMBOL(start_rt_apic_timers);
EXPORT_SYMBOL(rt_sched_type);
EXPORT_SYMBOL(rt_preempt_always);
EXPORT_SYMBOL(rt_preempt_always_cpuid);
EXPORT_SYMBOL(rt_set_task_trap_handler);
EXPORT_SYMBOL(rt_get_time);
EXPORT_SYMBOL(rt_get_time_cpuid);
EXPORT_SYMBOL(rt_get_time_ns);
EXPORT_SYMBOL(rt_get_time_ns_cpuid);
EXPORT_SYMBOL(rt_get_cpu_time_ns);
EXPORT_SYMBOL(rt_get_base_linux_task);
EXPORT_SYMBOL(rt_alloc_dynamic_task);
EXPORT_SYMBOL(rt_register_watchdog);
EXPORT_SYMBOL(rt_deregister_watchdog);
EXPORT_SYMBOL(count2nano);
EXPORT_SYMBOL(nano2count);
EXPORT_SYMBOL(count2nano_cpuid);
EXPORT_SYMBOL(nano2count_cpuid);

EXPORT_SYMBOL(rt_kthread_init);
EXPORT_SYMBOL(rt_smp_linux_task);
EXPORT_SYMBOL(rt_smp_current);
EXPORT_SYMBOL(rt_smp_time_h);
EXPORT_SYMBOL(rt_smp_oneshot_timer);
EXPORT_SYMBOL(wake_up_srq);
EXPORT_SYMBOL(set_rt_fun_entries);
EXPORT_SYMBOL(reset_rt_fun_entries);
EXPORT_SYMBOL(set_rt_fun_ext_index);
EXPORT_SYMBOL(reset_rt_fun_ext_index);

#ifdef CONFIG_SMP
EXPORT_SYMBOL(sqilter);
#endif /* CONFIG_SMP */

#endif /* CONFIG_KBUILD */
