/** 
 * @file
 * Semaphore functions.
 * @author Paolo Mantegazza
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
 * @ingroup sem
 */

/**
 * @ingroup sched
 * @defgroup sem Semaphore functions
 *
 *@{*/

#include <rtai_schedcore.h>
#include <rtai_sem.h>
#include <rtai_rwl.h>
#include <rtai_spl.h>

MODULE_LICENSE("GPL");

/* +++++++++++++++++++++ ALL SEMAPHORES TYPES SUPPORT +++++++++++++++++++++++ */

/**
 * @anchor rt_typed_sem_init
 * @brief Initialize a specifically typed (counting, binary, resource)
 *	  semaphore
 *
 * rt_typed_sem_init initializes a semaphore @e sem of type @e type. A
 * semaphore can be used for communication and synchronization among
 * real time tasks. Negative value of a semaphore shows how many tasks
 * are blocked on the semaphore queue, waiting to be awaken by calls
 * to rt_sem_signal.
 *
 * @param sem must point to an allocated SEM structure.
 *
 * @param value is the initial value of the semaphore, always set to 1
 *	  for a resource semaphore.
 *
 * @param type is the semaphore type and queuing policy. It can be an OR
 * a semaphore kind: CNT_SEM for counting semaphores, BIN_SEM for binary 
 * semaphores, RES_SEM for resource semaphores; and queuing policy:
 * FIFO_Q, PRIO_Q for a fifo and priority queueing respectively.
 * Resource semaphores will enforce a PRIO_Q policy anyhow.
 * 
 * Counting semaphores can register up to 0xFFFE events. Binary
 * semaphores do not count signalled events, their count will never
 * exceed 1 whatever number of events is signaled to them. Resource
 * semaphores are special binary semaphores suitable for managing
 * resources. The task that acquires a resource semaphore becomes its
 * owner, also called resource owner, since it is the only one capable
 * of manipulating the resource the semaphore is protecting. The owner
 * has its priority increased to that of any task blocking on a wait
 * to the semaphore. Such a feature, called priority inheritance,
 * ensures that a high priority task is never slaved to a lower
 * priority one, thus allowing to avoid any deadlock due to priority
 * inversion. Resource semaphores can be recursed, i.e. their task
 * owner is not blocked by nested waits placed on an owned
 * resource. The owner must insure that it will signal the semaphore,
 * in reversed order, as many times as he waited on it. Note that that
 * full priority inheritance is supported both for resource semaphores
 * and inter task messages, for a singly owned resource. Instead it
 * becomes an adaptive priority ceiling when a task owns multiple
 * resources, including messages sent to him. In such a case in fact
 * its priority is returned to its base one only when all such
 * resources are released and no message is waiting for being
 * received. This is a compromise design choice aimed at avoiding
 * extensive searches for the new priority to be inherited across
 * multiply owned resources and blocked tasks sending messages to
 * him. Such a solution will be implemented only if it proves
 * necessary. Note also that, to avoid @e deadlocks, a task owning a
 * resource semaphore cannot be suspended. Any @ref rt_task_suspend()
 * posed on it is just registered. An owner task will go into suspend
 * state only when it releases all the owned resources.
 *
 * @note RTAI counting semaphores assume that their counter will never
 *	 exceed 0xFFFF, such a number being used to signal returns in
 *	 error. Thus also the initial count value cannot be greater
 *	 than 0xFFFF. To be used only with RTAI24.x.xx (FIXME).
 */
void rt_typed_sem_init(SEM *sem, int value, int type)
{
	sem->magic = RT_SEM_MAGIC;
	sem->count = value;
	if ((type & RES_SEM) == RES_SEM) {
		sem->qtype = 0;
	} else {
		sem->qtype = (type & FIFO_Q) ? 1 : 0;
	}
	type = (type & 3) - 2;
	if ((sem->type = type) < 0 && value > 1) {
		sem->count = 1;
	} else if (type > 0) {
		sem->type = sem->count = 1;
	}
	sem->queue.prev = &(sem->queue);
	sem->queue.next = &(sem->queue);
	sem->queue.task = sem->owndby = 0;
}


/**
 * @anchor rt_sem_init
 * @brief Initialize a counting semaphore.
 *
 * rt_sem_init initializes a counting fifo queueing semaphore @e sem.
 *
 * A semaphore can be used for communication and synchronization among
 * real time tasks.
 *
 * @param sem must point to an allocated @e SEM structure.
 *
 * @param value is the initial value of the semaphore.
 * 
 * Positive values of the semaphore variable show how many tasks can
 * do a @ref rt_sem_wait() call without blocking. Negative value of a
 * semaphore shows how many tasks are blocked on the semaphore queue,
 * waiting to be awaken by calls to @ref rt_sem_signal().
 *
 * @note RTAI counting semaphores assume that their counter will never
 *	 exceed 0xFFFF, such a number being used to signal returns in
 *	 error. Thus also the initial count value cannot be greater
 *	 than 0xFFFF.
 *	 This is an old legacy function. RTAI 24.1.xx has also 
 *	 @ref rt_typed_sem_init(), allowing to
 *	 choose among counting, binary and resource
 *	 semaphores. Resource semaphores have priority inherithance. 
 */
void rt_sem_init(SEM *sem, int value)
{
	rt_typed_sem_init(sem, value, CNT_SEM);
}


/**
 * @anchor rt_sem_delete
 * @brief Delete a semaphore
 *
 * rt_sem_delete deletes a semaphore previously created with 
 * @ref rt_sem_init(). 
 *
 * @param sem points to the structure used in the corresponding
 * call to rt_sem_init. 
 *
 * Any tasks blocked on this semaphore is returned in error and
 * allowed to run when semaphore is destroyed. 
 *
 * @return 0 is returned upon success. A negative value is returned on
 * failure as described below: 
 * - @b 0xFFFF: @e sem does not refer to a valid semaphore.
 *
 * @note In principle 0xFFFF could theoretically be a usable
 *	 semaphores events count, so it could be returned also under
 *	 normal circumstances. It is unlikely you are going to count
 *	 up to such number of events, in any case avoid counting up 
 *	 to 0xFFFF. 
 */
int rt_sem_delete(SEM *sem)
{
	unsigned long flags;
	RT_TASK *task;
	unsigned long schedmap;
	QUEUE *q;

	if (sem->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}

	schedmap = 0;
	q = &(sem->queue);
	flags = rt_global_save_flags_and_cli();
	sem->magic = 0;
	while ((q = q->next) != &(sem->queue) && (task = q->task)) {
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEMAPHORE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			set_bit(task->runnable_on_cpus & 0x1F, &schedmap);
		}
	}
	clear_bit(hard_cpu_id(), &schedmap);
	if ((task = sem->owndby) && sem->type > 0) {
		int sched;
		if (task->owndres & SEMHLF) {
			--task->owndres;
		}
		if (!task->owndres) {
			sched = renq_ready_task(task, task->base_priority);
		} else if (!(task->owndres & SEMHLF)) {
			int priority;
                        sched = renq_ready_task(task, task->base_priority > (priority = ((task->msg_queue.next)->task)->priority) ? priority : task->base_priority);
		} else {
			sched = 0;
		}
		if (task->suspdepth) {
			if (task->suspdepth > 0) {
				task->state |= RT_SCHED_SUSPENDED;
				rem_ready_task(task);
				sched = 1;
			} else {
				rt_task_delete(task);
			}
		}
		if (sched) {
			if (schedmap) {
				RT_SCHEDULE_MAP_BOTH(schedmap);
			} else {
				rt_schedule();
			}
		}
	} else {
		RT_SCHEDULE_MAP(schedmap);
	}
	rt_global_restore_flags(flags);
	return 0;
}


int rt_sem_count(SEM *sem)
{
	return sem->count;
}


/**
 * @anchor rt_sem_signal
 * @brief Signaling a semaphore.
 *
 * rt_sem_signal signals an event to a semaphore. It is typically
 * called when the task leaves a critical region. The semaphore value
 * is incremented and tested. If the value is not positive, the first
 * task in semaphore's waiting queue is allowed to run.  rt_sem_signal
 * never blocks the caller task.
 *
 * @param sem points to the structure used in the call to @ref
 * rt_sem_init().
 * 
 * @return 0 is returned upon success. A negative value is returned on
 * failure as described below: 
 * - @b 0xFFFF: @e sem does not refer to a valid semaphore.
 *
 * @note In principle 0xFFFF could theoretically be a usable
 *	 semaphores events count, so it could be returned also under
 *	 normal circumstances. It is unlikely you are going to count
 *	 up to such number of events, in any case avoid counting up to
 * 	 0xFFFF.
 *	 See @ref rt_sem_wait() notes for some curiosities.
 */
int rt_sem_signal(SEM *sem)
{
	unsigned long flags;
	RT_TASK *task;
	int tosched;

	if (sem->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	if (sem->type) {
		if (sem->type > 1) {
			sem->type--;
			rt_global_restore_flags(flags);
			return 0;
		}
		if (++sem->count > 1) {
			sem->count = 1;
		}
	} else {
		sem->count++;
	}
	if ((task = (sem->queue.next)->task)) {
		dequeue_blocked(task);
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEMAPHORE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			if (sem->type <= 0) {
				RT_SCHEDULE(task, hard_cpu_id());
				rt_global_restore_flags(flags);
				return 0;
			}
			tosched = 1;
			goto res;
		}
	}
	tosched = 0;
res:	if (sem->type > 0) {
		DECLARE_RT_CURRENT;
		int sched;
		ASSIGN_RT_CURRENT;
		sem->owndby = 0;
		if (rt_current->owndres & SEMHLF) {
			--rt_current->owndres;
		}
		if (!rt_current->owndres) {
			sched = renq_current(rt_current, rt_current->base_priority);
		} else if (!(rt_current->owndres & SEMHLF)) {
			int priority;
			sched = renq_current(rt_current, rt_current->base_priority > (priority = ((rt_current->msg_queue.next)->task)->priority) ? priority : rt_current->base_priority);
		} else {
			sched = 0;
		}
		if (rt_current->suspdepth) {
			if (rt_current->suspdepth > 0) {
				rt_current->state |= RT_SCHED_SUSPENDED;
				rem_ready_current(rt_current);
                        	sched = 1;
			} else {
				rt_task_delete(rt_current);
			}
		}
		if (sched) {
			if (tosched) {
				RT_SCHEDULE_BOTH(task, cpuid);
			} else {
				rt_schedule();
			}
		} else if (tosched) {
			RT_SCHEDULE(task, cpuid);
		}
	}
	rt_global_restore_flags(flags);
	return 0;
}


/**
 * @anchor rt_sem_broadcast
 * @brief Signaling a semaphore.
 *
 * rt_sem_broadcast signals an event to a semaphore that unblocks all tasks
 * waiting on it. It is used as a support for RTAI proper conditional 
 * variables but can be of help in many other instances. After the broadcast
 * the semaphore counts is set to zero, thus all tasks waiting on it will
 * blocked.
 *
 * @param sem points to the structure used in the call to @ref
 * rt_sem_init().
 * 
 * @returns 0 always.
 */
int rt_sem_broadcast(SEM *sem)
{
	unsigned long flags, schedmap;
	RT_TASK *task;
	QUEUE *q;

	if (sem->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}
	schedmap = 0;
	q = &(sem->queue);
	flags = rt_global_save_flags_and_cli();
	while ((q = q->next) != &(sem->queue)) {
		dequeue_blocked(task = q->task);
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEMAPHORE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			set_bit(task->runnable_on_cpus & 0x1F, &schedmap);
		}
		rt_global_restore_flags(flags);
		flags = rt_global_save_flags_and_cli();
	}
	sem->count = 0;
	if (schedmap) {
		if (test_and_clear_bit(hard_cpu_id(), &schedmap)) {
			RT_SCHEDULE_MAP_BOTH(schedmap);
		} else {
			RT_SCHEDULE_MAP(schedmap);
		}
	}
	rt_global_restore_flags(flags);
	return 0;
}


/**
 * @anchor rt_sem_wait
 * @brief Take a semaphore.
 *
 * rt_sem_wait waits for a event to be signaled to a semaphore. It is
 * typically called when a task enters a critical region. The
 * semaphore value is decremented and tested. If it is still
 * non-negative rt_sem_wait returns immediately. Otherwise the caller
 * task is blocked and queued up. Queuing may happen in priority order
 * or on FIFO base. This is determined by the compile time option @e
 * SEM_PRIORD. In this case rt_sem_wait returns if:
 *	       - The caller task is in the first place of the waiting
 *		 queue and another task issues a @ref rt_sem_signal()
 *		 call;
 *	       - An error occurs (e.g. the semaphore is destroyed);
 *
 * @param sem points to the structure used in the call to @ref
 *	  rt_sem_init().
 *
 * @return the number of events already signaled upon success.
 * A special value" as described below in case of a failure :
 * - @b 0xFFFF: @e sem does not refer to a valid semaphore.
 *
 * @note In principle 0xFFFF could theoretically be a usable
 *	 semaphores events count, so it could be returned also under
 *	 normal circumstances. It is unlikely you are going to count
 *	 up to such number of events, in any case avoid counting up to
 *	 0xFFFF.<br>
 *	 Just for curiosity: the original Dijkstra notation for
 *	 rt_sem_wait was a "P" operation, and rt_sem_signal was a "V"
 *	 operation. The name for P comes from the Dutch "prolagen", a
 *	 combination of "proberen" (to probe) and "verlagen" (to
 *	 decrement). Also from the word "passeren" (to pass).<br>
 *	 The name for V comes from the Dutch "verhogen" (to increase)
 *	 or "vrygeven" (to release).  (Source: Daniel Tabak -
 *	 Multiprocessors, Prentice Hall, 1990).<br>
 *	 It should be also remarked that real time programming
 *	 practitioners were using semaphores a long time before
 *	 Dijkstra formalized P and V. "In Italian semaforo" means a
 *	 traffic light, so that semaphores have an intuitive appeal
 * 	 and their use and meaning is easily understood.
 */
int rt_sem_wait(SEM *sem)
{
	RT_TASK *rt_current;
	unsigned long flags;
	int count;

	if (sem->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	rt_current = RT_CURRENT;
	if ((count = sem->count) <= 0) {
		unsigned long schedmap;
		if (sem->type > 0) {
			if (sem->owndby == rt_current) {
				sem->type++;
				rt_global_restore_flags(flags);
				return count;
			}
			schedmap = pass_prio(sem->owndby, rt_current);
		} else {
			schedmap = 0;
		}
		sem->count--;
		rt_current->state |= RT_SCHED_SEMAPHORE;
		rem_ready_current(rt_current);
		enqueue_blocked(rt_current, &sem->queue, sem->qtype);
		RT_SCHEDULE_MAP_BOTH(schedmap);
		if (rt_current->blocked_on || sem->magic != RT_SEM_MAGIC) {
			rt_current->prio_passed_to = NOTHING;
			rt_global_restore_flags(flags);
			return SEM_ERR;
		} else { 
			count = sem->count;
		}
	} else {
		sem->count--;
	}
	if (sem->type > 0) {
		(sem->owndby = rt_current)->owndres++;
	}
	rt_global_restore_flags(flags);
	return count;
}


/**
 * @anchor rt_sem_wait_if
 * @brief Take a semaphore, only if the calling task is not blocked.
 *
 * rt_sem_wait_if is a version of the semaphore wait operation is
 * similar to @ref rt_sem_wait() but it is never blocks the caller. If
 * the semaphore is not free, rt_sem_wait_if returns immediately and
 * the semaphore value remains unchanged.
 *
 * @param sem points to the structure used in the call to @ref
 * rt_sem_init().
 *
 * @return the number of events already signaled upon success.
 * A special value as described below in case of a failure:
 * - @b 0xFFFF: @e sem does not refer to a valid semaphore.
 *
 * @note In principle 0xFFFF could theoretically be a usable
 *	 semaphores events count so it could be returned also under
 *	 normal circumstances. It is unlikely you are going to count
 *	 up to such number  of events, in any case avoid counting up
 *	 to 0xFFFF.
 */
int rt_sem_wait_if(SEM *sem)
{
	int count;
	unsigned long flags;

	if (sem->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	if ((count = sem->count) <= 0) {
		if (sem->type > 0 && sem->owndby == RT_CURRENT) {
			sem->type++;
			rt_global_restore_flags(flags);
			return 0;
		}
	} else {
		sem->count--;
		if (sem->type > 0) {
			(sem->owndby = RT_CURRENT)->owndres++;
		}
	}
	rt_global_restore_flags(flags);
	return count;
}


/**
 * @anchor rt_sem_wait_until
 * @brief Wait a semaphore with timeout.
 *
 * rt_sem_wait_until, like @ref rt_sem_wait_timed() is a timed version
 * of the standard semaphore wait call. The semaphore value is
 * decremented and tested. If it is still non-negative these functions
 * return immediately. Otherwise the caller task is blocked and queued
 * up. Queuing may happen in priority order or on FIFO base. This is
 * determined by the compile time option @e SEM_PRIORD. In this case
 * the function returns if:
 *	- The caller task is in the first place of the waiting queue
 *	  and an other task issues a @ref rt_sem_signal call();
 *	- a timeout occurs;
 *	- an error occurs (e.g. the semaphore is destroyed);
 *
 * In case of a timeout, the semaphore value is incremented before 
 * return.  
 *
 * @param sem points to the structure used in the call to @ref
 *	  rt_sem_init().
 *
 * @param time is an absolute value to the current time.
 *
 * @return the number of events already signaled upon success.
 * Aa special value" as described below in case of a failure:
 * - @b 0xFFFF: @e sem does not refer to a valid semaphore.
 * 
 * @note In principle 0xFFFF could theoretically be a usable
 *	 semaphores events count so it could be returned also under
 *	 normal circumstances. It is unlikely you are going to count
 *	 up to such number of events, in any case avoid counting up to
 *	 0xFFFF.
 */
int rt_sem_wait_until(SEM *sem, RTIME time)
{
	DECLARE_RT_CURRENT;
	int count;
	unsigned long flags;

	if (sem->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((count = sem->count) <= 0) {
		rt_current->blocked_on = &sem->queue;
		if ((rt_current->resume_time = time) > rt_time_h) {
			unsigned long schedmap;
			if (sem->type > 0) {
				if (sem->owndby == rt_current) {
					sem->type++;
					rt_global_restore_flags(flags);
					return 0;
				}
				schedmap = pass_prio(sem->owndby, rt_current);
			} else {
				schedmap = 0;
			}	
			sem->count--;
			rt_current->state |= (RT_SCHED_SEMAPHORE | RT_SCHED_DELAYED);
			rem_ready_current(rt_current);
			enqueue_blocked(rt_current, &sem->queue, sem->qtype);
			enq_timed_task(rt_current);
			RT_SCHEDULE_MAP_BOTH(schedmap);
		} else {
			sem->count--;
			rt_current->queue.prev = rt_current->queue.next = &rt_current->queue;
		}
		if (sem->magic != RT_SEM_MAGIC) {
			rt_current->prio_passed_to = NOTHING;
			rt_global_restore_flags(flags);
			return SEM_ERR;
		} else {
			if (rt_current->blocked_on) {
				dequeue_blocked(rt_current);
				if(++sem->count > 1 && sem->type) {
					sem->count = 1;
				}
				rt_global_restore_flags(flags);
				return SEM_TIMOUT;
			} else {
				count = sem->count;
			}
		}
	} else {
		sem->count--;
	}
	if (sem->type > 0) {
		(sem->owndby = rt_current)->owndres++;
	}
	rt_global_restore_flags(flags);
	return count;
}


/**
 * @anchor rt_sem_wait_timed
 * @brief Wait a semaphore with timeout.
 *
 * rt_sem_wait_timed, like @ref rt_sem_wait_until(), is a timed version
 * of the standard semaphore wait call. The semaphore value is
 * decremented and tested. If it is still non-negative these functions
 * return immediately. Otherwise the caller task is blocked and queued
 * up. Queuing may happen in priority order or on FIFO base. This is
 * determined by the compile time option @e SEM_PRIORD. In this case
 * the function returns if:
 *	- The caller task is in the first place of the waiting queue
 *	  and an other task issues a @ref rt_sem_signal() call;
 *	- a timeout occurs;
 *	- an error occurs (e.g. the semaphore is destroyed);
 *
 * In case of a timeout, the semaphore value is incremented before 
 * return.  
 *
 * @param sem points to the structure used in the call to @ref
 *	  rt_sem_init().
 *
 * @param delay is an absolute value to the current time.
 *
 * @return the number of events already signaled upon success.
 * A special value as described below in case of a failure:
 * - @b 0xFFFF: @e sem does not refer to a valid semaphore.
 * 
 * @note In principle 0xFFFF could theoretically be a usable
 *	 semaphores events count so it could be returned also under
 *	 normal circumstances. It is unlikely you are going to count
 *	 up to such number of events, in any case avoid counting up to
 *	 0xFFFF.
 */
int rt_sem_wait_timed(SEM *sem, RTIME delay)
{
	return rt_sem_wait_until(sem, get_time() + delay);
}


/* ++++++++++++++++++++++++++ BARRIER SUPPORT +++++++++++++++++++++++++++++++ */

/**
 * @anchor rt_sem_wait_barrier
 * @brief Wait on a semaphore barrier.
 *
 * rt_sem_wait_barrier is a gang waiting in that a task issuing such
 * a request will be blocked till a number of tasks equal to the semaphore
 * count set at rt_sem_init is reached.
 *
 * @returns 0 always.
 */
int rt_sem_wait_barrier(SEM *sem)
{
	unsigned long flags;

	if (sem->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	if (!sem->owndby) {
		sem->owndby = (void *)(sem->count < 1 ? 1 : sem->count);
		sem->count = sem->type = 0;
	}
	if ((1 - sem->count) < (int)sem->owndby) {
		rt_sem_wait(sem);
	} else {
		rt_sem_broadcast(sem);
	}
	rt_global_restore_flags(flags);
	return 0;
}

/* +++++++++++++++++++++++++ COND VARIABLES SUPPORT +++++++++++++++++++++++++ */

/**
 * @anchor rt_cond_signal
 * @brief Wait for a signal to a conditional variable.
 *
 * rt_cond_signal resumes one of the tasks that are waiting on the condition 
 * semaphore cnd. Nothing happens if no task is waiting on @a cnd, while it
 * resumed the first queued task blocked on cnd, according to the queueing
 * method set at rt_cond_init.
 *
 * @param cnd points to the structure used in the call to @ref
 *	  rt_cond_init().
 *
 * @returns 0
 *
 */
int rt_cond_signal(CND *cnd)
{
	unsigned long flags;
	RT_TASK *task;

	if (cnd->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}
	flags = rt_global_save_flags_and_cli();
	if ((task = (cnd->queue.next)->task)) {
		dequeue_blocked(task);
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEMAPHORE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			RT_SCHEDULE(task, hard_cpu_id());
		}
	}
	rt_global_restore_flags(flags);
	return 0;
}

static inline void rt_cndmtx_signal(SEM *mtx, RT_TASK *rt_current)
{
	RT_TASK *task;

	if (mtx->type <= 1) {
		if (++mtx->count > 1) {
			mtx->count = 1;
		}
		if ((task = (mtx->queue.next)->task)) {
			dequeue_blocked(task);
			rem_timed_task(task);
			if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEMAPHORE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(task);
			}
		}
		if (mtx->type > 0) {
			mtx->owndby = 0;
			if (rt_current->owndres & SEMHLF) {
				--rt_current->owndres;
			}
			if (!rt_current->owndres) {
				renq_current(rt_current, rt_current->base_priority);
			} else if (!(rt_current->owndres & SEMHLF)) {
				int priority;
				renq_current(rt_current, rt_current->base_priority > (priority = ((rt_current->msg_queue.next)->task)->priority) ? priority : rt_current->base_priority);
			}
			if (rt_current->suspdepth > 0) {
				rt_current->state |= RT_SCHED_SUSPENDED;
				rem_ready_current(rt_current);
			} else if (rt_current->suspdepth < 0) {
				rt_task_delete(rt_current);
			}
		}
	} else {
		task = 0;
		mtx->type--;
	}
 	if (task) {
		 RT_SCHEDULE_BOTH(task, hard_cpu_id());
	} else {
		rt_schedule();
	}
}

/**
 * @anchor rt_cond_wait
 * @brief Wait for a signal to a conditional variable.
 *
 * rt_cond_wait atomically unlocks mtx (as for using rt_sem_signal)
 * and waits for the condition semaphore cnd to be signaled. The task 
 * execution is suspended until the condition semaphore is signalled. 
 * Mtx must be obtained by the calling task, before calling rt_cond_wait is
 * called. Before returning to the calling task rt_cond_wait reacquires 
 * mtx by calling rt_sem_wait.
 *
 * @param cnd points to the structure used in the call to @ref
 *	  rt_cond_init().
 *
 * @param mtx points to the structure used in the call to @ref
 *	  rt_sem_init().
 *
 * @return 0 on succes, SEM_ERR in case of error.
 *
 */
int rt_cond_wait(CND *cnd, SEM *mtx)
{
	RT_TASK *rt_current;
	unsigned long flags;
	int retval;

	if (cnd->magic != RT_SEM_MAGIC || mtx->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}
	retval = 0;
	flags = rt_global_save_flags_and_cli();
	rt_current = RT_CURRENT;
	rt_current->state |= RT_SCHED_SEMAPHORE;
	rem_ready_current(rt_current);
	enqueue_blocked(rt_current, &cnd->queue, cnd->qtype);
	rt_cndmtx_signal(mtx, rt_current);
	if (rt_current->blocked_on || cnd->magic != RT_SEM_MAGIC) {
		retval = SEM_ERR;
	}
	rt_global_restore_flags(flags);
	rt_sem_wait(mtx);
	return retval;
}

/**
 * @anchor rt_cond_wait_until
 * @brief Wait a semaphore with timeout.
 *
 * rt_cond_wait_until atomically unlocks mtx (as for using rt_sem_signal)
 * and waits for the condition semaphore cnd to be signalled. The task 
 * execution is suspended until the condition semaphore is either signaled
 * or a timeout expires. Mtx must be obtained by the calling task, before 
 * calling rt_cond_wait is called. Before returning to the calling task 
 * rt_cond_wait_until reacquires mtx by calling rt_sem_wait and returns a 
 * value to indicate if it has been signalled pr timedout.
 *
 * @param cnd points to the structure used in the call to @ref
 *	  rt_cond_init().
 *
 * @param mtx points to the structure used in the call to @ref
 *	  rt_sem_init().
 *
 * @param time is an absolute value to the current time, in timer count unit.
 *
 * @returns 0 if it was signaled, SEM_TIMOUT if a timeout occured, SEM_ERR
 * if the task has been resumed because of any other action (likely cnd
 * was deleted).
 */
int rt_cond_wait_until(CND *cnd, SEM *mtx, RTIME time)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;
	int retval;

	if (cnd->magic != RT_SEM_MAGIC && mtx->magic != RT_SEM_MAGIC) {
		return SEM_ERR;
	}
	retval = SEM_TIMOUT;
	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((rt_current->resume_time = time) > rt_time_h) {
		rt_current->state |= (RT_SCHED_SEMAPHORE | RT_SCHED_DELAYED);
		rem_ready_current(rt_current);
		enqueue_blocked(rt_current, &cnd->queue, cnd->qtype);
		enq_timed_task(rt_current);
		rt_cndmtx_signal(mtx, rt_current);
		if (cnd->magic != RT_SEM_MAGIC) {
			retval = SEM_ERR;
		} else {
			if (rt_current->blocked_on) {
				dequeue_blocked(rt_current);
			} else {
				retval = 0;
			}
		}
	} else {
		rt_global_restore_flags(flags);
		return retval;
	}
	rt_global_restore_flags(flags);
	rt_sem_wait(mtx);
	return retval;
}

/**
 * @anchor rt_cond_wait_timed
 * @brief Wait a semaphore with timeout.
 *
 * rt_cond_wait_timed atomically unlocks mtx (as for using rt_sem_signal)
 * and waits for the condition semaphore cnd to be signalled. The task 
 * execution is suspended until the condition semaphore is either signaled
 * or a timeout expires. Mtx must be obtained by the calling task, before 
 * calling rt_cond_wait is called. Before returning to the calling task 
 * rt_cond_wait_until reacquires mtx by calling rt_sem_wait and returns a 
 * value to indicate if it has been signalled pr timedout.
 *
 * @param cnd points to the structure used in the call to @ref
 *	  rt_cond_init().
 *
 * @param mtx points to the structure used in the call to @ref
 *	  rt_sem_init().
 *
 * @param delay is a realtive time values with respect to the current time,
 * in timer count unit.
 *
 * @returns 0 if it was signaled, SEM_TIMOUT if a timeout occured, SEM_ERR
 * if the task has been resumed because of any other action (likely cnd
 * was deleted).
 */
int rt_cond_wait_timed(CND *cnd, SEM *mtx, RTIME delay)
{
	return rt_cond_wait_until(cnd, mtx, get_time() + delay);
}

/* ++++++++++++++++++++ READERS-WRITER LOCKS SUPPORT ++++++++++++++++++++++++ */

/**
 * @anchor rt_rwl_init
 * @brief Initialize a multi readers single writer lock.
 *
 * rt_rwl_init initializes a multi readers single writer lock @a rwl.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * A multi readers single writer lock (RWL) is a synchronization mechanism 
 * that allows to have simultaneous read only access to an object, while only 
 * one task can have write access. A data set which is searched more 
 * frequently than it is changed can be usefully controlled by using an rwl. 
 * The lock acquisition policy is determined solely on the priority of tasks 
 * applying to own a lock.
 *
 * @returns 0 if always.
 *
 */

int rt_rwl_init(RWL *rwl)
{
	rt_typed_sem_init(&rwl->wrmtx, 1, RES_SEM);
	rt_typed_sem_init(&rwl->wrsem, 0, CNT_SEM);
	rt_typed_sem_init(&rwl->rdsem, 0, CNT_SEM);
	return 0;
}

/**
 * @anchor rt_rwl_delete
 * @brief destroys a multi readers single writer lock.
 *
 * rt_rwl_init destroys a multi readers single writer lock @a rwl.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @returns 0 if OK, SEM_ERR if anything went wrong.
 *
 */

int rt_rwl_delete(RWL *rwl)
{
	int ret;

	ret  =  rt_sem_delete(&rwl->rdsem);
	ret |= !rt_sem_delete(&rwl->wrsem);
	ret |= !rt_sem_delete(&rwl->wrmtx);
	return ret ? 0 : SEM_ERR;
}

/**
 * @anchor rt_rwl_rdlock
 * @brief acquires a multi readers single writer lock for reading.
 *
 * rt_rwl_rdlock acquires a multi readers single writer lock @a rwl for
 * reading. The calling task will block only if any writer owns the lock
 * already or there are writers with higher priority waiting to acquire 
 * write access.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @returns 0 if OK, SEM_ERR if anything went wrong after being blocked.
 *
 */

int rt_rwl_rdlock(RWL *rwl)
{
	unsigned long flags;
	RT_TASK *wtask, *rt_current;

	flags = rt_global_save_flags_and_cli();
	rt_current = RT_CURRENT;
	while (rwl->wrmtx.owndby || ((wtask = (rwl->wrsem.queue.next)->task) && wtask->priority <= rt_current->priority)) {
		int ret;
		if (rwl->wrmtx.owndby == rt_current) {
			rt_global_restore_flags(flags);
			return SEM_ERR + 1;
		}
		if ((ret = rt_sem_wait(&rwl->rdsem)) >= SEM_TIMOUT) {
			rt_global_restore_flags(flags);
			return ret;
		}
	}
	((int *)&rwl->rdsem.owndby)[0]++;
	rt_global_restore_flags(flags);
	return 0;
}

/**
 * @anchor rt_rwl_rdlock_if
 * @brief try to acquire a multi readers single writer lock just for reading.
 *
 * rt_rwl_rdlock_if tries to acquire a multi readers single writer lock @a rwl 
 * for reading immediately, i.e. without blocking if a writer owns the lock
 * or there are writers with higher priority waiting to acquire write access.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @returns 0 if the lock was acquired, -1 if the lock was already owned.
 *
 */

int rt_rwl_rdlock_if(RWL *rwl)
{
	unsigned long flags;
	RT_TASK *wtask;

	flags = rt_global_save_flags_and_cli();
	if (!rwl->wrmtx.owndby && (!(wtask = (rwl->wrsem.queue.next)->task) || wtask->priority > RT_CURRENT->priority)) {
		((int *)&rwl->rdsem.owndby)[0]++;
		rt_global_restore_flags(flags);
		return 0;
	}
	rt_global_restore_flags(flags);
	return -1;
}

/**
 * @anchor rt_rwl_rdlock_until
 * @brief try to acquire a multi readers single writer lock for reading within
 * an absolute deadline time.
 *
 * rt_rwl_rdlock_untill tries to acquire a multi readers single writer lock 
 * @a rwl for reading, as for rt_rwl_rdlock, but timing out if the lock has not 
 * been acquired within an assigned deadline.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @param time is the time deadline, in internal count units.
 *
 * @returns 0 if the lock was acquired, SEM_TIMOUT if the deadline expired
 * without acquiring the lock, SEM_ERR in case something went wrong.
 *
 */

int rt_rwl_rdlock_until(RWL *rwl, RTIME time)
{
	unsigned long flags;
	RT_TASK *wtask, *rt_current;

	flags = rt_global_save_flags_and_cli();
	rt_current = RT_CURRENT;
	while (rwl->wrmtx.owndby || ((wtask = (rwl->wrsem.queue.next)->task) && wtask->priority <= rt_current->priority)) {
		int ret;
		if (rwl->wrmtx.owndby == rt_current) {
			rt_global_restore_flags(flags);
			return SEM_ERR + 1;
		}
		if ((ret = rt_sem_wait_until(&rwl->rdsem, time)) >= SEM_TIMOUT) {
			rt_global_restore_flags(flags);
			return ret;
		}
	}
	((int *)&rwl->rdsem.owndby)[0]++;
	rt_global_restore_flags(flags);
	return 0;
}

/**
 * @anchor rt_rwl_rdlock_timed
 * @brief try to acquire a multi readers single writer lock for reading within
 * a relative deadline time.
 *
 * rt_rwl_rdlock_timed tries to acquire a multi readers single writer lock
 * @a rwl for reading, as for rt_rwl_rdlock, but timing out if the lock has not
 * been acquired within an assigned deadline.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @param delay is the time delay within which the lock must be acquired, in 
 * internal count units.
 *
 * @returns 0 if the lock was acquired, SEM_TIMOUT if the deadline expired
 * without acquiring the lock, SEM_ERR in case something went wrong.
 *
 */

int rt_rwl_rdlock_timed(RWL *rwl, RTIME delay)
{
	return rt_rwl_rdlock_until(rwl, get_time() + delay);
}

/**
 * @anchor rt_rwl_wrlock
 * @brief acquires a multi readers single writer lock for wrtiting.
 *
 * rt_rwl_rwlock acquires a multi readers single writer lock @a rwl for
 * writing. The calling task will block if any other task, reader or writer, 
 * owns the lock already.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @returns 0 if OK, SEM_ERR if anything went wrong after being blocked.
 *
 */

int rt_rwl_wrlock(RWL *rwl)
{
	unsigned long flags;
	int ret;

	flags = rt_global_save_flags_and_cli();
	while (rwl->rdsem.owndby) {
		if ((ret = rt_sem_wait(&rwl->wrsem)) >= SEM_TIMOUT) {
			rt_global_restore_flags(flags);
			return ret;
		}
	}
	if ((ret = rt_sem_wait(&rwl->wrmtx)) >= SEM_TIMOUT) {
		rt_global_restore_flags(flags);
		return ret;
	}
	rt_global_restore_flags(flags);
	return 0;
}

/**
 * @anchor rt_rwl_wrlock_if
 * @brief acquires a multi readers single writer lock for writing.
 *
 * rt_rwl_wrlock_if try to acquire a multi readers single writer lock @a rwl 
 * for writing immediately, i.e without blocking if the lock is owned already.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @returns 0 if the lock was acquired, -1 if the lock was already owned.
 *
 */

int rt_rwl_wrlock_if(RWL *rwl)
{
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	if (!rwl->rdsem.owndby && rt_sem_wait_if(&rwl->wrmtx) >= 0) {
		rt_global_restore_flags(flags);
		return 0;
	}
	rt_global_restore_flags(flags);
	return -1;
}

/**
 * @anchor rt_rwl_wrlock_until
 * @brief try to acquire a multi readers single writer lock for writing within
 * an absolute deadline time.
 *
 * rt_rwl_rwlock_until tries to acquire a multi readers single writer lock 
 * @a rwl for writing, as for rt_rwl_rwlock, but timing out if the lock has not 
 * been acquired within an assigned deadline.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @param time is the time deadline, in internal count units.
 *
 * @returns 0 if the lock was acquired, SEM_TIMOUT if the deadline expired
 * without acquiring the lock, SEM_ERR in case something went wrong.
 *
 */

int rt_rwl_wrlock_until(RWL *rwl, RTIME time)
{
	unsigned long flags;
	int ret;

	flags = rt_global_save_flags_and_cli();
	while (rwl->rdsem.owndby) {
		if ((ret = rt_sem_wait_until(&rwl->wrsem, time)) >= SEM_TIMOUT) {
			rt_global_restore_flags(flags);
			return ret;
		};
	}
	if ((ret = rt_sem_wait_until(&rwl->wrmtx, time)) >= SEM_TIMOUT) {
		rt_global_restore_flags(flags);
		return ret;
	};
	rt_global_restore_flags(flags);
	return 0;
}

/**
 * @anchor rt_rwl_wrlock_timed
 * @brief try to acquire a multi readers single writer lock for writing within
 * a relative deadline time.
 *
 * rt_rwl_wrlock_timed tries to acquire a multi readers single writer lock
 * @a rwl  for writing, as for rt_rwl_wrlock, timing out if the lock has not
 * been acquired within an assigned deadline.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @param delay is the time delay within which the lock must be acquired, in 
 * internal count units.
 *
 * @returns 0 if the lock was acquired, SEM_TIMOUT if the deadline expired
 * without acquiring the lock, SEM_ERR in case something went wrong.
 *
 */

int rt_rwl_wrlock_timed(RWL *rwl, RTIME delay)
{
	return rt_rwl_wrlock_until(rwl, get_time() + delay);
}

/**
 * @anchor rt_rwl_unlock
 * @brief unlock an acquired multi readers single writer lock.
 *
 * rt_rwl_unlock unlocks an acquired multi readers single writer lock @a rwl. 
 * After releasing the lock any task waiting to acquire it will own the lock
 * according to its priority, whether it is a reader or a writer, otherwise
 * the lock will be fully unlocked.
 *
 * @param rwl must point to an allocated @e RWL structure.
 *
 * @returns 0 always.
 *
 */

int rt_rwl_unlock(RWL *rwl)
{
	unsigned long flags;

	flags = rt_global_save_flags_and_cli();
	if (rwl->wrmtx.owndby) {
		rt_sem_signal(&rwl->wrmtx);
	} else if (rwl->rdsem.owndby) {
	           rwl->rdsem.owndby = (struct rt_task_struct *)((char *)rwl->rdsem.owndby - 1);
	}
	rt_global_restore_flags(flags);
	flags = rt_global_save_flags_and_cli();
	if (!rwl->wrmtx.owndby && !rwl->rdsem.owndby) {
		RT_TASK *wtask, *rtask;
		wtask = (rwl->wrsem.queue.next)->task;
		rtask = (rwl->rdsem.queue.next)->task;
		if (wtask && rtask) {
			if (wtask->priority < rtask->priority) {
				rt_sem_signal(&rwl->wrsem);
			} else {
				rt_sem_signal(&rwl->rdsem);
			}
		} else if (wtask) {
			rt_sem_signal(&rwl->wrsem);
		} else if (rtask) {
			rt_sem_signal(&rwl->rdsem);
		}
        }
	rt_global_restore_flags(flags);
	return 0;
}

/* +++++++++++++++++++++ RECURSIVE SPINLOCKS SUPPORT ++++++++++++++++++++++++ */

/**
 * @anchor rt_spl_init
 * @brief Initialize a spinlock.
 *
 * rt_spl_init initializes a spinlock @a spl.
 *
 * @param spl must point to an allocated @e SPL structure.
 *
 * A spinlock is an active wait synchronization mechanism useful for multi
 * processors very short synchronization, when it is more efficient to wait
 * at a meeting point instead of being suspended and the reactivated, as by
 * using semaphores, to acquire ownership of any object.
 * Spinlocks can be recursed once acquired, a recurring owner must care of
 * unlocking as many times as he took the spinlock.
 *
 * @returns 0 if always.
 *
 */

int rt_spl_init(SPL *spl)
{
	spl->owndby = 0;
	spl->count  = 0;
	return 0;
}

/**
 * @anchor rt_spl_delete
 * @brief Initialize a spinlock.
 *
 * rt_spl_delete destroies a spinlock @a spl.
 *
 * @param spl must point to an allocated @e SPL structure.
 *
 * @returns 0 if always.
 *
 */

int rt_spl_delete(SPL *spl)
{
        return 0;
}

/**
 * @anchor rt_spl_lock
 * @brief Acquire a spinlock.
 *
 * rt_spl_lock acquires a spinlock @a spl.
 *
 * @param spl must point to an allocated @e SPL structure.
 *
 * rt_spl_lock spins on lock till it can be acquired. If a tasks asks for
 * lock it owns already it will acquire it immediately but will have to care
 * to unlock it as many times as it recursed the spinlock ownership.
 *
 * @returns 0 if always.
 *
 */

int rt_spl_lock(SPL *spl)
{
	unsigned long flags;
	RT_TASK *rt_current;

	hard_save_flags_and_cli(flags);
	if (spl->owndby == (rt_current = RT_CURRENT)) {
		spl->count++;
	} else {
		while (atomic_cmpxchg(&spl->owndby, 0, rt_current));
		spl->flags = flags;
	}
	return 0;
}

/**
 * @anchor rt_spl_lock_if
 * @brief Acquire a spinlock without waiting.
 *
 * rt_spl_lock_if acquires a spinlock @a spl without waiting.
 *
 * @param spl must point to an allocated @e SPL structure.
 *
 * rt_spl_lock_if tries to acquire a spinlock but will not spin on it if
 * it is owned already.
 *
 * @returns 0 if it succeeded, -1 if the lock was owned already.
 *
 */

int rt_spl_lock_if(SPL *spl)
{
	unsigned long flags;
	RT_TASK *rt_current;

	hard_save_flags_and_cli(flags);
	if (spl->owndby == (rt_current = RT_CURRENT)) {
		spl->count++;
	} else {
		if (atomic_cmpxchg(&spl->owndby, 0, rt_current)) {
			hard_restore_flags(flags);
			return -1;
		}
		spl->flags = flags;
	}
	return 0;
}

/**
 * @anchor rt_spl_lock_timed
 * @brief Acquire a spinlock with timeout.
 *
 * rt_spl_lock_timed acquires a spinlock @a spl, but waiting spinning only 
 * for an allowed time.
 *
 * @param spl must point to an allocated @e SPL structure.
 *
 * @param ns timeout 
 *
 * rt_spl_lock spins on lock till it can be acquired, as for rt_spl_lock,
 * but only for an allowed time. If the spinlock cannot be acquired in time
 * the functions returns in error.
 * This function can be usefull either in itself or as a diagnosis toll
 * during code development.
 *
 * @returns 0 if the spinlock was acquired, -1 if a timeout occured.
 *
 */

int rt_spl_lock_timed(SPL *spl, unsigned long ns)
{
	unsigned long flags;
	RT_TASK *rt_current;

	hard_save_flags_and_cli(flags);
	if (spl->owndby == (rt_current = RT_CURRENT)) {
		spl->count++;
	} else {
		RTIME end_time;
		void *locked;
		end_time = rdtsc() + imuldiv(ns, tuned.cpu_freq, 1000000000);
		while ((locked = atomic_cmpxchg(&spl->owndby, 0, rt_current)) && rdtsc() < end_time);
		if (locked) {
			hard_restore_flags(flags);
			return -1;
		}
		spl->flags = flags;
	}
	return 0;
}

/**
 * @anchor rt_spl_unlock
 * @brief Release an owned spinlock.
 *
 * rt_spl_lock releases an owned spinlock @a spl.
 *
 * @param spl must point to an allocated @e SPL structure.
 *
 * rt_spl_unlock releases an owned lock. The spinlock can remain locked and
 * its ownership can remain with the task is the spinlock acquisition was 
 * recursed.
 *
 * @returns 0 if the function was used legally, -1 if a tasks tries to unlock
 * a spinlock it does not own.
 *
 */

int rt_spl_unlock(SPL *spl)
{
	unsigned long flags;
	RT_TASK *rt_current;

	hard_save_flags_and_cli(flags);
	if (spl->owndby == (rt_current = RT_CURRENT)) {
		if (spl->count) {
			--spl->count;
		} else {
			spl->owndby = 0;
			spl->count  = 0;
			hard_restore_flags(spl->flags);
		}
		return 0;
	}
	hard_restore_flags(flags);
	return -1;
}

/* ++++++ NAMED SEMAPHORES, BARRIER, COND VARIABLES, RWLOCKS, SPINLOCKS +++++ */

#include <rtai_registry.h>

/**
 * @anchor _rt_typed_named_sem_init
 * @brief Initialize a specifically typed (counting, binary, resource)
 *	  semaphore identified by a name.
 *
 * _rt_typed_named_sem_init allocate and initializes a semaphore identified 
 * by @e name of type @e type. Once the semaphore structure is allocated the 
 * initialization is as for rt_typed_sem_init. The function returns the
 * handle pointing to the allocated semaphore structure, to be used as the
 * usual semaphore address in all semaphore based services. Named objects
 * are useful for use among different processes, kernel/user space and
 * in distributed applications, see netrpc.
 *
 * @param sem_name is the identifier associated with the returned object.
 *
 * @param value is the initial value of the semaphore, always set to 1
 *	  for a resource semaphore.
 *
 * @param type is the semaphore type and queuing policy. It can be an OR
 * a semaphore kind: CNT_SEM for counting semaphores, BIN_SEM for binary 
 * semaphores, RES_SEM for resource semaphores; and queuing policy:
 * FIFO_Q, PRIO_Q for a fifo and priority queueing respectively.
 * Resource semaphores will enforce a PRIO_Q policy anyhow.
 * 
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 *
 * @see nam2num() and num2nam().
 *
 * See rt_typed_sem_init for further clues.
 *
 * As for all the named initialization functions it must be remarked that
 * only the very first call to initilize/create a named RTAI object does a 
 * real allocation of the object, any following call with the same name 
 * will just increase its usage count. In any case the function returns
 * a pointer to the named object, or zero if in error.
 *
 * @returns either a valid pointer or 0 if in error.
 *
 */

SEM *_rt_typed_named_sem_init(unsigned long sem_name, int value, int type)
{
	SEM *sem;

	if ((sem = rt_get_adr_cnt(sem_name))) {
		return sem;
	}
	if ((sem = rt_malloc(sizeof(SEM)))) {
		rt_typed_sem_init(sem, value, type);
		if (rt_register(sem_name, sem, IS_SEM, 0)) {
			return sem;
		}
		rt_sem_delete(sem);
	}
	rt_free(sem);
	return (SEM *)0;
}

/**
 * @anchor rt_named_sem_delete
 * @brief Delete a semaphore initialized in named mode.
 *
 * rt_named_sem_delete deletes a semaphore previously created with 
 * @ref _rt_typed_named_sem_init(). 
 *
 * @param sem points to the structure pointer returned by a corresponding
 * call to _rt_typed_named_sem_init. 
 *
 * Any tasks blocked on this semaphore is returned in error and
 * allowed to run when semaphore is destroyed. 
 * As it is done by all the named allocation functions delete calls have just 
 * the effect of decrementing a usage count till the last is done, as that is 
 * the one the really frees the object.
 *
 * @return an int >=0 is returned upon success, SEM_ERR if it failed to 
 * delete the semafore, -EFAULT if the semaphore does not exist anymore.
 *
 */

int rt_named_sem_delete(SEM *sem)
{
	int ret;
	if (!(ret = rt_drg_on_adr_cnt(sem))) {
		if (!rt_sem_delete(sem)) {
			rt_free(sem);
			return 0;
		} else {
			return SEM_ERR;
		}
	}
	return ret;
}

/**
 * @anchor _rt_named_rwl_init
 * @brief Initialize a multi readers single writer lock identified by a name.
 *
 * _rt_named_rwl_init allocate and initializes a multi readers single writer 
 * lock (RWL) identified by @e name. Once the lock structure is allocated the 
 * initialization is as for rt_rwl_init. The function returns the
 * handle pointing to the allocated multi readers single writer lock o
 * structure, to be used as the usual lock address in all rwl based services. 
 * Named objects are useful for use among different processes, kernel/user 
 * space and in distributed applications, see netrpc.
 *
 * @param rwl_name is the identifier associated with the returned object.
 *
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 *
 * @see nam2num() and num2nam().
 *
 * As for all the named initialization functions it must be remarked that
 * only the very first call to initilize/create a named RTAI object does a 
 * real allocation of the object, any following call with the same name 
 * will just increase its usage count. In any case the function returns
 * a pointer to the named object, or zero if in error.
 *
 * @returns either a valid pointer or 0 if in error.
 *
 */

RWL *_rt_named_rwl_init(unsigned long rwl_name)
{
	RWL *rwl;

	if ((rwl = rt_get_adr_cnt(rwl_name))) {
		return rwl;
	}
	if ((rwl = rt_malloc(sizeof(RWL)))) {
		rt_rwl_init(rwl);
		if (rt_register(rwl_name, rwl, IS_RWL, 0)) {
			return rwl;
		}
		rt_rwl_delete(rwl);
	}
	rt_free(rwl);
	return (RWL *)0;
}

/**
 * @anchor rt_named_rwl_delete
 * @brief Delete a multi readers single writer lock in named mode.
 *
 * rt_named_rwl_delete deletes a multi readers single writer lock
 * previously created with @ref _rt_named_rwl_init(). 
 *
 * @param rwl points to the structure pointer returned by a corresponding 
 * call to rt_named_rwl_init. 
 *
 * As it is done by all the named allocation functions delete calls have just 
 * the effect of decrementing a usage count till the last is done, as that is 
 * the one the really frees the object.
 *
 * @return an int >=0 is returned upon success, SEM_ERR if it failed to 
 * delete the multi readers single writer lock, -EFAULT if the lock does 
 * not exist anymore.
 *
 */

int rt_named_rwl_delete(RWL *rwl)
{
	int ret;
	if (!(ret = rt_drg_on_adr_cnt(rwl))) {
		if (!rt_rwl_delete(rwl)) {
			rt_free(rwl);
			return 0;
		} else {
			return SEM_ERR;
		}
	}
	return ret;
}

/**
 * @anchor _rt_named_spl_init
 * @brief Initialize a spinlock identified by a name.
 *
 * _rt_named_spl_init allocate and initializes a spinlock (SPL) identified 
 * by @e name. Once the spinlock structure is allocated the initialization 
 * is as for rt_spl_init. The function returns the handle pointing to the 
 * allocated spinlock structure, to be used as the usual spinlock address 
 * in all spinlock based services. Named objects are useful for use among 
 * different processes and kernel/user space.
 *
 * @param spl_name is the identifier associated with the returned object.
 *
 * Since @a name can be a clumsy identifier, services are provided to
 * convert 6 characters identifiers to unsigned long, and vice versa.
 *
 * @see nam2num() and num2nam().
 *
 * As for all the named initialization functions it must be remarked that
 * only the very first call to initilize/create a named RTAI object does a 
 * real allocation of the object, any following call with the same name 
 * will just increase its usage count. In any case the function returns
 * a pointer to the named object, or zero if in error.
 *
 * @returns either a valid pointer or 0 if in error.
 *
 */

SPL *_rt_named_spl_init(unsigned long spl_name)
{
	SPL *spl;

	if ((spl = rt_get_adr_cnt(spl_name))) {
		return spl;
	}
	if ((spl = rt_malloc(sizeof(SPL)))) {
		rt_spl_init(spl);
		if (rt_register(spl_name, spl, IS_SPL, 0)) {
			return spl;
		}
		rt_spl_delete(spl);
	}
	rt_free(spl);
	return (SPL *)0;
}

/**
 * @anchor rt_named_spl_delete
 * @brief Delete a spinlock in named mode.
 *
 * rt_named_spl_delete deletes a spinlock previously created with
 * @ref _rt_named_spl_init(). 
 *
 * @param spl points to the structure pointer returned by a corresponding 
 * call to rt_named_spl_init. 
 *
 * As it is done by all the named allocation functions delete calls have just 
 * the effect of decrementing a usage count till the last is done, as that is 
 * the one the really frees the object.
 *
 * @return an int >=0 is returned upon success, -EFAULT if the spinlock
 * does not exist anymore.
 *
 */

int rt_named_spl_delete(SPL *spl)
{
	int ret;
	if (!(ret = rt_drg_on_adr_cnt(spl))) {
		rt_spl_delete(spl);
		rt_free(spl);
		return 0;
	}
	return ret;
}

/* +++++ SEMAPHORES, BARRIER, COND VARIABLES, RWLOCKS, SPINLOCKS ENTRIES ++++ */

struct rt_native_fun_entry rt_sem_entries[] = {
	{ { 0, rt_typed_sem_init },        TYPED_SEM_INIT },
	{ { 0, rt_sem_delete },            SEM_DELETE },
	{ { 0, _rt_typed_named_sem_init }, NAMED_SEM_INIT },
	{ { 0, rt_named_sem_delete },      NAMED_SEM_DELETE },
	{ { 1, rt_sem_signal },            SEM_SIGNAL },
	{ { 1, rt_sem_wait },              SEM_WAIT },
	{ { 1, rt_sem_wait_if },           SEM_WAIT_IF },
	{ { 1, rt_sem_wait_until },        SEM_WAIT_UNTIL },
	{ { 1, rt_sem_wait_timed },        SEM_WAIT_TIMED },
	{ { 1, rt_sem_broadcast },         SEM_BROADCAST },
	{ { 1, rt_sem_wait_barrier },      SEM_WAIT_BARRIER },
	{ { 1, rt_sem_count },             SEM_COUNT },
        { { 1, rt_cond_wait },             COND_WAIT },
        { { 1, rt_cond_wait_until },       COND_WAIT_UNTIL },
        { { 1, rt_cond_wait_timed },       COND_WAIT_TIMED },
        { { 0, rt_rwl_init },              RWL_INIT },
        { { 0, rt_rwl_delete },            RWL_DELETE },
	{ { 0, _rt_named_rwl_init },	   NAMED_RWL_INIT },
	{ { 0, rt_named_rwl_delete },      NAMED_RWL_DELETE },
        { { 1, rt_rwl_rdlock },            RWL_RDLOCK },
        { { 1, rt_rwl_rdlock_if },         RWL_RDLOCK_IF },
        { { 1, rt_rwl_rdlock_until },      RWL_RDLOCK_UNTIL },
        { { 1, rt_rwl_rdlock_timed },      RWL_RDLOCK_TIMED },
        { { 1, rt_rwl_wrlock },            RWL_WRLOCK },
        { { 1, rt_rwl_wrlock_if },         RWL_WRLOCK_IF },
        { { 1, rt_rwl_wrlock_until },      RWL_WRLOCK_UNTIL },
        { { 1, rt_rwl_wrlock_timed },      RWL_WRLOCK_TIMED },
        { { 1, rt_rwl_unlock },            RWL_UNLOCK },
        { { 0, rt_spl_init },              SPL_INIT },
        { { 0, rt_spl_delete },            SPL_DELETE },
	{ { 0, _rt_named_spl_init },	   NAMED_SPL_INIT },
	{ { 0, rt_named_spl_delete },      NAMED_SPL_DELETE },
        { { 1, rt_spl_lock },              SPL_LOCK },
        { { 1, rt_spl_lock_if },           SPL_LOCK_IF },
        { { 1, rt_spl_lock_timed },        SPL_LOCK_TIMED },
        { { 1, rt_spl_unlock },            SPL_UNLOCK },
        { { 1, rt_cond_signal}, 	   COND_SIGNAL },
	{ { 0, 0 },  		           000 }
};

extern int set_rt_fun_entries(struct rt_native_fun_entry *entry);
extern void reset_rt_fun_entries(struct rt_native_fun_entry *entry);

int __rtai_sem_init (void)
{
    return set_rt_fun_entries(rt_sem_entries);
}

void __rtai_sem_exit (void)
{
    reset_rt_fun_entries(rt_sem_entries);
}

/* +++++++ END SEMAPHORES, BARRIER, COND VARIABLES, RWLOCKS, SPINLOCKS ++++++ */

/*@}*/

#ifndef CONFIG_RTAI_SEM_BUILTIN
module_init(__rtai_sem_init);
module_exit(__rtai_sem_exit);
#endif /* !CONFIG_RTAI_SEM_BUILTIN */

#ifdef CONFIG_KBUILD
EXPORT_SYMBOL(rt_typed_sem_init);
EXPORT_SYMBOL(rt_sem_init);
EXPORT_SYMBOL(rt_sem_delete);
EXPORT_SYMBOL(rt_sem_count);
EXPORT_SYMBOL(rt_sem_signal);
EXPORT_SYMBOL(rt_sem_broadcast);
EXPORT_SYMBOL(rt_sem_wait);
EXPORT_SYMBOL(rt_sem_wait_if);
EXPORT_SYMBOL(rt_sem_wait_until);
EXPORT_SYMBOL(rt_sem_wait_timed);
EXPORT_SYMBOL(rt_sem_wait_barrier);
EXPORT_SYMBOL(_rt_typed_named_sem_init);
EXPORT_SYMBOL(rt_named_sem_delete);

EXPORT_SYMBOL(rt_cond_signal);
EXPORT_SYMBOL(rt_cond_wait);
EXPORT_SYMBOL(rt_cond_wait_until);
EXPORT_SYMBOL(rt_cond_wait_timed);

EXPORT_SYMBOL(rt_rwl_init);
EXPORT_SYMBOL(rt_rwl_delete);
EXPORT_SYMBOL(rt_rwl_rdlock);
EXPORT_SYMBOL(rt_rwl_rdlock_if);
EXPORT_SYMBOL(rt_rwl_rdlock_until);
EXPORT_SYMBOL(rt_rwl_rdlock_timed);
EXPORT_SYMBOL(rt_rwl_wrlock);
EXPORT_SYMBOL(rt_rwl_wrlock_if);
EXPORT_SYMBOL(rt_rwl_wrlock_until);
EXPORT_SYMBOL(rt_rwl_wrlock_timed);
EXPORT_SYMBOL(rt_rwl_unlock);
EXPORT_SYMBOL(_rt_named_rwl_init);
EXPORT_SYMBOL(rt_named_rwl_delete);

EXPORT_SYMBOL(rt_spl_init);
EXPORT_SYMBOL(rt_spl_delete);
EXPORT_SYMBOL(rt_spl_lock);
EXPORT_SYMBOL(rt_spl_lock_if);
EXPORT_SYMBOL(rt_spl_lock_timed);
EXPORT_SYMBOL(rt_spl_unlock);
EXPORT_SYMBOL(_rt_named_spl_init);
EXPORT_SYMBOL(rt_named_spl_delete);
#endif /* CONFIG_KBUILD */
