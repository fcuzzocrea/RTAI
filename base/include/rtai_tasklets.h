/**
 * @ingroup tasklets
 * @file
 *
 * Interface of the @ref tasklets "mini LXRT RTAI tasklets module".
 *
 * @author Paolo Mantegazza
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

#ifndef _RTAI_TASKLETS_H
#define _RTAI_TASKLETS_H

/**
 * @addtogroup tasklets
 *@{*/

#include <rtai_types.h>
#include <rtai_sched.h>

#define TSKIDX  1

#define INIT	 	 0
#define DELETE		 1
#define TASK_INSERT 	 2
#define TASK_REMOVE	 3
#define USE_FPU	 	 4
#define TIMER_INSERT 	 5
#define TIMER_REMOVE	 6
#define SET_TASKLETS_PRI 7
#define SET_FIR_TIM	 8	
#define SET_PER	 	 9
#define SET_HDL		10
#define SET_DAT	 	11
#define EXEC_TASKLET    12
#define WAIT_IS_HARD	13
#define SET_TSK_PRI	14
#define REG_TASK   	15

struct rt_task_struct;

#define TASKLET_STACK_SIZE  8196

#ifdef __KERNEL__

struct rt_tasklet_struct {
	struct rt_tasklet_struct *next, *prev;
	int priority, uses_fpu, cpuid;
	RTIME firing_time, period;
	void (*handler)(unsigned long);
	unsigned long data, id;
	int thread;
	struct rt_task_struct *task;
	struct rt_tasklet_struct *usptasklet;
#ifdef  CONFIG_RTAI_LONG_TIMED_LIST
	rb_root_t rbr;
	rb_node_t rbn;
#endif
};

#ifdef __cplusplus
extern "C" {
#endif /* !__cplusplus */

int __rtai_tasklets_init(void);

void __rtai_tasklets_exit(void);

struct rt_tasklet_struct *rt_init_tasklet(void);

int rt_delete_tasklet(struct rt_tasklet_struct *tasklet);

int rt_insert_tasklet(struct rt_tasklet_struct *tasklet,
		      int priority,
		      void (*handler)(unsigned long),
		      unsigned long data,
		      unsigned long id,
		      int pid);

void rt_remove_tasklet(struct rt_tasklet_struct *tasklet);

struct rt_tasklet_struct *rt_find_tasklet_by_id(unsigned long id);

int rt_exec_tasklet(struct rt_tasklet_struct *tasklet);

void rt_set_tasklet_priority(struct rt_tasklet_struct *tasklet,
			     int priority);

int rt_set_tasklet_handler(struct rt_tasklet_struct *tasklet,
			   void (*handler)(unsigned long));

#define rt_fast_set_tasklet_handler(t, h) do { (t)->handler = (h); } while (0)

void rt_set_tasklet_data(struct rt_tasklet_struct *tasklet,
			 unsigned long data);

#define rt_fast_set_tasklet_data(t, d) \
do { \
   (t)->data = (d); \
} while (0)

/**
 * Notify the use of floating point operations within any tasklet/timer.
 *
 * rt_tasklets_use_fpu notifies that there is at least one tasklet/timer using
 * floating point calculations within its handler function.
 *
 * @param use_fpu set/resets the use of floating point calculations:
 * - a value different from 0 sets the use of floating point calculations ;
 * - a 0 value resets the no floating calculations state.
 *
 * Note that the use of floating calculations is assigned once for all and is
 * valid for all tasklets/timers. If just one handler needs it all of them
 * will have floating point support. An optimized floating point support,
 * i.e. on a per tasklet/timer base will add an unnoticeable performance
 * improvement on most CPUs. However such an optimization is not rule out a
 * priori, if anybody can prove it is really important.
 *
 * This function and macro can be used within the timer handler.
 *
 */
struct rt_task_struct *rt_tasklet_use_fpu(struct rt_tasklet_struct *tasklet,
					  int use_fpu);

/**
 * Init, in kernel space, a timed tasklet, simply called timer, structure
 * to be used in user space.
 *
 * rt_timer_init allocate a timer tasklet structure (struct rt_tasklet_struct)
 * in kernel space to be used for the management of a user space timer.
 *
 * This function is to be used only for user space timers. In kernel space
 * it is just an empty macro, as the user can, and must allocate the related
 * structure directly, either statically or dynamically.
 *
 * @return the pointer to the timer structure the user space application must
 * use to access all its related services.
 *
 */
#define rt_init_timer rt_init_tasklet 

/**
 * Delete, in kernel space, a timed tasklet, simply called timer, structure
 * to be used in user space.
 *
 * rt_timer_delete free a timer tasklet structure (struct rt_tasklet_struct) in
 * kernel space that was allocated by rt_timer_init.
 *
 * @param timer is a pointer to a timer tasklet structure (struct
 * rt_tasklet_struct).
 *
 * This function is to be used only for user space timers. In kernel space
 * it is just an empty macro, as the user can, and must allocate the related
 * structure directly, either statically or dynamically.
 *
 */
#define rt_delete_timer rt_delete_tasklet

int rt_insert_timer(struct rt_tasklet_struct *timer,
		    int priority,
		    RTIME firing_time,
		    RTIME period,
		    void (*handler)(unsigned long),
		    unsigned long data,
		    int pid);

void rt_remove_timer(struct rt_tasklet_struct *timer);

void rt_set_timer_priority(struct rt_tasklet_struct *timer,
			   int priority);

void rt_set_timer_firing_time(struct rt_tasklet_struct *timer,
			      RTIME firing_time);

void rt_set_timer_period(struct rt_tasklet_struct *timer,
			 RTIME period);

#define rt_fast_set_timer_period(t, p) \
do { \
   (t)->period = (p); \
} while (0)

/**
 * Change the timer handler.
 *
 * rt_set_timer_handler changes the timer handler function overloading any
 * existing value, so that at the next timer firing the new handler will be
 * used.   Note that if a oneshot timer has its handler changed after it has
 * already expired this function has no effect. You should reinsert it in the
 * timer list with the new handler.
 *
 * @param timer is the pointer to the timer structure to be used to manage the
 * timer at hand.
 *
 * @param handler is the new handler.
 *
 * The macro rt_fast_set_timer_handler can safely be used to substitute the
 * corresponding function in kernel space.
 *
 * This function and macro can be used within the timer handler.
 *
 * @retval 0 on success.
 *
 */
#define rt_set_timer_handler rt_set_tasklet_handler

#define rt_fast_set_timer_handler(t, h) do { (t)->handler = (h); } while (0)

/**
 * Change the data passed to a timer.
 *
 * rt_set_timer_data changes the timer data, overloading any existing value, so
 * that at the next timer firing the new data will be used.   Note that if a
 * oneshot timer has its data changed after it is already expired this function
 * has no effect.   You should reinsert it in the timer list with the new data.
 *
 * @param timer is the pointer to the timer structure to be used to manage the
 * timer at hand.
 *
 * @param data is the new data.
 *
 * The macro rt_fast_set_timer_data can safely be used substitute the
 * corresponding function in kernel space.
 *
 *  This function and macro can be used within the timer handler.
 *
 * @retval 0 on success.
 *
 */
#define rt_set_timer_data rt_set_tasklet_data

#define rt_fast_set_timer_data(t, d) do { (t)->data = (d); } while (0)

#define rt_timer_use_fpu rt_tasklet_use_fpu

void rt_wait_tasklet_is_hard(struct rt_tasklet_struct *tasklet,
			     int thread);

void rt_register_task(struct rt_tasklet_struct *tasklet,
		      struct rt_tasklet_struct *usptasklet,
		      struct rt_task_struct *task);
 
#ifdef __cplusplus
}
#endif /* __cplusplus */

#else /* !__KERNEL__ */

#include <sys/types.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <stdarg.h>

#include <rtai_usi.h>
#include <rtai_lxrt.h>

struct rt_tasklet_struct {
	struct rt_tasklet_struct *next, *prev;
	int priority, uses_fpu, cpuid;
	RTIME firing_time, period;
	void (*handler)(unsigned long);
	unsigned long data, id;
	int thread;
	struct rt_task_struct *task;
	struct rt_tasklet_struct *usptasklet;
#ifdef  CONFIG_RTAI_LONG_TIMED_LIST
	struct { void *rb_parent; int rb_color; void *rb_right, *rb_left; } rbn;
	struct { void *rb_node; } rbr;
#endif
};

#ifndef __SUPPORT_TASKLET__
#define __SUPPORT_TASKLET__

static int support_tasklet(void *tasklet)
{
	RT_TASK *task;
	struct rt_tasklet_struct usptasklet;
	struct { struct rt_tasklet_struct *tasklet; void *handler; } arg = { (struct rt_tasklet_struct *)tasklet, };

	if (!(task = rt_thread_init((unsigned long)arg.tasklet, 98, 0, SCHED_FIFO, 0xF))) {
		printf("CANNOT INIT SUPPORT TASKLET\n");
		return -1;
	} else {
		struct { struct rt_tasklet_struct *tasklet, *usptasklet; RT_TASK *task; } reg = { arg.tasklet, &usptasklet, task };
		rtai_lxrt(TSKIDX, sizeof(reg), REG_TASK, &reg);
	}

	mlockall(MCL_CURRENT | MCL_FUTURE);
	rt_make_hard_real_time();
	while (1) {
		rt_task_suspend(task);
		if ((arg.handler = (void*)usptasklet.handler)) {
			rtai_lxrt(TSKIDX, SIZARG, SET_HDL, &arg);
			usptasklet.handler(usptasklet.data);
		} else {
			break;
		}
	}
	rtai_sti();
	rt_make_soft_real_time();
	rt_task_delete(task);

	return 0;
}
#endif /* __SUPPORT_TASKLET__ */

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

RTAI_PROTO(struct rt_tasklet_struct *, rt_init_tasklet,(void))
{
	int is_hard;
	struct { struct rt_tasklet_struct *tasklet; long thread; } arg;

	arg.tasklet = (struct rt_tasklet_struct*)rtai_lxrt(TSKIDX, SIZARG, INIT, &arg).v[LOW];
	if ((is_hard = rt_is_hard_real_time(NULL))) {
		rt_make_soft_real_time();
	}
	arg.thread = rt_thread_create((void *)support_tasklet, arg.tasklet, TASKLET_STACK_SIZE);
//	arg.thread = clone(support_tasklet, sp + TASKLET_STACK_SIZE - 1, CLONE_VM | CLONE_FS | CLONE_FILES, arg.tasklet);
	rtai_lxrt(TSKIDX, SIZARG, WAIT_IS_HARD, &arg);
	if (is_hard) {
		rt_make_hard_real_time();
	}

	return arg.tasklet;
}

#define rt_init_timer rt_init_tasklet

RTAI_PROTO(void, rt_delete_tasklet,(struct rt_tasklet_struct *tasklet))
{
	int thread;
	struct { struct rt_tasklet_struct *tasklet; } arg = { tasklet };
	if ((thread = rtai_lxrt(TSKIDX, SIZARG, DELETE, &arg).i[LOW])) {
		rt_thread_join(thread);
	}
}

#define rt_delete_timer rt_delete_tasklet

RTAI_PROTO(int, rt_insert_timer,(struct rt_tasklet_struct *timer,
				 int priority,
				 RTIME firing_time,
				 RTIME period,
				 void (*handler)(unsigned long),
				 unsigned long data,
				 int pid))
{
	struct { struct rt_tasklet_struct *timer; long priority; RTIME firing_time; RTIME period; void (*handler)(unsigned long); unsigned long data; long pid; } arg = { timer, priority, firing_time, period, handler, data, pid };
	return rtai_lxrt(TSKIDX, SIZARG, TIMER_INSERT, &arg).i[LOW];
}

RTAI_PROTO(void, rt_remove_timer,(struct rt_tasklet_struct *timer))
{
	struct { struct rt_tasklet_struct *timer; } arg = { timer };
	rtai_lxrt(TSKIDX, SIZARG, TIMER_REMOVE, &arg);
}

RTAI_PROTO(void, rt_set_timer_priority,(struct rt_tasklet_struct *timer, int priority))
{
	struct { struct rt_tasklet_struct *timer; long priority; } arg = { timer, priority };
	rtai_lxrt(TSKIDX, SIZARG, SET_TASKLETS_PRI, &arg);
}

RTAI_PROTO(void, rt_set_timer_firing_time,(struct rt_tasklet_struct *timer, RTIME firing_time))
{
	struct { struct rt_tasklet_struct *timer; RTIME firing_time; } arg = { timer, firing_time };
	rtai_lxrt(TSKIDX, SIZARG, SET_FIR_TIM, &arg);
}

RTAI_PROTO(void, rt_set_timer_period,(struct rt_tasklet_struct *timer, RTIME period))
{
	struct { struct rt_tasklet_struct *timer; RTIME period; } arg = { timer, period };
	rtai_lxrt(TSKIDX, SIZARG, SET_PER, &arg);
}

RTAI_PROTO(int, rt_set_tasklet_handler,(struct rt_tasklet_struct *tasklet, void (*handler)(unsigned long)))
{
	struct { struct rt_tasklet_struct *tasklet; void (*handler)(unsigned long); } arg = { tasklet, handler };
	return rtai_lxrt(TSKIDX, SIZARG, SET_HDL, &arg).i[LOW];
}

#define rt_set_timer_handler rt_set_tasklet_handler

RTAI_PROTO(void, rt_set_tasklet_data,(struct rt_tasklet_struct *tasklet, unsigned long data))
{
	struct { struct rt_tasklet_struct *tasklet; unsigned long data; } arg = { tasklet, data };
	rtai_lxrt(TSKIDX, SIZARG, SET_DAT, &arg);
}

#define rt_set_timer_data rt_set_tasklet_data

RTAI_PROTO(RT_TASK *, rt_tasklet_use_fpu,(struct rt_tasklet_struct *tasklet, int use_fpu))
{
	RT_TASK *task;
	struct { struct rt_tasklet_struct *tasklet; long use_fpu; } arg = { tasklet, use_fpu };
	if ((task = (RT_TASK*)rtai_lxrt(TSKIDX, SIZARG, USE_FPU, &arg).v[LOW])) {
		rt_task_use_fpu(task, use_fpu);
	}
	return task;
}

#define rt_timer_use_fpu rt_tasklet_use_fpu

RTAI_PROTO(int, rt_insert_tasklet,(struct rt_tasklet_struct *tasklet,
				   int priority,
				   void (*handler)(unsigned long),
				   unsigned long data,
				   unsigned long id,
				   int pid))
{
	struct { struct rt_tasklet_struct *tasklet; long priority; void (*handler)(unsigned long); unsigned long data; unsigned long id; long pid; } arg = { tasklet, priority, handler, data, id, pid };
	return rtai_lxrt(TSKIDX, SIZARG, TASK_INSERT, &arg).i[LOW];
}

RTAI_PROTO(void, rt_set_tasklet_priority,(struct rt_tasklet_struct *tasklet, int priority))
{
	struct { struct rt_tasklet_struct *tasklet; long priority; } arg = { tasklet, priority };
	rtai_lxrt(TSKIDX, SIZARG, SET_TSK_PRI, &arg);
}

RTAI_PROTO(void, rt_remove_tasklet,(struct rt_tasklet_struct *tasklet))
{
	struct { struct rt_tasklet_struct *tasklet; } arg = { tasklet };
	rtai_lxrt(TSKIDX, SIZARG, TASK_REMOVE, &arg);
}

RTAI_PROTO(int, rt_exec_tasklet,(struct rt_tasklet_struct *tasklet))
{
	struct { struct rt_tasklet_struct *tasklet; } arg = { tasklet };
	return rtai_lxrt(TSKIDX, SIZARG, EXEC_TASKLET, &arg).i[LOW];
}

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __KERNEL__ */

/*@}*/

#endif /* !_RTAI_TASKLETS_H */
