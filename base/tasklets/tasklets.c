/**
 * @ingroup tasklets
 * @file
 *
 * Implementation of the @ref tasklets "mini LXRT RTAI tasklets module".
 *
 * @author Paolo Mantegazza
 *
 * @note Copyright &copy;1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
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

/**
 * @defgroup tasklets mini RTAI LXRT tasklets module
 *
 * The MINI_RTAI_LXRT tasklets module adds an interesting new feature along the
 * line, pioneered by RTAI, of a symmetric usage of all its services inter-intra
 * kernel and user space, both for soft and hard real time applications.   In
 * such a way you have opened a whole spectrum of development and implementation
 * lanes, allowing maximum flexibility with uncompromized performances.
 *
 * The new services provided can be useful when you have many tasks, both in
 * kernel and user space, that must execute in soft/hard real time but do not
 * need any RTAI scheduler service that could lead to a task block. Such tasks
 * are here called tasklets and can be of two kinds: normal tasklets and timed
 * tasklets (timers).
 *
 * It must be noted that only timers should need to be made available both in
 * user and kernel space.   In fact normal tasklets in kernel space are nothing
 * but standard functions that can be directly executed by calling them, so
 * there would be no need for any special treatment.   However to maintain full
 * usage symmetry, and to ease any possible porting from one address space to
 * the other, also normal tasklet functions can be used in whatever address
 * space.
 *
 * Note that if, at this point, you are reminded to similar Linux kernel
 * services you are not totally wrong.  They are not exactly the same, because
 * of their symmetric availability in kernel and user space, but the basic idea
 * behind them  is clearly fairly similar.
 *
 * Tasklets should be used whenever the standard hard real time tasks available
 * with RTAI and LXRT schedulers can be a waist of resources and the execution
 * of simple, possibly timed, functions could often be more than
 * enough. Instances of such applications are timed polling and simple
 * Programmable Logic Controllers (PLC) like sequences of services.   Obviously
 * there are many others instances that can make it sufficient the use of
 * tasklets, either normal or timers.   In general such an approach can be a
 * very useful complement to fully featured tasks in controlling complex
 * machines and systems, both for basic and support services.
 *
 * It is remarked that the implementation found here for timed tasklets rely on
 * a server support task that executes the related timer functions, either in
 * oneshot or periodic mode, on the base of their time deadline and according to
 * their, user assigned, priority. Instead, as told above, plain tasklets are
 * just functions executed from kernel space; their execution needs no server
 * and is simply triggered by calling a given service function at due time,
 * either from a kernel task or interrupt handler requiring, or in charge of,
 * their execution when they are needed. Once more it is important to recall
 * that all non blocking RTAI scheduler services can be used in any tasklet
 * function.   Blocking services must absolutely be avoided.   They will
 * deadlock the timers server task, executing task or interrupt handler,
 * whichever applies, so that no more tasklet functions will be executed.
 *
 * User and kernel space MINI_RTAI_LXRT applications can cooperate and
 * synchronize by using shared memory. It has been called MINI_RTAI_LXRT because
 * it is a kind of light soft/hard real time server that can partially
 * substitute RTAI and LXRT in simple applications, i.e. if the constraints
 * hinted above are wholly satisfied. So MINI_RTAI_LXRT can be used in kernel
 * and user space, with any RTAI scheduler. Its implementations has been very
 * easy, as it is nothing but what its name implies.   LXRT made all the needed
 * tools already available.   In fact it duplicates a lot of LXRT so that its
 * final production version will be fully integrated with it, ASAP.   However,
 * at the moment, it cannot work with LXRT yet.
 *
 * Note that in user space you run within the memory of the process owning the
 * tasklet function so you MUST lock all of your processes memory in core, by
 * using mlockall, to prevent it being swapped out.   Also abundantly pre grow
 * your stack to the largest size needed during the execution of your
 * application, see mlockall usage in Linux manuals.
 *
 * The RTAI distribution contains many useful examples that demonstrate the use
 * of most services, both in kernel and user space.
 *
 *@{*/

#include <linux/module.h>
#include <linux/version.h>
#include <linux/proc_fs.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/interrupt.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <asm/rtai_sched.h>
#include <rtai_tasklets.h>
#include <rtai_lxrt.h>
#include <rtai_malloc.h>
#include <rtai_schedcore.h>

MODULE_LICENSE("GPL");

#ifdef CONFIG_RTAI_MALLOC
#define sched_malloc(size)      rt_malloc((size))
#define sched_free(adr)         rt_free((adr))
#else
#define sched_malloc(size)      kmalloc((size), GFP_KERNEL)
#define sched_free(adr)         kfree((adr))
#endif

DEFINE_LINUX_CR0

static struct rt_tasklet_struct timers_list =
{ &timers_list, &timers_list, RT_SCHED_LOWEST_PRIORITY, 0, RT_TIME_END, 0LL, 0, 0, 0 };

static struct rt_tasklet_struct tasklets_list =
{ &tasklets_list, &tasklets_list, };

static spinlock_t tasklets_lock = SPIN_LOCK_UNLOCKED;
static spinlock_t timers_lock   = SPIN_LOCK_UNLOCKED;

static struct rt_fun_entry rt_tasklet_fun[]  __attribute__ ((__unused__));

static struct rt_fun_entry rt_tasklet_fun[] = {
	{ 0, rt_init_tasklet },    		//   0
	{ 0, rt_delete_tasklet },    		//   1
	{ 0, rt_insert_tasklet },    		//   2
	{ 0, rt_remove_tasklet },    		//   3
	{ 0, rt_tasklet_use_fpu },   		//   4
	{ 0, rt_insert_timer },    		//   5
	{ 0, rt_remove_timer },    		//   6
	{ 0, rt_set_timer_priority },  		//   7
	{ 0, rt_set_timer_firing_time },   	//   8
	{ 0, rt_set_timer_period },   		//   9
	{ 0, rt_set_tasklet_handler },  	//  10
	{ 0, rt_set_tasklet_data },   		//  11
	{ 0, rt_exec_tasklet },   		//  12
	{ 0, rt_wait_tasklet_is_hard },	   	//  13
	{ 0, rt_set_tasklet_priority },  	//  14
	{ 0, rt_register_task },	  	//  15
};

/**
 * Insert a tasklet in the list of tasklets to be processed.
 *
 * rt_insert_tasklet insert a tasklet in the list of tasklets to be processed.
 *
 * @param tasklet is the pointer to the tasklet structure to be used to manage
 * the tasklet at hand.
 *
 * @param handler is the tasklet function to be executed.
 *
 * @param data is an unsigned long to be passed to the handler.   Clearly by an
 * appropriate type casting one can pass a pointer to whatever data structure
 * and type is needed.
 *
 * @param id is a unique unsigned number to be used to identify the tasklet
 * tasklet. It is typically required by the kernel space service, interrupt
 * handler ot task, in charge of executing a user space tasklet.   The support
 * functions nam2num() and num2nam() can be used for setting up id from a six
 * character string.
 *
 * @param pid is an integer that marks a tasklet either as being a kernel or
 * user space one. Despite its name you need not to know the pid of the tasklet
 * parent process in user space.   Simple use 0 for kernel space and 1 for user
 * space.
 *
 * @retval 0 on success
 * @return a negative number to indicate that an invalid handler address has
 * been passed.
 *
 * @note To be used only with RTAI24.x.xx.
 */
int rt_insert_tasklet(struct rt_tasklet_struct *tasklet, int priority, void (*handler)(unsigned long), unsigned long data, unsigned long id, int pid)
{
	unsigned long flags;

// tasklet initialization
	if (!handler || !id) {
		return -EINVAL;
	}
	tasklet->uses_fpu = 0;
	tasklet->priority = priority;
	tasklet->handler  = handler;
	tasklet->data     = data;
	tasklet->id       = id;
	if (!pid) {
		tasklet->task = 0;
	} else {
		(tasklet->task)->priority = priority;
		copy_to_user(tasklet->usptasklet, tasklet, sizeof(struct rt_tasklet_struct));
	}
// tasklet insertion tasklets_list
	flags = rt_spin_lock_irqsave(&tasklets_lock);
	tasklet->next             = &tasklets_list;
	tasklet->prev             = tasklets_list.prev;
	(tasklets_list.prev)->next = tasklet;
	tasklets_list.prev         = tasklet;
	rt_spin_unlock_irqrestore(flags, &tasklets_lock);
	return 0;
}

/**
 * Remove a tasklet in the list of tasklets to be processed.
 *
 * rt_remove_tasklet remove a tasklet from the list of tasklets to be processed.
 *
 * @param tasklet is the pointer to the tasklet structure to be used to manage
 * the tasklet at hand.
 *
 * @note To be used only with RTAI24.x.xx.
 */
void rt_remove_tasklet(struct rt_tasklet_struct *tasklet)
{
	if (tasklet->next != tasklet && tasklet->prev != tasklet) {
		unsigned long flags;
		flags = rt_spin_lock_irqsave(&tasklets_lock);
		(tasklet->next)->prev = tasklet->prev;
		(tasklet->prev)->next = tasklet->next;
		tasklet->next = tasklet->prev = tasklet;
		rt_spin_unlock_irqrestore(flags, &tasklets_lock);
	}
}

/**
 * Find a tasklet identified by its id.
 *
 * @param id is the unique unsigned long to be used to identify the tasklet.
 *
 * The support functions nam2num() and num2nam() can be used for setting up id
 * from a six character string.
 *
 * @return the pointer to a tasklet handler on success
 * @retval 0 to indicate that @a id is not a valid identifier so that the
 * related tasklet was not found.
 *
 * @note To be used only with RTAI24.x.xx.
 */
struct rt_tasklet_struct *rt_find_tasklet_by_id(unsigned long id)
{
	struct rt_tasklet_struct *tasklet;

	tasklet = &tasklets_list;
	while ((tasklet = tasklet->next) != &tasklets_list) {
		if (id == tasklet->id) {
			return tasklet;
		}
	}
	return 0;
}

/**
 * Exec a tasklet.
 *
 * rt_exec_tasklet execute a tasklet from the list of tasklets to be processed.
 *
 * @param tasklet is the pointer to the tasklet structure to be used to manage
 * the tasklet @a tasklet.
 *
 * Kernel space tasklets addresses are usually available directly and can be
 * easily be used in calling rt_tasklet_exec.   In fact one can call the related
 * handler directly without using such a support  function, which is mainly
 * supplied for symmetry and to ease the porting of applications from one space
 * to the other.
 *
 * User space tasklets instead must be first found within the tasklet list by
 * calling rt_find_tasklet_by_id() to get the tasklet address to be used
 * in rt_tasklet_exec().
 *
 * @note To be used only with RTAI24.x.xx.
 */
int rt_exec_tasklet(struct rt_tasklet_struct *tasklet)
{
	if (tasklet && tasklet->next != tasklet && tasklet->prev != tasklet) {
		if (!tasklet->task) {
			tasklet->handler(tasklet->data);
		} else {
			rt_task_resume(tasklet->task);
		}
		return 0;
	}
	return -EINVAL;
}

void rt_set_tasklet_priority(struct rt_tasklet_struct *tasklet, int priority)
{
	tasklet->priority = priority;
	if (tasklet->task) {
		(tasklet->task)->priority = priority;
	}
}

int rt_set_tasklet_handler(struct rt_tasklet_struct *tasklet, void (*handler)(unsigned long))
{
	if (!handler) {
		return -EINVAL;
	}
	tasklet->handler = handler;
	if (tasklet->task) {
		copy_to_user(tasklet->usptasklet, tasklet, sizeof(struct rt_tasklet_struct));
	}
	return 0;
}

void rt_set_tasklet_data(struct rt_tasklet_struct *tasklet, unsigned long data)
{
	tasklet->data = data;
	if (tasklet->task) {
		copy_to_user(tasklet->usptasklet, tasklet, sizeof(struct rt_tasklet_struct));
	}
}

RT_TASK *rt_tasklet_use_fpu(struct rt_tasklet_struct *tasklet, int use_fpu)
{
	tasklet->uses_fpu = use_fpu ? 1 : 0;
	return tasklet->task;
}

static RT_TASK timers_manager;

static inline void asgn_min_prio(void)
{
// find minimum priority in timers_struct 
	struct rt_tasklet_struct *timer;
	unsigned long flags;
	int priority;

	priority = (timer = timers_list.next)->priority;
	flags = rt_spin_lock_irqsave(&timers_lock);
	while ((timer = timer->next) != &timers_list) {
		if (timer->priority < priority) {
			priority = timer->priority;
		}
	}
	rt_spin_unlock_irqrestore(flags, &timers_lock);
	flags = rt_global_save_flags_and_cli();
	if (timers_manager.priority > priority) {
		timers_manager.priority = priority;
		if (timers_manager.state == RT_SCHED_READY) {
			rem_ready_task(&timers_manager);
			enq_ready_task(&timers_manager);
		}
	}
	rt_global_restore_flags(flags);
}

static inline void set_timer_firing_time(struct rt_tasklet_struct *timer, RTIME firing_time)
{
	if (timer->next != timer && timer->prev != timer) {
		unsigned long flags;
		struct rt_tasklet_struct *tmr;

		tmr = &timers_list;
		timer->firing_time = firing_time;
		flags = rt_spin_lock_irqsave(&timers_lock);
		(timer->next)->prev = timer->prev;
		(timer->prev)->next = timer->next;
		while (firing_time >= (tmr = tmr->next)->firing_time);
		timer->next     = tmr;
		timer->prev     = tmr->prev;
		(tmr->prev)->next = timer;
		tmr->prev         = timer;
		rt_spin_unlock_irqrestore(flags, &timers_lock);
	}
}

/**
 * Insert a timer in the list of timers to be processed.
 *
 * rt_insert_timer insert a timer in the list of timers to be processed.  Timers
 * can be either periodic or oneshot.   A periodic timer is reloaded at each
 * expiration so that it executes with the assigned periodicity.   A oneshot
 * timer is fired just once and then removed from the timers list. Timers can be
 * reinserted or modified within their handlers functions.
 *
 * @param timer is the pointer to the timer structure to be used to manage the
 * timer at hand.
 *
 * @param priority is the priority to be used to execute timers handlers when
 * more than one timer has to be fired at the same time.It can be assigned any
 * value such that: 0 < priority < RT_LOWEST_PRIORITY.
 *
 * @param firing_time is the time of the first timer expiration.
 *
 * @param period is the period of a periodic timer. A periodic timer keeps
 * calling its handler at  firing_time + k*period k = 0, 1.  To define a oneshot
 * timer simply use a null period.
 * 
 * @param handler is the timer function to be executed at each timer expiration.
 *
 * @param data is an unsigned long to be passed to the handler.   Clearly by a 
 * appropriate type casting one can pass a pointer to whatever data structure
 * and type is needed.
 *
 * @param pid is an integer that marks a timer either as being a kernel or user
 * space one. Despite its name you need not to know the pid of the timer parent
 * process in user space. Simple use 0 for kernel space and 1 for user space.
 *
 * @retval 0 on success
 * @retval EINVAL if @a handler is an invalid handler address
 *
 * @note To be used only with RTAI24.x.xx.
 */
int rt_insert_timer(struct rt_tasklet_struct *timer, int priority, RTIME firing_time, RTIME period, void (*handler)(unsigned long), unsigned long data, int pid)
{
	unsigned long flags;
	struct rt_tasklet_struct *tmr;

// timer initialization
	if (!handler) {
		return -EINVAL;
	}
	timer->uses_fpu    = 0;
	timer->priority    = priority;
	timer->firing_time = firing_time;
	timer->period      = period;
	timer->handler     = handler;
	timer->data        = data;
	if (!pid) {
		timer->task = 0;
	} else {
		(timer->task)->priority = priority;
		copy_to_user(timer->usptasklet, timer, sizeof(struct rt_tasklet_struct));
	}
// timer insertion in timers_list
	tmr = &timers_list;
	flags = rt_spin_lock_irqsave(&timers_lock);
	while (firing_time >= (tmr = tmr->next)->firing_time);
	timer->next     = tmr;
	timer->prev     = tmr->prev;
	(tmr->prev)->next = timer;
	tmr->prev         = timer;
	rt_spin_unlock_irqrestore(flags, &timers_lock);
// timers_manager priority inheritance
	if (timer->priority < timers_manager.priority) {
		timers_manager.priority = timer->priority;
	}
// timers_task deadline inheritance
	flags = rt_global_save_flags_and_cli();
	if (timers_list.next == timer && (timers_manager.state & RT_SCHED_DELAYED) && firing_time < timers_manager.resume_time) {
		timers_manager.resume_time = firing_time;
		rem_timed_task(&timers_manager);
		enq_timed_task(&timers_manager);
		rt_schedule();
	}
	rt_global_restore_flags(flags);
	return 0;
}

/**
 * Remove a timer in the list of timers to be processed.
 *
 * rt_remove_timer remove a timer from the list of the timers to be processed.
 *
 * @param timer is the pointer to the timer structure to be used to manage the
 * timer at hand.
 *
 * @note To be used only with RTAI24.x.xx.
 */
void rt_remove_timer(struct rt_tasklet_struct *timer)
{
	if (timer->next != timer && timer->prev != timer) {
		unsigned long flags;
		flags = rt_spin_lock_irqsave(&timers_lock);
		(timer->next)->prev = timer->prev;
		(timer->prev)->next = timer->next;
		timer->next = timer->prev = timer;
		rt_spin_unlock_irqrestore(flags, &timers_lock);
		asgn_min_prio();
	}
}

/**
 * Change the priority of an existing timer.
 *
 * rt_set_timer_priority change the priority of an existing timer.
 *
 * @param timer is the pointer to the timer structure to be used to manage the
 * timer at hand.
 *
 * @param priority is the priority to be used to execute timers handlers when
 * more than one timer has to be fired at the same time. It can be assigned any
 * value such that: 0 < priority < RT_LOWEST_PRIORITY.
 *
 * This function can be used within the timer handler.
 *
 * @note To be used only with RTAI24.x.xx.
 */
void rt_set_timer_priority(struct rt_tasklet_struct *timer, int priority)
{
	timer->priority = priority;
	if (timer->task) {
		(timer->task)->priority = priority;
	}
	asgn_min_prio();
}

/**
 * Change the firing time of a timer.
 * 
 * rt_set_timer_firing_time changes the firing time of a periodic timer
 * overloading any existing value, so that the timer next shoot will take place
 * at the new firing time. Note that if a oneshot timer has its firing time
 * changed after it has already expired this function has no effect. You
 * should reinsert it in the timer list with the new firing time.
 *
 * @param timer is the pointer to the timer structure to be used to manage the
 * timer at hand.
 *
 * @param firing_time is the new time of the first timer expiration.
 *
 * This function can be used within the timer handler.
 *
 * @retval 0 on success.
 *
 * @note To be used only with RTAI24.x.xx.
 */
void rt_set_timer_firing_time(struct rt_tasklet_struct *timer, RTIME firing_time)
{
	unsigned long flags;

	set_timer_firing_time(timer, firing_time);
	flags = rt_global_save_flags_and_cli();
	if (timers_list.next == timer && (timers_manager.state & RT_SCHED_DELAYED) && firing_time < timers_manager.resume_time) {
		timers_manager.resume_time = firing_time;
		rem_timed_task(&timers_manager);
		enq_timed_task(&timers_manager);
		rt_schedule();
	}
	rt_global_restore_flags(flags);
}

/**
 * Change the period of a timer.
 * 
 * rt_set_timer_period changes the period of a periodic timer. Note that the new
 * period will be used to pace the timer only after the expiration of the firing
 * time already in place. Using this function with a period different from zero
 * for a oneshot timer, that has not expired yet, will transform it into a
 * periodic timer.
 *
 * @param timer is the pointer to the timer structure to be used to manage the
 * timer at hand.
 *
 * @param period is the new period of a periodic timer.
 *
 * The macro #rt_fast_set_timer_period  can substitute the corresponding
 * function in kernel space if both the existing timer period and the new one
 * fit into an 32 bits integer.
 *
 * This function an be used within the timer handler.
 *
 * @retval 0 on success.
 *
 * @note To be used only with RTAI24.x.xx.
 */
void rt_set_timer_period(struct rt_tasklet_struct *timer, RTIME period)
{
	unsigned long flags;
	flags = rt_spin_lock_irqsave(&timers_lock);
	timer->period = period;
	rt_spin_unlock_irqrestore(flags, &timers_lock);
}

// the timers_manager task function

static void rt_timers_manager(int dummy)
{
	RTIME now;
	struct rt_tasklet_struct *tmr, *timer;
	unsigned long flags;
	int priority, used_fpu;

	while (1) {
		rt_sleep_until((timers_list.next)->firing_time);
		now = timers_manager.resume_time + tuned.timers_tol[0];
// find all the timers to be fired, in priority order
		while (1) {
			used_fpu = 0;
			tmr = timer = &timers_list;
			priority = RT_SCHED_LOWEST_PRIORITY;
			flags = rt_spin_lock_irqsave(&timers_lock);
			while ((tmr = tmr->next)->firing_time <= now) {
				if (tmr->priority < priority) {
					priority = (timer = tmr)->priority;
				}
			}
			timers_manager.priority = priority;
			rt_spin_unlock_irqrestore(flags, &timers_lock);
			if (timer == &timers_list) {
				break;
			}
			if (!timer->period) {
				flags = rt_spin_lock_irqsave(&timers_lock);
				(timer->next)->prev = timer->prev;
				(timer->prev)->next = timer->next;
				timer->next = timer->prev = timer;
				rt_spin_unlock_irqrestore(flags, &timers_lock);
			} else {
				set_timer_firing_time(timer, timer->firing_time + timer->period);
			}
			if (!timer->task) {
				if (!used_fpu && timer->uses_fpu) {
					used_fpu = 1;
					save_cr0_and_clts(linux_cr0);
					save_fpenv(timers_manager.fpu_reg);
				}
				timer->handler(timer->data);
			} else {
				rt_task_resume(timer->task);
			}
		}
		if (used_fpu) {
			restore_fpenv(timers_manager.fpu_reg);
			restore_cr0(linux_cr0);
		}
// set next timers_manager priority according to the highest priority timer
		asgn_min_prio();
// if no more timers in timers_struct remove timers_manager from tasks list
	}
}

/**
 * Init, in kernel space, a tasklet structure to be used in user space.
 *
 * rt_tasklet_init allocate a tasklet structure (struct rt_tasklet_struct) in
 * kernel space to be used for the management of a user space tasklet.
 *
 * This function is to be used only for user space tasklets. In kernel space
 * it is just an empty macro, as the user can, and must  allocate the related
 * structure directly, either statically or dynamically.
 *
 * @return the pointer to the tasklet structure the user space application must
 * use to access all its related services.
 */
struct rt_tasklet_struct *rt_init_tasklet(void)
{
	struct rt_tasklet_struct *tasklet;
	tasklet = sched_malloc(sizeof(struct rt_tasklet_struct));
	memset(tasklet, 0, sizeof(struct rt_tasklet_struct));
	return tasklet;
}

void rt_register_task(struct rt_tasklet_struct *tasklet, struct rt_tasklet_struct *usptasklet, RT_TASK *task)
{
	tasklet->task = task;
	tasklet->usptasklet = usptasklet;
	copy_to_user(usptasklet, tasklet, sizeof(struct rt_tasklet_struct));
}

void rt_wait_tasklet_is_hard(struct rt_tasklet_struct *tasklet, int thread)
{
	tasklet->thread = thread;
	while (!tasklet->task || !((tasklet->task)->state & RT_SCHED_SUSPENDED)) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(2);
	}
}

/**
 * Delete, in kernel space, a tasklet structure to be used in user space.
 *
 * rt_tasklet_delete free a tasklet structure (struct rt_tasklet_struct) in
 * kernel space that was allocated by rt_tasklet_init.
 *
 * @param tasklet is the pointer to the tasklet structure (struct
 * rt_tasklet_struct) returned by rt_tasklet_init.
 *
 * This function is to be used only for user space tasklets. In kernel space
 * it is just an empty macro, as the user can, and must  allocate the related
 * structure directly, either statically or dynamically.
 *
 * @note To be used only with RTAI24.x.xx.
 */
int rt_delete_tasklet(struct rt_tasklet_struct *tasklet)
{
	int pid, thread;

	rt_remove_tasklet(tasklet);
	tasklet->handler = 0;
	pid = ((tasklet->task)->lnxtsk)->pid;
	copy_to_user(tasklet->usptasklet, tasklet, sizeof(struct rt_tasklet_struct));
	rt_task_resume(tasklet->task);
	while (find_task_by_pid(pid)) {
		current->state = TASK_INTERRUPTIBLE;
		schedule_timeout(2);
	}
	thread = tasklet->thread;	
	sched_free(tasklet);
	return thread;	
}

static int tasklets_stacksize = STACK_SIZE;
MODULE_PARM(tasklets_stacksize, "i");

static RT_TASK *rt_base_linux_task;

int __rtai_tasklets_init(void)
{
	RT_TASK *rt_linux_tasks[NR_RT_CPUS];
	rt_base_linux_task = rt_get_base_linux_task(rt_linux_tasks);
	if (rt_sched_type() == RT_SCHED_MUP) {
		tuned.timers_tol[0] = tuned.timers_tol[timers_manager.runnable_on_cpus];
	}
        if(rt_base_linux_task->task_trap_handler[0]) {
                if(((int (*)(void *, int))rt_base_linux_task->task_trap_handler[0])(rt_tasklet_fun, TSKIDX)) {
                        printk("Recompile your module with a different index\n");
                        return -EACCES;
                }
        }
	rt_task_init(&timers_manager, rt_timers_manager, 0, tasklets_stacksize, RT_SCHED_LOWEST_PRIORITY, 0, 0);
	rt_task_resume(&timers_manager);
	printk(KERN_INFO "RTAI[tasklets]: loaded.\n");
	return 0;
}

void __rtai_tasklets_exit(void)
{
	rt_task_delete(&timers_manager);
        if(rt_base_linux_task->task_trap_handler[1]) {
                ((int (*)(void *, int))rt_base_linux_task->task_trap_handler[1])(rt_tasklet_fun, TSKIDX);
        }
	printk(KERN_INFO "RTAI[tasklets]: unloaded.\n");
}

/*@}*/

#ifndef CONFIG_RTAI_TASKLETS_BUILTIN
module_init(__rtai_tasklets_init);
module_exit(__rtai_tasklets_exit);
#endif /* !CONFIG_RTAI_TASKLETS_BUILTIN */

#ifdef CONFIG_KBUILD
EXPORT_SYMBOL(rt_insert_tasklet);
EXPORT_SYMBOL(rt_remove_tasklet);
EXPORT_SYMBOL(rt_find_tasklet_by_id);
EXPORT_SYMBOL(rt_exec_tasklet);
EXPORT_SYMBOL(rt_set_tasklet_priority);
EXPORT_SYMBOL(rt_set_tasklet_handler);
EXPORT_SYMBOL(rt_set_tasklet_data);
EXPORT_SYMBOL(rt_tasklet_use_fpu);
EXPORT_SYMBOL(rt_insert_timer);
EXPORT_SYMBOL(rt_remove_timer);
EXPORT_SYMBOL(rt_set_timer_priority);
EXPORT_SYMBOL(rt_set_timer_firing_time);
EXPORT_SYMBOL(rt_set_timer_period);
EXPORT_SYMBOL(rt_init_tasklet);
EXPORT_SYMBOL(rt_register_task);
EXPORT_SYMBOL(rt_wait_tasklet_is_hard);
EXPORT_SYMBOL(rt_delete_tasklet);
#endif /* CONFIG_KBUILD */
