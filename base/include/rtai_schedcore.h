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

#ifndef _RTAI_SCHEDCORE_H
#define _RTAI_SCHEDCORE_H

#include <rtai_version.h>
#include <rtai_lxrt.h>
#include <rtai_sched.h>
#include <rtai_malloc.h>
#include <rtai_trace.h>
#include <rtai_leds.h>
#include <rtai_sem.h>
#include <rtai_rwl.h>
#include <rtai_spl.h>
#include <rtai_scb.h>
#include <rtai_mbx.h>
#include <rtai_msg.h>
#include <rtai_tbx.h>
#include <rtai_mq.h>
#include <rtai_bits.h>
#include <rtai_wd.h>
#include <rtai_tasklets.h>
#include <rtai_fifos.h>
#include <rtai_netrpc.h>
#include <rtai_shm.h>
#include <rtai_usi.h>

#ifdef __KERNEL__

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/timex.h>
#include <linux/sched.h>
#include <asm/param.h>
#include <asm/system.h>
#include <asm/io.h>

extern RT_TASK rt_smp_linux_task[];

extern RT_TASK *rt_smp_current[];

extern RTIME rt_smp_time_h[];

extern int rt_smp_oneshot_timer[];

#ifdef CONFIG_RTAI_MALLOC
#define sched_malloc(size)		rt_malloc((size))
#define sched_free(adr)			rt_free((adr))
#ifndef CONFIG_RTAI_MALLOC_BUILTIN
#define sched_mem_init()
#define sched_mem_end()
#else  /* CONFIG_RTAI_MALLOC_BUILTIN */
#define sched_mem_init() \
	{ if(__rtai_heap_init() != 0) { \
                return(-ENOMEM); \
        } }
#define sched_mem_end()		__rtai_heap_exit()
#endif /* !CONFIG_RTAI_MALLOC_BUILTIN */
#define call_exit_handlers(task)	        __call_exit_handlers(task)
#define set_exit_handler(task, fun, arg1, arg2)	__set_exit_handler(task, fun, arg1, arg2)
#else  /* !CONFIG_RTAI_MALLOC */
#define sched_malloc(size)	kmalloc((size), GFP_KERNEL)
#define sched_free(adr)		kfree((adr))
#define sched_mem_init()
#define sched_mem_end()
#define call_exit_handlers(task)
#define set_exit_handler(task, fun, arg1, arg2)
#endif /* CONFIG_RTAI_MALLOC */

#define RT_SEM_MAGIC 0x3f83ebb  // nam2num("rtsem")

#define SEM_ERR (0xFfff)

#define MSG_ERR ((RT_TASK *)0xFfff)

#define NOTHING ((void *)0)

#define SOMETHING ((void *)1)

#define SEMHLF 0x0000FFFF
#define RPCHLF 0xFFFF0000
#define RPCINC 0x00010000

#define DECLARE_RT_CURRENT int cpuid; RT_TASK *rt_current
#define ASSIGN_RT_CURRENT rt_current = rt_smp_current[cpuid = rtai_cpuid()]
#define RT_CURRENT rt_smp_current[rtai_cpuid()]

#define MAX_LINUX_RTPRIO  99
#define MIN_LINUX_RTPRIO   1

#ifdef CONFIG_RTAI_SCHED_ISR_LOCK
void rtai_handle_isched_lock(int nesting);
#endif /* CONFIG_RTAI_SCHED_ISR_LOCK */

#ifdef CONFIG_SMP
#define rt_time_h (rt_smp_time_h[cpuid])
#define oneshot_timer (rt_smp_oneshot_timer[cpuid])
#define rt_linux_task (rt_smp_linux_task[cpuid])
#else
#define rt_time_h (rt_smp_time_h[0])
#define oneshot_timer (rt_smp_oneshot_timer[0])
#define rt_linux_task (rt_smp_linux_task[0])
#endif

#ifdef CONFIG_SMP

static inline void send_sched_ipi(unsigned long dest)
{
	_send_sched_ipi(dest);
}

#define RT_SCHEDULE_MAP(schedmap) \
	do { if (schedmap) send_sched_ipi(schedmap); } while (0)

#define RT_SCHEDULE_MAP_BOTH(schedmap) \
	do { if (schedmap) send_sched_ipi(schedmap); rt_schedule(); } while (0)

#define RT_SCHEDULE(task, cpuid) \
	do { \
		if ((task)->runnable_on_cpus != (cpuid)) { \
			send_sched_ipi(1 << (task)->runnable_on_cpus); \
		} else { \
			rt_schedule(); \
		} \
	} while (0)

#define RT_SCHEDULE_BOTH(task, cpuid) \
	{ \
		if ((task)->runnable_on_cpus != (cpuid)) { \
			send_sched_ipi(1 << (task)->runnable_on_cpus); \
		} \
		rt_schedule(); \
	}

#else /* !CONFIG_SMP */

#define send_sched_ipi(dest)

#define RT_SCHEDULE_MAP_BOTH(schedmap)  rt_schedule()

#define RT_SCHEDULE_MAP(schedmap)       rt_schedule()

#define RT_SCHEDULE(task, cpuid)        rt_schedule()

#define RT_SCHEDULE_BOTH(task, cpuid)   rt_schedule()

#endif /* CONFIG_SMP */

#define BASE_SOFT_PRIORITY 1000000000

#define TASK_HARDREALTIME  TASK_UNINTERRUPTIBLE

static inline void enq_ready_edf_task(RT_TASK *ready_task)
{
	RT_TASK *task;
#ifdef CONFIG_SMP
	task = rt_smp_linux_task[ready_task->runnable_on_cpus].rnext;
#else
	task = rt_smp_linux_task[0].rnext;
#endif
	while (task->policy < 0 && ready_task->period >= task->period) {
		task = task->rnext;
	}
	task->rprev = (ready_task->rprev = task->rprev)->rnext = ready_task;
	ready_task->rnext = task;
}

#define MAX_WAKEUP_SRQ (2 << 6)

struct klist_t { volatile int srq, in, out; void *task[MAX_WAKEUP_SRQ]; };
extern struct klist_t wake_up_srq;

static inline void enq_ready_task(RT_TASK *ready_task)
{
	RT_TASK *task;
	if (ready_task->is_hard) {
#ifdef CONFIG_SMP
		task = rt_smp_linux_task[ready_task->runnable_on_cpus].rnext;
#else
		task = rt_smp_linux_task[0].rnext;
#endif
		while (ready_task->priority >= task->priority) {
			if ((task = task->rnext)->priority < 0) break;
		}
		task->rprev = (ready_task->rprev = task->rprev)->rnext = ready_task;
		ready_task->rnext = task;
	} else {
		ready_task->state |= RT_SCHED_SFTRDY;
		wake_up_srq.task[wake_up_srq.in] = ready_task->lnxtsk;
		wake_up_srq.in = (wake_up_srq.in + 1) & (MAX_WAKEUP_SRQ - 1);
		rt_pend_linux_srq(wake_up_srq.srq);
	}
}

static inline int renq_ready_task(RT_TASK *ready_task, int priority)
{
	int retval;
	if ((retval = ready_task->priority != priority)) {
		ready_task->priority = priority;
		if (ready_task->state == RT_SCHED_READY) {
			(ready_task->rprev)->rnext = ready_task->rnext;
			(ready_task->rnext)->rprev = ready_task->rprev;
			enq_ready_task(ready_task);
		}
	}
	return retval;
}

static inline int renq_current(RT_TASK *rt_current, int priority)
{
	int retval;
	if ((retval = rt_current->priority != priority)) {
		rt_current->priority = priority;
		(rt_current->rprev)->rnext = rt_current->rnext;
		(rt_current->rnext)->rprev = rt_current->rprev;
		enq_ready_task(rt_current);
	}
	return retval;
}

static inline void rem_ready_task(RT_TASK *task)
{
	if (task->state == RT_SCHED_READY) {
		if (!task->is_hard) {
			(task->lnxtsk)->state = TASK_HARDREALTIME;
		}
		(task->rprev)->rnext = task->rnext;
		(task->rnext)->rprev = task->rprev;
	}
}

static inline void rem_ready_current(RT_TASK *rt_current)
{
	if (!rt_current->is_hard) {
		(rt_current->lnxtsk)->state = TASK_HARDREALTIME;
	}
	(rt_current->rprev)->rnext = rt_current->rnext;
	(rt_current->rnext)->rprev = rt_current->rprev;
}

static inline void enq_timed_task(RT_TASK *timed_task)
{
	RT_TASK *task;
#ifdef CONFIG_SMP
	task = rt_smp_linux_task[timed_task->runnable_on_cpus].tnext;
#else
	task = rt_smp_linux_task[0].tnext;
#endif
	while (timed_task->resume_time > task->resume_time) {
		task = task->tnext;
	}
	task->tprev = (timed_task->tprev = task->tprev)->tnext = timed_task;
	timed_task->tnext = task;
}

static inline void wake_up_timed_tasks(int cpuid)
{
	RT_TASK *task;
#ifdef CONFIG_SMP
	task = rt_smp_linux_task[cpuid].tnext;
#else
	task = rt_smp_linux_task[0].tnext;
#endif
	while (task->resume_time <= rt_time_h) {
		if ((task->state &= ~(RT_SCHED_DELAYED | RT_SCHED_SUSPENDED | RT_SCHED_SEMAPHORE | RT_SCHED_RECEIVE | RT_SCHED_SEND | RT_SCHED_RPC | RT_SCHED_RETURN | RT_SCHED_MBXSUSP)) == RT_SCHED_READY) {
			if (task->policy < 0) {
				enq_ready_edf_task(task);
			} else {
				enq_ready_task(task);
			}
		}
		task = task->tnext;
	}
#ifdef CONFIG_SMP
	rt_smp_linux_task[cpuid].tnext = task;
	task->tprev = &rt_smp_linux_task[cpuid];
#else
	rt_smp_linux_task[0].tnext = task;
	task->tprev = &rt_smp_linux_task[0];
#endif
}

static inline void rem_timed_task(RT_TASK *task)
{
	if ((task->state & RT_SCHED_DELAYED)) {
		(task->tprev)->tnext = task->tnext;
		(task->tnext)->tprev = task->tprev;
	}
}

#define get_time() rt_get_time()
#if 0
static inline RTIME get_time(void)
{
#ifdef CONFIG_SMP
	int cpuid;
	return rt_smp_oneshot_timer[cpuid = rtai_cpuid()] ? rdtsc() : rt_smp_times[cpuid].tick_time;
#else
	return rt_smp_oneshot_timer[0] ? rdtsc() : rt_smp_times[0].tick_time;
#endif
}
#endif

static inline void enqueue_blocked(RT_TASK *task, QUEUE *queue, int qtype)
{
        QUEUE *q;
        task->blocked_on = (q = queue);
        if (!qtype) {
                while ((q = q->next) != queue && (q->task)->priority <= task->priority);
        }
        q->prev = (task->queue.prev = q->prev)->next  = &(task->queue);
        task->queue.next = q;
}


static inline void dequeue_blocked(RT_TASK *task)
{
        task->prio_passed_to     = NOTHING;
        (task->queue.prev)->next = task->queue.next;
        (task->queue.next)->prev = task->queue.prev;
        task->blocked_on         = NOTHING;
}

static __volatile__ inline unsigned long pass_prio(RT_TASK *to, RT_TASK *from)
{
        QUEUE *q;
#ifdef CONFIG_SMP
        unsigned long schedmap;
        schedmap = 0;
#endif
        from->prio_passed_to = to;
        while (to && to->priority > from->priority) {
                to->priority = from->priority;
		if (to->state == RT_SCHED_READY) {
                        (to->rprev)->rnext = to->rnext;
                        (to->rnext)->rprev = to->rprev;
                        enq_ready_task(to);
#ifdef CONFIG_SMP
                        set_bit(to->runnable_on_cpus & 0x1F, &schedmap);
#endif
                } else if ((q = to->blocked_on) && !((to->state & RT_SCHED_SEMAPHORE) &&
 ((SEM *)q)->qtype)) {
                        (to->queue.prev)->next = to->queue.next;
                        (to->queue.next)->prev = to->queue.prev;
                        while ((q = q->next) != to->blocked_on && (q->task)->priority <= to->priority);
                        q->prev = (to->queue.prev = q->prev)->next  = &(to->queue);
                        to->queue.next = q;
                }
                to = to->prio_passed_to;
	}
#ifdef CONFIG_SMP
	return schedmap;
#else
	return 0;
#endif
}

static inline RT_TASK *_rt_whoami(void)
{
#ifdef CONFIG_SMP
        RT_TASK *rt_current;
        unsigned long flags;
        flags = rt_global_save_flags_and_cli();
        rt_current = RT_CURRENT;
        rt_global_restore_flags(flags);
        return rt_current;
#else
        return rt_smp_current[0];
#endif
}

static inline void __call_exit_handlers(RT_TASK *task)
{
	XHDL *pt, *tmp;

	pt = task->ExitHook; // Initialise ExitHook in rt_task_init()
	while ( pt ) {
		(*pt->fun) (pt->arg1, pt->arg2);
		tmp = pt;
		pt  = pt->nxt;
		rt_free(tmp);
	}
	task->ExitHook = 0;
}

static inline XHDL *__set_exit_handler(RT_TASK *task, void (*fun) (void *, int), void *arg1, int arg2)
{
	XHDL *p;

	// exit handler functions are automatically executed at terminattion time by rt_task_delete()
	// in the reverse order they were created (like C++ destructors behave).
	if (task->magic != RT_TASK_MAGIC) return 0;
	if (!(p = (XHDL *) rt_malloc (sizeof(XHDL)))) return 0;
	p->fun  = fun;
	p->arg1 = arg1;
	p->arg2 = arg2;
	p->nxt  = task->ExitHook;
	return (task->ExitHook = p);
}

static inline int rtai_init_features (void)

{
#ifdef CONFIG_RTAI_LEDS_BUILTIN
    __rtai_leds_init();
#endif /* CONFIG_RTAI_LEDS_BUILTIN */
#ifdef CONFIG_RTAI_SEM_BUILTIN
    __rtai_sem_init();
#endif /* CONFIG_RTAI_SEM_BUILTIN */
#ifdef CONFIG_RTAI_MSG_BUILTIN
    __rtai_msg_init();
#endif /* CONFIG_RTAI_MSG_BUILTIN */
#ifdef CONFIG_RTAI_MBX_BUILTIN
    __rtai_mbx_init();
#endif /* CONFIG_RTAI_MBX_BUILTIN */
#ifdef CONFIG_RTAI_TBX_BUILTIN
    __rtai_tbx_init();
#endif /* CONFIG_RTAI_TBX_BUILTIN */
#ifdef CONFIG_RTAI_MQ_BUILTIN
    __rtai_mq_init();
#endif /* CONFIG_RTAI_MQ_BUILTIN */
#ifdef CONFIG_RTAI_BITS_BUILTIN
    __rtai_bits_init();
#endif /* CONFIG_RTAI_BITS_BUILTIN */
#ifdef CONFIG_RTAI_TASKLETS_BUILTIN
    __rtai_tasklets_init();
#endif /* CONFIG_RTAI_TASKLETS_BUILTIN */
#ifdef CONFIG_RTAI_FIFOS_BUILTIN
    __rtai_fifos_init();
#endif /* CONFIG_RTAI_FIFOS_BUILTIN */
#ifdef CONFIG_RTAI_NETRPC_BUILTIN
    __rtai_netrpc_init();
#endif /* CONFIG_RTAI_NETRPC_BUILTIN */
#ifdef CONFIG_RTAI_SHM_BUILTIN
    __rtai_shm_init();
#endif /* CONFIG_RTAI_SHM_BUILTIN */
#ifdef CONFIG_RTAI_USI_BUILTIN
    __rtai_usi_init();
#endif /* CONFIG_RTAI_USI_BUILTIN */
#ifdef CONFIG_RTAI_MATH_BUILTIN
    __rtai_math_init();
#endif /* CONFIG_RTAI_MATH_BUILTIN */

	return 0;
}

static inline void rtai_cleanup_features (void) {

#ifdef CONFIG_RTAI_MATH_BUILTIN
    __rtai_math_exit();
#endif /* CONFIG_RTAI_MATH_BUILTIN */
#ifdef CONFIG_RTAI_USI_BUILTIN
    __rtai_usi_exit();
#endif /* CONFIG_RTAI_USI_BUILTIN */
#ifdef CONFIG_RTAI_SHM_BUILTIN
    __rtai_shm_exit();
#endif /* CONFIG_RTAI_SHM_BUILTIN */
#ifdef CONFIG_RTAI_NETRPC_BUILTIN
    __rtai_netrpc_exit();
#endif /* CONFIG_RTAI_NETRPC_BUILTIN */
#ifdef CONFIG_RTAI_FIFOS_BUILTIN
    __rtai_fifos_exit();
#endif /* CONFIG_RTAI_FIFOS_BUILTIN */
#ifdef CONFIG_RTAI_TASKLETS_BUILTIN
    __rtai_tasklets_exit();
#endif /* CONFIG_RTAI_TASKLETS_BUILTIN */
#ifdef CONFIG_RTAI_BITS_BUILTIN
    __rtai_bits_exit();
#endif /* CONFIG_RTAI_BITS_BUILTIN */
#ifdef CONFIG_RTAI_MQ_BUILTIN
    __rtai_mq_exit();
#endif /* CONFIG_RTAI_MQ_BUILTIN */
#ifdef CONFIG_RTAI_TBX_BUILTIN
    __rtai_tbx_exit();
#endif /* CONFIG_RTAI_TBX_BUILTIN */
#ifdef CONFIG_RTAI_MBX_BUILTIN
    __rtai_mbx_exit();
#endif /* CONFIG_RTAI_MBX_BUILTIN */
#ifdef CONFIG_RTAI_MSG_BUILTIN
    __rtai_msg_exit();
#endif /* CONFIG_RTAI_MSG_BUILTIN */
#ifdef CONFIG_RTAI_SEM_BUILTIN
    __rtai_sem_exit();
#endif /* CONFIG_RTAI_SEM_BUILTIN */
#ifdef CONFIG_RTAI_LEDS_BUILTIN
    __rtai_leds_exit();
#endif /* CONFIG_RTAI_LEDS_BUILTIN */
}

int rt_check_current_stack(void);

int rt_kthread_init(RT_TASK *task,
		    void (*rt_thread)(int),
		    int data,
		    int stack_size,
		    int priority,
		    int uses_fpu,
		    void(*signal)(void));

int rt_kthread_init_cpuid(RT_TASK *task,
			  void (*rt_thread)(int),
			  int data,
			  int stack_size,
			  int priority,
			  int uses_fpu,
			  void(*signal)(void),
			  unsigned int cpuid);

#endif /* __KERNEL__ */

#endif /* !_RTAI_SCHEDCORE_H */
