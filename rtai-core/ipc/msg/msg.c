/** 
 * @file
 * Message handling functions.
 * @author Paolo Mantegazza
 *
 * @note Copyright (C) 1999-2003 Paolo Mantegazza
 * <mantegazza@aero.polimi.it> [ Specific COPYRIGHTS follow along the
 *  code ] 
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
 * @ingroup msg
 * @ingroup rpc
 */

/**
 * @ingroup sched
 * @defgroup msg Message handling functions
 */

/**
 * @ingroup sched
 * @defgroup rpc Remote procedure call functions
 */

#include <rtai_schedcore.h>

MODULE_LICENSE("GPL");

/* +++++++++++++++++++++++ SHORT INTERTASK MESSAGES +++++++++++++++++++++++++ */

/* +++++++++++++++++++++++++++++ ASYNC SENDS ++++++++++++++++++++++++++++++++ */

/**
 * @ingroup msg
 * @anchor rt_send
 * @brief Send a message.
 *
 * rt_send sends the message @e msg to the task @e task. If the
 * receiver task is ready to get the message rt_send does not block
 * the sending task, but its execution can be preempted if the
 * receiving task has a higher priority. Otherwise the caller task is
 * blocked and queued up (queuing may happen in priority order or on
 * FIFO base. This is determined by the compile time option
 * MSG_PRIORD.)
 *
 * @param task is a pointer to a task structure.
 *
 * @param msg corresponds to the message that has to be sent.
 *
 * @return On success, the pointer to the task that received the message is
 * returned.<br>
 * 0 is returned if the caller is unblocked but the message has not
 * been sent, e.g. the task @e task was killed before receiving the
 * message.<br>
 * A special value is returned as described below in case of
 * a failure:
 * - @b 0xFFFF: @e task does not refer to a valid task.
 * 
 * @note Since all the messaging functions return a task address
 * 0xFFFF could seem an inappropriate return value. However on all the
 * CPUs RTAI runs on 0xFFFF is not an address that can be used by any
 * RTAI task, so it is should be always safe.
 */
RT_TASK *rt_send(RT_TASK *task, unsigned int msg)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((task->state & RT_SCHED_RECEIVE) &&
	      (!task->msg_queue.task || task->msg_queue.task == rt_current)) {
		task->msg = msg;
		task->msg_queue.task = rt_current;
		task->ret_queue.task = NOTHING;
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_RECEIVE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			RT_SCHEDULE(task, cpuid);
		}
	} else {
		rt_current->msg = msg;
		rt_current->msg_queue.task = task;
		enqueue_blocked(rt_current, &task->msg_queue, 0);
		rt_current->state |= RT_SCHED_SEND;
		rem_ready_current(rt_current);
		rt_schedule();
	}
	if (rt_current->msg_queue.task != rt_current) {
		rt_current->msg_queue.task = rt_current;
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	return task;
}


/**
 * @ingroup msg
 * @anchor rt_send_if
 * @brief Send a message, only if the calling task is not blocked.
 *
 * rt_send_if sends the message @e msg to the task @e task if the
 * latter is ready to receive, so that the caller task is never
 * blocked, but its execution can be preempted if the messaged task is
 * ready to receive and has a higher priority.
 *
 * @param task is a pointer to a task structure.
 *
 * @param msg corresponds to the message that has to be sent.
 *
 * @return the pointer to the task @e task that received the message 
 * is returned upon success.<br>
 * @e 0 is returned if the message has not been sent.<br>
 * A special value @e 0xFFFF is returned upon failure.<br><br>
 * The errors are described below: 
 * - @b 0: the task @e task was not ready to receive the message.
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * @note Since all the messaging functions return a task address,
 * 0xFFFF could seem an inappropriate return value. However on all the
 * CPUs RTAI runs on 0xFFFF is not an address that can be used by any
 * RTAI task, so it is should be always safe. (FIXME)
 */
RT_TASK *rt_send_if(RT_TASK *task, unsigned int msg)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((task->state & RT_SCHED_RECEIVE) &&
	      (!task->msg_queue.task || task->msg_queue.task == rt_current)) {
		task->msg = msg;
		task->msg_queue.task = rt_current;
		task->ret_queue.task = NOTHING;
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_RECEIVE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			RT_SCHEDULE(task, cpuid);
		}
		if (rt_current->msg_queue.task != rt_current) {
			rt_current->msg_queue.task = rt_current;
			task = (RT_TASK *)0;
		}
	} else {
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	return task;
}


/**
 * @ingroup msg
 * @anchor rt_send_until
 * brief Send a message with timeout.
 *
 * rt_send_until sends the message @e msg to the task @e task. If the
 * receiver task is ready to get the message, this function does not
 * block the sending task, but its execution can be preempted if the
 * receiving task has a higher priority. Otherwise the caller task is
 * blocked and queued up (queuing may happen in priority order or on
 * FIFO base. This is determined by the compile time option
 * MSG_PRIORD). In this case the function returns if:
 * - the caller task is in the first place of the waiting queue and
 *   the receiver gets the message and has a lower priority;
 * - a timeout occurs;
 * - an error occurs (e.g. the receiver task is killed).
 *
 * @param task is a pointer to a task structure.
 *
 * @param msg corresponds to the message that has to be sent.
 *
 * @param time is an absolute value.
 *
 * @return the pointer to the task that received the message is
 * returned on success i.e. the message received before timeout
 * expiration.<br>
 * 0 is returned if the message has not been sent.
 * A special value is returned on other failure. The errors
 * are described below:  
 * - @b 0: operation timed out, message was not delivered; 
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * See also: @ref rt_send_timed().
 *
 * @note Since all the messaging functions return a task address
 * 0xFFFF could seem an inappropriate return value.  However on all
 * the CPUs RTAI runs on 0xFFFF is not an address that can be used by
 * any RTAI task, so it is should be always safe.
 */
RT_TASK *rt_send_until(RT_TASK *task, unsigned int msg, RTIME time)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((task->state & RT_SCHED_RECEIVE) &&
	      (!task->msg_queue.task || task->msg_queue.task == rt_current)) {
		task->msg = msg;
		task->msg_queue.task = rt_current;
		task->ret_queue.task = NOTHING;
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_RECEIVE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			RT_SCHEDULE(task, cpuid);
		}
	} else {
		rt_current->msg_queue.task = task;
		if ((rt_current->resume_time = time) > rt_time_h) {
			rt_current->msg = msg;
			enqueue_blocked(rt_current, &task->msg_queue, 0);
			rt_current->state |= (RT_SCHED_SEND | RT_SCHED_DELAYED);
			rem_ready_current(rt_current);
			enq_timed_task(rt_current);
			rt_schedule();
		} else {
			rt_current->queue.prev = rt_current->queue.next = &rt_current->queue;
		}
	}
	if (rt_current->msg_queue.task != rt_current) {
		dequeue_blocked(rt_current);
		rt_current->msg_queue.task = rt_current;
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	return task;
}


/**
 * @ingroup msg
 * @anchor rt_send_timed
 * brief Send a message with timeout.
 *
 * rt_send_timed sends the message @e msg to the task @e task. If the 
 * receiver task is ready to get the message, this function does not
 * block the sending task, but its execution can be preempted if the
 * receiving task has a higher priority. Otherwise the caller task is
 * blocked and queued up (queuing may happen in priority order or on
 * FIFO base. This is determined by the compile time option
 * MSG_PRIORD). In this case the function returns if:
 * - the caller task is in the first place of the waiting queue and
 *   the receiver gets the message and has a lower priority;
 * - a timeout occurs;
 * - an error occurs (e.g. the receiver task is killed).
 *
 * @param task is a pointer to a task structure.
 *
 * @param msg corresponds to the message that has to be sent.
 *
 * @param delay is relative to the current time.
 *
 * @return on success, the pointer to the task that received the
 * message i.e. the message received before timeout expiration.<br>
 * 0 if the message has not been sent.<br>
 * A special value on other failure. The errors
 * are described below:  
 * - @b 0: operation timed out, message was not delivered; 
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * See also: @ref rt_send_until().
 *
 * @note Since all the messaging functions return a task address
 * 0xFFFF could seem an inappropriate return value.  However on all
 * the CPUs RTAI runs on 0xFFFF is not an address that can be used by
 * any RTAI task, so it is should be always safe. (FIXME)
 */

RT_TASK *rt_send_timed(RT_TASK *task, unsigned int msg, RTIME delay)
{
	return rt_send_until(task, msg, get_time() + delay);
}

/* ++++++++++++++++++++++++++++++++ RPCS +++++++++++++++++++++++++++++++++++ */

/**
 * @ingroup rpc
 * @anchor rt_rpc
 * @brief Make a remote procedure call
 *
 * rt_rpc makes a Remote Procedure Call (RPC). rt_rpc is used for
 * synchronous inter task messaging as it sends the message @e msg to the
 * task @e task then it always block waiting until a return is
 * received from the called task. So the caller task is always blocked
 * and queued up (queuing may happen in priority order or on FIFO
 * base. This is determined by the compile time option
 * MSG_PRIORD). The receiver task may get the message with any
 * rt_receive function. It can send an answer with @ref rt_return().
 *
 * @param task pointer to a RT_TASK structure.
 *
 * @param msg message to send.
 *
 * @param reply points to a buffer provided by the caller were the
 * returned result message, any 4 bytes integer, is to be place.
 *
 * @return On success, task (the pointer to the task that received the
 * message) is returned. If the message has not been sent (e.g. the
 * task @e task was killed before receiving the message) 0 is returned.
 * On other failure, a special value is returned as described below:
 * - @b 0: the receiver task was killed before receiving the message.
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * See also: rt_receive_*, @ref rt_return(), @ref rt_isrpc().
 *
 * @note Since all the messaging functions return a task address,
 *       0xFFFF could seem an inappropriate return value. However on
 *       all the CPUs RTAI runs on, 0xFFFF is not an address that can
 *       be used by any RTAI task, so it is should be always safe.<br>
 *	 The trio @ref rt_rpc(), @ref rt_receive(), @ref rt_return()
 * 	 implement functions similar to its peers send-receive-replay
 * 	 found in QNX, except that in RTAI only four bytes messages
 * 	 contained in any integer can be exchanged. That's so because
 * 	 we never needed anything different. Note also that we prefer
 * 	 the idea of calling a function by using a message and then
 * 	 wait for a return value since it is believed to give a better
 *  	 idea of what is meant for synchronous message passing. For
 * 	 a truly QNX like way of inter task messaging use the support
 * 	 module found in directory lxrt-informed.
 */
RT_TASK *rt_rpc(RT_TASK *task, unsigned int to_do, unsigned int *result)
{

	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((task->state & RT_SCHED_RECEIVE) &&
		(!task->msg_queue.task || task->msg_queue.task == rt_current)) {
		rt_current->msg = task->msg = to_do;
		task->msg_queue.task = rt_current;
		task->ret_queue.task = NOTHING;
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_RECEIVE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
		}
		rt_current->state |= RT_SCHED_RETURN;
	} else {
		rt_current->msg = to_do;
		task->owndres += RPCINC;
                pass_prio(task, rt_current);
                enqueue_blocked(rt_current, &task->msg_queue, 0);
		rt_current->state |= RT_SCHED_RPC;
	}
	rem_ready_current(rt_current);
	rt_current->msg_queue.task = task;
	RT_SCHEDULE_BOTH(task, cpuid);
	if (rt_current->msg_queue.task == rt_current) {
		*result = rt_current->msg;
	} else {
		rt_current->msg_queue.task = rt_current;
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	return task;
}


/**
 * @ingroup rpc
 * @anchor rt_rpc_if
 * @brief Make a remote procedure call, only if the calling task is
 *  not blocked.
 *
 * rt_rpc_if tries to make a Remote Procedure Call (RPC). If the
 * receiver task is ready to accept a message rt_rpc_if sends the
 * message @e msg then it always block until a return is received. In
 * this case the caller task is blocked and queued up (queuing may
 * happen in priority order or on FIFO base. This is determined by the
 * compile time option MSG_PRIORD). If the receiver is not ready
 * rt_rpc_if returns immediately. The receiver task may get the
 * message with any rt_receive function. It can send the answer with
 * @ref rt_return().
 *
 * @param task pointer to a RT_TASK structure.
 *
 * @param msg message to send.
 *
 * @param reply points to a buffer provided by the caller.
 *
 * @return On success, task (the pointer to the task that received the
 * message) is returned. If message has not been sent, 0 is
 * returned. On other failure, a special value is returned as
 * described below:
 * - @b 0: The task @e task was not ready to receive the message or
 *   	   it was killed before sending the reply.
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * See also: notes under @ref rt_rpc().
 *
 * @note Since all the messaging functions return a task address,
 * 	 0xFFFF could seem an inappropriate return value. However on
 * 	 all the CPUs RTAI runs on, 0xFFFF is not an address that can
 *  	 be used by any RTAI task, so it is should be always safe.
 */
RT_TASK *rt_rpc_if(RT_TASK *task, unsigned int to_do, unsigned int *result)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}
	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((task->state & RT_SCHED_RECEIVE) &&
	      (!task->msg_queue.task || task->msg_queue.task == rt_current)) {
		rt_current->msg = task->msg = to_do;
		task->msg_queue.task = rt_current;
		task->ret_queue.task = NOTHING;
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_RECEIVE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
		}
		rt_current->state |= RT_SCHED_RETURN;
		rem_ready_current(rt_current);
		rt_current->msg_queue.task = task;
		RT_SCHEDULE_BOTH(task, cpuid);
		if (rt_current->msg_queue.task == rt_current) {
			*result = rt_current->msg;
		} else {
			rt_current->msg_queue.task = rt_current;
			task = (RT_TASK *)0;
		}
	} else {
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	return task;
}


/** 
 * @ingroup rpc
 * @anchor rt_rpc_until
 * @brief Make a remote procedure call with timeout.
 *
 * rt_rpc_until makes a Remote Procedure Call. It sends the message @e
 * msg to the task @e task then always waits until a return is
 * received or a timeout occurs. So the caller task is always blocked
 * and queued up (queuing may happen in priority order or on FIFO
 * base. This is determined by the compile time option
 * MSG_PRIORD). The receiver task may get the message with any @ref
 * rt_receive() function. It can send the answer with @ref rt_return().
 *
 * @param task pointer to a RT_TASK structure.
 *
 * @param msg message to send.
 *
 * @param reply points to a buffer provided by the caller.
 *
 * @param time is an absolute value.
 *
 * @return On success, task (the pointer to the task that received the
 * message) is returned. If message has not been sent or no answer
 * arrived, 0 is returned.
 * On other failure, a special value is returned as described below:
 * - @b 0: The message could not be sent or the answer did not arrived
 *    	   in time.  
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * See also: @ref rt_receive(), @ref rt_return(), @ref rt_isrpc().
 *
 * @note Since all the messaging functions return a task address, 0xFFFF
 * could seem an inappropriate return value. However on all the CPUs
 * RTAI runs on, 0xFFFF is not an address that can be used by any RTAI
 * task, so it is should be always safe.<br>
 * See also the notes under @ref rt_rpc().
 */
RT_TASK *rt_rpc_until(RT_TASK *task, unsigned int to_do, unsigned int *result, RTIME time)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((task->state & RT_SCHED_RECEIVE) &&
	    (!task->msg_queue.task || task->msg_queue.task == rt_current)) {
		rt_current->msg = task->msg = to_do;
		task->msg_queue.task = rt_current;
		task->ret_queue.task = NOTHING;
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_RECEIVE | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
		}
		rt_current->state |= (RT_SCHED_RETURN | RT_SCHED_DELAYED);
	} else {
		if ((rt_current->resume_time = time) <= rt_time_h) {
			rt_global_restore_flags(flags);
			return (RT_TASK *)0;
		}
		rt_current->msg = to_do;
		task->owndres += RPCINC;
		pass_prio(task, rt_current);
		enqueue_blocked(rt_current, &task->msg_queue, 0);
		rt_current->state |= (RT_SCHED_RPC | RT_SCHED_DELAYED);
	}
	rem_ready_current(rt_current);
	rt_current->msg_queue.task = task;
	enq_timed_task(rt_current);
	RT_SCHEDULE_BOTH(task, cpuid);
	if (rt_current->msg_queue.task == rt_current) {
		*result = rt_current->msg;
	} else {
		dequeue_blocked(rt_current);
		rt_current->msg_queue.task = rt_current;
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	return task;
}


/** 
 * @ingroup rpc
 * @anchor rt_rpc_timed
 * @brief Make a remote procedure call with timeout.
 *
 * rt_rpc_timed makes a Remote Procedure Call. It sends the message @e
 * msg to the task @e task then always waits until a return is
 * received or a timeout occurs. So the caller task is always blocked
 * and queued up (queuing may happen in priority order or on FIFO
 * base. This is determined by the compile time option
 * MSG_PRIORD). The receiver task may get the message with any @ref
 * rt_receive() function. It can send the answer with @ref rt_return().
 *
 * @param task pointer to a RT_TASK structure.
 *
 * @param msg message to send.
 *
 * @param reply points to a buffer provided by the caller.
 *
 * @param delay is relative to the current time.
 *
 * @return On success, task (the pointer to the task that received the
 * message) is returned. If message has not been sent or no answer
 * arrived, 0 is returned.
 * On other failure, a special value is returned as described below:
 * - @b 0: The message could not be sent or the answer did not arrived
 *    	   in time.  
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * See also: @ref rt_receive(), @ref rt_return(), @ref rt_isrpc().
 *
 * @note Since all the messaging functions return a task address, 0xFFFF
 * could seem an inappropriate return value. However on all the CPUs
 * RTAI runs on, 0xFFFF is not an address that can be used by any RTAI
 * task, so it is should be always safe.<br>
 * See also the notes under @ref rt_rpc().
 */
RT_TASK *rt_rpc_timed(RT_TASK *task, unsigned int to_do, unsigned int *result, RTIME delay)
{
	return rt_rpc_until(task, to_do, result, get_time() + delay);
}

/* ++++++++++++++++++++++++++++++ RPC_RETURN +++++++++++++++++++++++++++++++ */

/**
 * @ingroup rpc
 * @anchor rt_isrpc
 * @brief Check if sender waits for reply or not.
 *
 * After receiving a message, by calling rt_isrpc a task can figure
 * out whether the sender task @e task is waiting for a reply or
 * not. That can be needed in the case one needs a server task that
 * must provide services both to sends (FIXME) and rt_rtcs.
 * No answer is required if the message sent by a @e rt_send function
 * or the sender called @ref rt_rpc_timed() or @ref rt_rpc_until() but it
 * is already timed out.
 *
 * @param task pointer to a task structure.
 *
 * @return If the task waits for a reply, a nonzero value is returned.
 * 	   Otherwise 0 is returned.
 *
 * @note rt_isrpc does not perform any check on pointer task. rt_isrpc
 *  cannot figure out what RPC result the sender is waiting for.<br>
 * @ref rt_return() is intelligent enough to not send an answer to a
 * task which is not waiting for it. Therefore using rt_isrpc is not
 * necessary and discouraged. 
 */
int rt_isrpc(RT_TASK *task)
{
	return task->state & RT_SCHED_RETURN;
}


/**
 * @ingroup rpc
 * @anchor rt_return
 * @brief Sends (returns) the result back to the task that made the 
 *  related remote procedure call.
 *
 * rt_return sends the result result to the task @e task. If the task
 * calling rt_rpc previously is not waiting the answer (i.e. killed or
 * timed out) this return message is silently discarded.
 *
 * @return On success, task (the pointer to the task that is got the
 * reply) is returned. If the reply message has not been sent, 0 is
 * returned. On other failure, a special value is returned as
 * described below:
 * - @b 0: The reply message was not delivered.
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * @note Since all the messaging functions return a task address,
 * 0xFFFF could seem an inappropriate return value. However on all the
 * CPUs RTAI runs on, 0xFFFF is not an address that can be used by any
 * RTAI task, so it is should be always safe.
 *
 * See also: notes under @ref rt_rpc().
 */
RT_TASK *rt_return(RT_TASK *task, unsigned int result)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if ((task->state & RT_SCHED_RETURN) && task->msg_queue.task == rt_current) {
		int sched;
		dequeue_blocked(task);
		if (rt_current->owndres & RPCHLF) {
			rt_current->owndres -= RPCINC;
		}
		if (!rt_current->owndres) {
			sched = renq_current(rt_current, rt_current->base_priority);
		} else if (!(rt_current->owndres & SEMHLF)) {
			int priority;
			sched = renq_current(rt_current, rt_current->base_priority > (priority = ((rt_current->msg_queue.next)->task)->priority) ? priority : rt_current->base_priority);
		} else {
			sched = 0;
		}
		task->msg = result;
		task->msg_queue.task = task;
		rem_timed_task(task);
		if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_RETURN | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
			enq_ready_task(task);
			if (sched) {
				RT_SCHEDULE_BOTH(task, cpuid);
			} else {
				RT_SCHEDULE(task, cpuid);
			}
                } else if (sched) {
                        rt_schedule();
                }
	} else {
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	return task;
}

/* ++++++++++++++++++++++++++++++ RECEIVES +++++++++++++++++++++++++++++++++ */

RT_TASK *rt_evdrp(RT_TASK *task, unsigned int *msg)
{
	DECLARE_RT_CURRENT;

	if (task && task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	ASSIGN_RT_CURRENT;
	if (!task) task = (rt_current->msg_queue.next)->task;
	if ((task->state & (RT_SCHED_SEND | RT_SCHED_RPC)) && task->msg_queue.task == rt_current) {
		*msg = task->msg;
	} else {
		task = (RT_TASK *)0;
	}
	return task;
}


/**
 * @ingroup msg
 * @anchor rt_receive
 * @brief Receive a message.
 *
 * rt_receive gets a message from the task specified by task.  If task
 * is equal to 0, the caller accepts messages from any task. If there
 * is a pending message, rt_receive does not block but can be
 * preempted if the task that sent the just received message has a
 * higher priority. Otherwise the caller task is blocked and queued up
 * (queuing may happen in priority order or on FIFO base.  This is
 * determined by the compile time option MSG_PRIORD.)
 *
 * @param task is a pointer to a @e RT_TASK structure.
 * 
 * @param msg points to any 4 bytes word buffer provided by the
 * caller. 
 *
 * @return a pointer to the sender task is returned upon success.<br> 
 * 0 is returned if the caller is unblocked but no message has
 * been received (e.g. the task @e task was killed before sending the
 * message.)<br>
 * A special value is returned on other failure. The errors 
 * are described below:  
 * - @b 0: the sender task was killed before sending the message;
 * - @b 0xFFFF: @e task does not refer to a valid task.
 * 
 * @note Since all the messaging functions return a task address
 * 0xFFFF could seem an inappropriate return value.  However on all
 * the CPUs RTAI runs on 0xFFFF is not an address that can be used by
 * any RTAI task, so it is should be always safe.
 */
RT_TASK *rt_receive(RT_TASK *task, unsigned int *msg)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task && task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (!task) task = (rt_current->msg_queue.next)->task;
	if ((task->state & (RT_SCHED_SEND | RT_SCHED_RPC)) && task->msg_queue.task == rt_current) {
		dequeue_blocked(task);
		rem_timed_task(task);
		*msg = task->msg;
		rt_current->msg_queue.task = task;
		if (task->state & RT_SCHED_SEND) {
			task->msg_queue.task = task;
			if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEND | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(task);
				RT_SCHEDULE(task, cpuid);
			}
		} else if (task->state & RT_SCHED_RPC) {
                        enqueue_blocked(task, &rt_current->ret_queue, 0);
			task->state = (task->state & ~(RT_SCHED_RPC | RT_SCHED_DELAYED)) | RT_SCHED_RETURN;
		}
	} else {
		rt_current->ret_queue.task = SOMETHING;
		rt_current->state |= RT_SCHED_RECEIVE;
		rem_ready_current(rt_current);
		rt_current->msg_queue.task = task != rt_current ? task : (RT_TASK *)0;
		rt_schedule();
		*msg = rt_current->msg;
	}
	if (rt_current->ret_queue.task) {
		rt_current->ret_queue.task = NOTHING;
		task = (RT_TASK *)0;
	} else {
		task = rt_current->msg_queue.task;
	}
	rt_current->msg_queue.task = rt_current;
	rt_global_restore_flags(flags);
	if (task && (struct proxy_t *)task->stack_bottom) {
		if (((struct proxy_t *)task->stack_bottom)->receiver == rt_current) {
			rt_return(task, 0);
		}
	}
	return task;
}


/**
 * @ingroup msg
 * @anchor rt_receive_if
 * @brief Receive a message, only if the calling task is not blocked.
 * 
 * rt_receive_if tries to get a message from the task specified by
 * task. If task is equal to 0, the caller accepts messages from any
 * task. The caller task is never blocked but can be preempted if the
 * receiving task is ready to receive and has a higher priority.
 *
 * @param task is a pointer to the task structure.
 *
 * @param msg points to a buffer provided by the caller.
 *
 * @return a pointer to the sender task is returned upon success.<br>
 * A special value is returned on other failure. The errors are
 * described below:   
 * - @b 0: there was no message to receive;
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * Since all the messaging functions return a task address 0xFFFF
 * could seem an inappropriate return value. However on all the CPUs
 * RTAI runs on 0xFFFF is not an address that can be used by any RTAI
 * task, so it is should be always safe.
 */
RT_TASK *rt_receive_if(RT_TASK *task, unsigned int *msg)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task && task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (!task) task = (rt_current->msg_queue.next)->task;
	if ((task->state & (RT_SCHED_SEND | RT_SCHED_RPC)) && task->msg_queue.task == rt_current) {
		dequeue_blocked(task);
		rem_timed_task(task);
		*msg = task->msg;
		rt_current->msg_queue.task = task;
		if (task->state & RT_SCHED_SEND) {
			task->msg_queue.task = task;
			if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEND | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(task);
				RT_SCHEDULE(task, cpuid);
			}
		} else if (task->state & RT_SCHED_RPC) {
			enqueue_blocked(task, &rt_current->ret_queue, 0);
			task->state = (task->state & ~(RT_SCHED_RPC | RT_SCHED_DELAYED)) | RT_SCHED_RETURN;
		}
		if (rt_current->ret_queue.task) {
			rt_current->ret_queue.task = NOTHING;
			task = (RT_TASK *)0;
		} else {
			task = rt_current->msg_queue.task;
		}
		rt_current->msg_queue.task = rt_current;
	} else {
		task = (RT_TASK *)0;
	}
	rt_global_restore_flags(flags);
	if (task && (struct proxy_t *)task->stack_bottom) {
		if (((struct proxy_t *)task->stack_bottom)->receiver == rt_current) {
			rt_return(task, 0);
		}
	}
	return task;
}


/**
 * @ingroup msg
 * @anchor rt_receive_until
 * @brief Receive a message with timeout.
 *
 * rt_receive_until receives a message from the task specified by
 * task. If task is equal to 0, the caller accepts messages from any
 * task. If there is a pending message, rt_receive does not block but
 * can be preempted if the task that sent the just received message
 * has a higher priority.
 * Otherwise the caller task is blocked and queued up (queuing may
 * happen in priority order or on FIFO base. This is determined by the
 * compile time option MSG_PRIORD.) In this case these functions
 * return if:
 * - the caller task is in the first place of the waiting queue and
 *   the sender sends a message and has a lower priority;
 * - timeout occurs;
 * - an error occurs (e.g. the sender task is killed.)
 *
 * @param task is a pointer to the task structure.
 *
 * @param msg points to a buffer provided by the caller.
 *
 * @param time is an absolute value.
 *
 * @return On success, a pointer to the sender task is returned.
 * On other failure, a special value is returned. The errors
 * are described below:  
 * - @b 0: there was no message to receive.
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * @note Since all the messaging functions return a task address
 * 0xFFFF could seem an inappropriate return value. However on all
 * the CPUs RTAI runs on 0xFFFF is not an address that can be used by
 * any RTAI task, so it is should be always safe.
 *
 * See also: @ref rt_receive_timed().
 */
RT_TASK *rt_receive_until(RT_TASK *task, unsigned int *msg, RTIME time)
{
	DECLARE_RT_CURRENT;
	unsigned long flags;

	if (task && task->magic != RT_TASK_MAGIC) {
		return MSG_ERR;
	}

	flags = rt_global_save_flags_and_cli();
	ASSIGN_RT_CURRENT;
	if (!task) task = (rt_current->msg_queue.next)->task;
	if ((task->state & (RT_SCHED_SEND | RT_SCHED_RPC)) && task->msg_queue.task == rt_current) {
		dequeue_blocked(task);
		rem_timed_task(task);
		*msg = task->msg;
		rt_current->msg_queue.task = task;
		if (task->state & RT_SCHED_SEND) {
			task->msg_queue.task = task;
			if (task->state != RT_SCHED_READY && (task->state &= ~(RT_SCHED_SEND | RT_SCHED_DELAYED)) == RT_SCHED_READY) {
				enq_ready_task(task);
				RT_SCHEDULE(task, cpuid);
			}
		} else if (task->state & RT_SCHED_RPC) {
			enqueue_blocked(task, &rt_current->ret_queue, 0);
			task->state = (task->state & ~(RT_SCHED_RPC | RT_SCHED_DELAYED)) | RT_SCHED_RETURN;
		}
	} else {
		rt_current->ret_queue.task = SOMETHING;
		if ((rt_current->resume_time = time) > rt_time_h) {
			rt_current->state |= (RT_SCHED_RECEIVE | RT_SCHED_DELAYED);
			rem_ready_current(rt_current);
			rt_current->msg_queue.task = task != rt_current ? task : (RT_TASK *)0;
			enq_timed_task(rt_current);
			rt_schedule();
			*msg = rt_current->msg;
		}
	}
	if (rt_current->ret_queue.task) {
		rt_current->ret_queue.task = NOTHING;
		task = (RT_TASK *)0;
	} else {
		task = rt_current->msg_queue.task;
	}
	rt_current->msg_queue.task = rt_current;
	rt_global_restore_flags(flags);
	if (task && (struct proxy_t *)task->stack_bottom) {
		if (((struct proxy_t *)task->stack_bottom)->receiver == rt_current) {
			rt_return(task, 0);
		}
	}
	return task;
}


/**
 * @ingroup msg
 * @anchor rt_receive_timed
 * @brief Receive a message with timeout.
 *
 * rt_receive_timed receives a message from the task specified by
 * task. If task is equal to 0, the caller accepts messages from any
 * task. If there is a pending message, rt_receive does not block but
 * can be preempted if the task that sent the just received message
 * has a higher priority.
 * Otherwise the caller task is blocked and queued up (queuing may
 * happen in priority order or on FIFO base. This is determined by the
 * compile time option MSG_PRIORD.) In this case these functions
 * return if:
 * - the caller task is in the first place of the waiting queue and
 *   the sender sends a message and has a lower priority;
 * - timeout occurs;
 * - an error occurs (e.g. the sender task is killed.)
 *
 * @param task is a pointer to the task structure.
 *
 * @param msg points to a buffer provided by the caller.
 *
 * @param delay is relative to the current time.
 *
 * @return On success, a pointer to the sender task is returned.
 * On other failure, a special value is returned. The errors
 * are described below:  
 * - @b 0: there was no message to receive.
 * - @b 0xFFFF: @e task does not refer to a valid task.
 *
 * @note Since all the messaging functions return a task address
 * 0xFFFF could seem an inappropriate return value. However on all
 * the CPUs RTAI runs on 0xFFFF is not an address that can be used by
 * any RTAI task, so it is should be always safe.
 *
 * See also: @ref rt_receive_until().
 */
RT_TASK *rt_receive_timed(RT_TASK *task, unsigned int *msg, RTIME delay)
{
	return rt_receive_until(task, msg, get_time() + delay);
}

/* ++++++++++++++++++++++++++ EXTENDED MESSAGES +++++++++++++++++++++++++++++++
COPYRIGHT (C) 2003  Pierre Cloutier  (pcloutier@poseidoncontrols.com)
                    Paolo Mantegazza (mantegazza@aero.polimi.it)
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */

#define SET_RPC_MCB() \
	do { \
		mcb.sbuf   = smsg; \
		mcb.sbytes = ssize; \
		mcb.rbuf   = rmsg; \
		mcb.rbytes = rsize; \
	} while (0)

RT_TASK *rt_rpcx(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize)
{
	if (task) {
		struct mcb_t mcb;
		SET_RPC_MCB();
		return rt_rpc(task, (unsigned int)&mcb, &mcb.rbytes);
	}
	return 0;
}


RT_TASK *rt_rpcx_if(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize)
{
	if (task) {
		struct mcb_t mcb;
		SET_RPC_MCB();
		return rt_rpc_if(task, (unsigned int)&mcb, &mcb.rbytes);
	}
	return 0;
}


RT_TASK *rt_rpcx_until(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize, RTIME time)
{
	if (task) {
		struct mcb_t mcb;
		SET_RPC_MCB();
		return rt_rpc_until(task, (unsigned int)&mcb, &mcb.rbytes, time);
	}
	return 0;
}


RT_TASK *rt_rpcx_timed(RT_TASK *task, void *smsg, void *rmsg, int ssize, int rsize, RTIME delay)
{
	if (task) {
		struct mcb_t mcb;
		SET_RPC_MCB();
		return rt_rpc_timed(task, (unsigned int)&mcb, &mcb.rbytes, delay);
	}
	return 0;
}

#define task_mcb (task->mcb)
#define SET_SEND_MCB() \
	do { \
		task_mcb.sbuf   = msg; \
		task_mcb.sbytes = size; \
		task_mcb.rbuf   = 0; \
		task_mcb.rbytes = 0; \
	} while (0)

RT_TASK *rt_sendx(RT_TASK *task, void *msg, int size) 
{
	if (task) {
		SET_SEND_MCB();
		return rt_send(task, (unsigned int)&task_mcb);
	}
	return 0;
}


RT_TASK *rt_sendx_if(RT_TASK *task, void *msg, int size)
{
	if (task) {
		SET_SEND_MCB();
		return rt_send_if(task, (unsigned int)&task_mcb);
	}
	return 0;
}


RT_TASK *rt_sendx_until(RT_TASK *task, void *msg, int size, RTIME time)
{
	if (task) {
		SET_SEND_MCB();
		return rt_send_until(task, (unsigned int)&task_mcb, time);
	}
	return 0;
}


RT_TASK *rt_sendx_timed(RT_TASK *task, void *msg, int size, RTIME delay)
{
	if (task) {
		SET_SEND_MCB();
		return rt_send_timed(task, (unsigned int)&task_mcb, delay);
	}
	return 0;
}


RT_TASK *rt_returnx(RT_TASK *task, void *msg, int size)
{
	if (task) {
		struct mcb_t *mcb;
		if ((mcb = (struct mcb_t *)task->msg)->rbytes < size) {
			size = mcb->rbytes;
		}
		if (size) {
			memcpy(mcb->rbuf, msg, size);
		}
		return rt_return(task, 0);
	}
	return 0;
}

#define DO_RCV_MSG() \
	do { \
		if ((*len = size <= mcb->sbytes ? size : mcb->sbytes)) { \
			memcpy(msg, mcb->sbuf, *len); \
		} \
	} while (0)


RT_TASK *rt_evdrpx(RT_TASK *task, void *msg, int size, int *len)
{
	struct mcb_t *mcb;
	if ((task = rt_evdrp(task, (unsigned int *)&mcb))) {
		DO_RCV_MSG();
		return task;
	}
	return 0;
}


RT_TASK *rt_receivex(RT_TASK *task, void *msg, int size, int *len)
{
	struct mcb_t *mcb;
	if ((task = rt_receive(task, (unsigned int *)&mcb))) {
		DO_RCV_MSG();
		return task;
	}
	return 0;
}


RT_TASK *rt_receivex_if(RT_TASK *task, void *msg, int size, int *len)
{
	struct mcb_t *mcb;
	if ((task = rt_receive_if(task, (unsigned int *)&mcb))) {
		DO_RCV_MSG();
		return task;
	}
	return 0;
}


RT_TASK *rt_receivex_until(RT_TASK *task, void *msg, int size, int *len, RTIME time)
{
	struct mcb_t *mcb;
	if ((task = rt_receive_until(task, (unsigned int *)&mcb, time))) {
		DO_RCV_MSG();
		return task;
	}
	return 0;
}


RT_TASK *rt_receivex_timed(RT_TASK *task, void *msg, int size, int *len, RTIME delay)
{
	struct mcb_t *mcb;
	if ((task = rt_receive_timed(task, (unsigned int *)&mcb, delay))) {
		DO_RCV_MSG();
		return task;
	}
	return 0;
}

/* +++++++++++++++++++++++++++++++ PROXIES ++++++++++++++++++++++++++++++++++ */

extern int rt_kthread_init(RT_TASK *task,
			   void (*rt_thread)(int),
			   int data,
			   int stack_size,
			   int priority,
			   int uses_fpu,
			   void(*signal)(void));

// What any proxy is supposed to do, raw RTAI implementation.
static void proxy_task(RT_TASK *me)
{
	struct proxy_t *my;
	unsigned int ret;

	my = (struct proxy_t *)me->stack_bottom;        	
	while (1) {
		while (my->nmsgs) {
		 	atomic_dec((atomic_t *)&my->nmsgs);
			rt_rpc(my->receiver, *((unsigned int *)my->msg), &ret);
		}
		rt_task_suspend(me);
	}
}

// Create a raw proxy agent task.
RT_TASK *__rt_proxy_attach(void (*agent)(int), RT_TASK *task, void *msg, int nbytes, int priority)
{
	RT_TASK *proxy, *rt_current;
	struct proxy_t *my;

        rt_current = _rt_whoami();
	if (!task) {
		task = rt_current;
	}

	if (task->magic != RT_TASK_MAGIC) {
		return 0;
	}

	if (!(proxy = rt_malloc(sizeof(RT_TASK)))) {
		return 0;
	}

	if (priority == -1 && (priority = rt_current->base_priority) == RT_SCHED_LINUX_PRIORITY) {
		priority = RT_SCHED_LOWEST_PRIORITY;
	}
	if (rt_kthread_init(proxy, agent, (int)proxy, PROXY_MIN_STACK_SIZE + nbytes + sizeof(struct proxy_t), priority, 0, 0)) {
		rt_free(proxy);
		return 0;
	}

	my = (struct proxy_t *)(proxy->stack_bottom);
	my->receiver = task ;
	my->msg      = ((char *)(proxy->stack_bottom)) + sizeof(struct proxy_t);
	my->nmsgs    = 0;
	my->nbytes   = nbytes;
	if (msg && nbytes) {
		memcpy(my->msg, msg, nbytes);
	}

        // agent is at *(proxy->stack + 2)
	return proxy;
}

// Create a raw proxy task.
RT_TASK *rt_proxy_attach(RT_TASK *task, void *msg, int nbytes, int prio)
{
	return __rt_proxy_attach((void *)proxy_task, task, msg, nbytes, prio);
}

// Delete a proxy task (a simplified specific rt_task_delete).
// Note: a self delete will not do the rt_free() call.
int rt_proxy_detach(RT_TASK *proxy)
{
	if (!rt_task_delete(proxy)) {
		rt_free(proxy);
		return 0;
	}
	return -EINVAL;
}

// Trigger a proxy.
RT_TASK *rt_trigger(RT_TASK *proxy)
{
	struct proxy_t *his;
	
	his = (struct proxy_t *)(proxy->stack_bottom);
	if (his && proxy->magic == RT_TASK_MAGIC) {
		atomic_inc((atomic_t *)&his->nmsgs);
		rt_task_resume(proxy);
		return his->receiver;
	}
	return (RT_TASK *)0;
}

#ifdef CONFIG_RTAI_INTERNAL_LXRT_SUPPORT

/* ++++++++++++ ANOTHER API SET FOR EXTENDED INTERTASK MESSAGES +++++++++++++++
COPYRIGHT (C) 2003  Pierre Cloutier  (pcloutier@poseidoncontrols.com)
                    Paolo Mantegazza (mantegazza@aero.polimi.it)
+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++ */

#include <asm/uaccess.h>

#include "rtai_registry.h"
#include "rtai_msg.h"

int rt_Send(pid_t pid, void *smsg, void *rmsg, size_t ssize, size_t rsize)
{
	RT_TASK *task;
	if ((task = pid2rttask(pid))) {
		MSGCB cb;
		RT_TASK *replier;
		unsigned int replylen;
		cb.cmd    = SYNCMSG;
		cb.sbuf   = smsg;
		cb.sbytes = ssize; 
		cb.rbuf   = rmsg;
		cb.rbytes = rsize;
		if (!(replier = rt_rpc(task, (unsigned int)&cb, &replylen))) {
			return -EINVAL;
		} else if (replier != task) {
			return -ESRCH;
		}
		return replylen ;
	}
	return -ESRCH;
}

pid_t rt_Receive(pid_t pid, void *msg, size_t maxsize, size_t *msglen)
{
	RT_TASK *task;
	MSGCB *cb;
	if ((task = rt_receive(pid ? pid2rttask(pid) : 0, (void *)&cb))) {
		if ((pid = rttask2pid(task))) {
			*msglen = maxsize <= cb->sbytes ? maxsize : cb->sbytes; 
			if (*msglen) {
				memcpy(msg, cb->sbuf, *msglen);
			}
			return pid;
		}
		return -ESRCH;
	}
	return -EINVAL;
}

pid_t rt_Creceive(pid_t pid, void *msg, size_t maxsize, size_t *msglen, RTIME delay)
{
	RT_TASK *task;
	MSGCB *cb;
	task = pid ? pid2rttask(pid) : 0;
	if (delay) {
		task = rt_receive_timed(task, (void *)&cb, delay);
	} else {
		task = rt_receive_if(task, (void *)&cb);
	}
	if (task) {
		if ((pid = rttask2pid(task))) {
			*msglen = maxsize <= cb->sbytes ? maxsize : cb->sbytes;
			if (*msglen) {
				memcpy(msg, cb->sbuf, *msglen);
			}
			return pid;
		}
		return 0;
	}
	return 0;
}

int rt_Reply(pid_t pid, void *msg, size_t size)
{
	RT_TASK *task;
	if ((task = pid2rttask(pid))) {
		MSGCB *cb;
		if ((cb = (MSGCB *)task->msg)->cmd == SYNCMSG) {
			unsigned int retlen;
			RT_TASK *retask;
			if ((retlen = size <= cb->rbytes ? size : cb->rbytes)) {
				memcpy(cb->rbuf, msg, retlen);
			}
			if (!(retask = rt_return(task, retlen))) {
				return -EINVAL;
			} else if (retask != task) {
				return -ESRCH;
			}
			return 0;
		}
		return -EPERM;
	}
	return -ESRCH;
}

static void Proxy_Task(RT_TASK *me)
{
        struct proxy_t *my;
	MSGCB cb;
        unsigned int replylen;
        my = (struct proxy_t *)me->stack_bottom;
	cb.cmd    = PROXY;
	cb.sbuf   = my->msg;
	cb.sbytes = my->nbytes;
	cb.rbuf   = &replylen;
	cb.rbytes = sizeof(replylen);
        while(1) {
		while (my->nmsgs) {
			atomic_dec((atomic_t *)&my->nmsgs);
                        rt_rpc(my->receiver, (unsigned int)(&cb), &replylen);
		}
		rt_task_suspend(me);
        }
}

pid_t rt_Proxy_attach(pid_t pid, void *msg, int nbytes, int prio)
{
	RT_TASK *task;
	return (task = __rt_proxy_attach((void *)Proxy_Task, pid ? pid2rttask(pid) : 0, msg, nbytes, prio)) ? (task->lnxtsk)->pid : -ENOMEM;
}

int rt_Proxy_detach(pid_t pid)
{
	RT_TASK *proxy;
	if (!rt_task_delete(proxy = pid2rttask(pid))) {
		rt_free(proxy);
		return 0;
	}
	return -EINVAL;
}

pid_t rt_Trigger(pid_t pid)
{
	RT_TASK *proxy;
       	struct proxy_t *his;
	if ((proxy = pid2rttask(pid))) {
	        his = (struct proxy_t *)(proxy->stack_bottom);
        	if (his && proxy->magic == RT_TASK_MAGIC) {
	                atomic_inc((atomic_t *)&his->nmsgs);
        	        rt_task_resume(proxy);
                	return rttask2pid(his->receiver);
		}
		return -EINVAL;
	}
	return -ESRCH;
}


pid_t rt_Name_attach(const char *argname)
{
	RT_TASK *task;
	task = current->this_rt_task[0] ? (RT_TASK *)current->this_rt_task[0] : _rt_whoami();
	if (current->comm[0] != 'U' && current->comm[1] != ':') {
	    	strncpy_from_user(task->task_name, argname, MAX_NAME_LENGTH);
	} else {
	    	strncpy(task->task_name, argname, MAX_NAME_LENGTH);
	}
    	task->task_name[MAX_NAME_LENGTH - 1] = 0;
	return strnlen(task->task_name, MAX_NAME_LENGTH) > (MAX_NAME_LENGTH - 1) ? -EINVAL : task->lnxtsk ? ((struct task_struct *)current->this_rt_task[1])->pid : (pid_t)task;
}

pid_t rt_Name_locate(const char *arghost, const char *argname)
{
	extern RT_TASK rt_smp_linux_task[];
	int cpuid;
	RT_TASK *task;
        for (cpuid = 0; cpuid < smp_num_cpus; cpuid++) {
                task = &rt_smp_linux_task[cpuid];
                while ((task = task->next)) {
			if (!strncmp(argname, task->task_name, MAX_NAME_LENGTH - 1)) {
				return (struct task_struct *)(task->lnxtsk) ?  ((struct task_struct *)(task->lnxtsk)->this_rt_task[1])->pid : (pid_t)task;

			}
		}
	}
	return strlen(argname) <= 6 && (task = rt_get_adr(nam2num(argname))) ? rttask2pid(task) : 0;
}

int rt_Name_detach(pid_t pid)
{
	if (pid <= PID_MAX) {
	 	if (pid != ((struct task_struct *)current->this_rt_task[1])->pid ) {
			return -EINVAL;
		}
	    	((RT_TASK *)current->this_rt_task[0])->task_name[0] = 0;
	} else {
	    	((RT_TASK *)pid)->task_name[0] = 0;
	}
	return 0;
}

#endif /* CONFIG_RTAI_INTERNAL_LXRT_SUPPORT */

/* +++++++++++++++++++++ INTERTASK MESSAGES ENTRIES +++++++++++++++++++++++++ */

struct rt_native_fun_entry rt_msg_entries[] = {
	{ { 1, rt_send },				SENDMSG },
	{ { 1, rt_send_if },				SEND_IF },
	{ { 1, rt_send_until },				SEND_UNTIL },
	{ { 1, rt_send_timed },				SEND_TIMED },
	{ { UW1(2, 0), rt_receive },			RECEIVEMSG },
	{ { UW1(2, 0), rt_receive_if },			RECEIVE_IF },
	{ { UW1(2, 0), rt_receive_until },		RECEIVE_UNTIL },
	{ { UW1(2, 0), rt_receive_timed },		RECEIVE_TIMED },
	{ { UW1(3, 0), rt_rpc },			RPCMSG },
	{ { UW1(3, 0), rt_rpc_if },			RPC_IF },
	{ { UW1(3, 0), rt_rpc_until },			RPC_UNTIL },
	{ { UW1(3, 0), rt_rpc_timed },			RPC_TIMED },
	{ { UW1(2, 0), rt_evdrp },			EVDRP },
	{ { 0, rt_isrpc }, 		 		ISRPC },
	{ { 1, rt_return },				RETURNMSG },
	{ { UR1(2, 4) | UW1(3, 5), rt_rpcx },		RPCX },
	{ { UR1(2, 4) | UW1(3, 5), rt_rpcx_if },	RPCX_IF },
	{ { UR1(2, 4) | UW1(3, 5), rt_rpcx_until },	RPCX_UNTIL },
	{ { UR1(2, 4) | UW1(3, 5), rt_rpcx_timed }, 	RPCX_TIMED },
	{ { UR1(2, 3), rt_sendx },			SENDX },
	{ { UR1(2, 3), rt_sendx_if },			SENDX_IF },
	{ { UR1(2, 3), rt_sendx_until },		SENDX_UNTIL },
	{ { UR1(2, 3), rt_sendx_timed },		SENDX_TIMED },
	{ { UR1(2, 3), rt_returnx },			RETURNX },
	{ { UW1(2, 3) | UW2(4, 0), rt_receivex },	RECEIVEX },
	{ { UW1(2, 3) | UW2(4, 0), rt_receivex_if },	RECEIVEX_IF },
	{ { UW1(2, 3) | UW2(4, 0), rt_receivex_until }, RECEIVEX_UNTIL },
	{ { UW1(2, 3) | UW2(4, 0), rt_receivex_timed },	RECEIVEX_TIMED },
	{ { UW1(2, 3) | UW2(4, 0), rt_evdrpx },		EVDRPX },
	{ { UR1(2, 3), rt_proxy_attach },         	PROXY_ATTACH },
	{ { 1, rt_proxy_detach },                 	PROXY_DETACH },
	{ { 1, rt_trigger },                      	PROXY_TRIGGER },
#ifdef CONFIG_RTAI_INTERNAL_LXRT_SUPPORT
	{ { UR1(2, 4) | UW1(3, 5), rt_Send },	 	RT_SEND },
	{ { UW1(2, 3) | UW2(4, 0), rt_Receive },	RT_RECEIVE },
	{ { UW1(2, 3) | UW2(4, 0), rt_Creceive }, 	RT_CRECEIVE },
	{ { UR1(2, 3), rt_Reply },		  	RT_REPLY },
	{ { UR1(2, 3), rt_Proxy_attach },	  	RT_PROXY_ATTACH },
	{ { 1, rt_Proxy_detach },		  	RT_PROXY_DETACH },
	{ { 1, rt_Trigger },			  	RT_TRIGGER },
	{ { 1, rt_Name_attach },		  	RT_NAME_ATTACH },
	{ { 1, rt_Name_detach },		  	RT_NAME_DETACH },
	{ { 0, rt_Name_locate },		  	RT_NAME_LOCATE },
#endif /* CONFIG_RTAI_INTERNAL_LXRT_SUPPORT */
	{ { 0, 0 },  		      			000 }
};

extern int set_rt_fun_entries(struct rt_native_fun_entry *entry);
extern void reset_rt_fun_entries(struct rt_native_fun_entry *entry);

int MSG_INIT_MODULE (void)
{
	return set_rt_fun_entries(rt_msg_entries);
}

void MSG_CLEANUP_MODULE (void)
{
	reset_rt_fun_entries(rt_msg_entries);
}

/* ++++++++++++++++++++++ END OF ALL INTERTASK MESSAGES +++++++++++++++++++++ */
