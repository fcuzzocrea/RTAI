/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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

#ifndef _RTAI_TASK_H
#define _RTAI_TASK_H

#include <nucleus/fusion.h>
#include <nucleus/thread.h>
#include <rtai/timer.h>

/* Creation flags. */
#define T_FPU     XNFPU
#define T_SUSP    XNSUSP
/* <!> High bits must not conflict with XNFPU|XNSHADOW|XNSHIELD|XNSUSP. */
#define T_CPU(cpu) (1 << (24 + (cpu & 0xff))) /* Up to 8 cpus [0-7] */

/* Status/mode flags. */
#define T_BLOCKED XNPEND
#define T_DELAYED XNDELAY
#define T_READY   XNREADY
#define T_DORMANT XNDORMANT
#define T_STARTED XNSTARTED
#define T_BOOST   XNBOOST
#define T_LOCK    XNLOCK
#define T_RRB     XNRRB
#define T_NOSIG   XNASDI
#define T_SHIELD  XNSHIELD

/* Task hook types. */
#define T_HOOK_START  XNHOOK_THREAD_START
#define T_HOOK_SWITCH XNHOOK_THREAD_SWITCH
#define T_HOOK_DELETE XNHOOK_THREAD_DELETE
#define T_DESC(cookie) thread2rtask(cookie)

/* Priority range (POSIXish, same bounds as fusion). */
#define T_LOPRIO  FUSION_LOW_PRIO
#define T_HIPRIO  FUSION_HIGH_PRIO

typedef struct rt_task_placeholder {
    rt_handle_t opaque;
    unsigned long opaque2;
} RT_TASK_PLACEHOLDER;

#define RT_TASK_STATUS_MASK \
(T_FPU|T_BLOCKED|T_DELAYED|T_READY|T_DORMANT|T_STARTED|T_BOOST|T_LOCK|T_RRB)

struct rt_queue_msg;

typedef struct rt_task_info {

    int bprio;			/* !< Base priority. */

    int cprio;			/* !< Current priority. */

    unsigned status;		/* !< Status. */

    RTIME relpoint;		/* !< Periodic release point. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_TASK_INFO;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_TASK_MAGIC 0x55550101

typedef struct rt_task {

    unsigned magic;   /* !< Magic code - must be first */

    xnholder_t link;

#define link2rtask(laddr) \
((RT_TASK *)(((char *)laddr) - (int)(&((RT_TASK *)0)->link)))

    xntimer_t timer;

    xnthread_t thread_base;

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    int suspend_depth;

    int overrun;

    xnarch_cpumask_t affinity;

    union { /* Saved args for current synch. wait operation. */

	struct {
	    int mode;
	    unsigned long mask;
	} event;

	struct rt_queue_msg *qmsg;

	struct {
	    size_t size;
	    void *block;
	} heap;
	
	struct {
	    const char *key;
	} registry;

    } wait_args;

} RT_TASK;

static inline RT_TASK *thread2rtask (xnthread_t *t)
{
    return t ? ((RT_TASK *)(((char *)(t)) - (int)(&((RT_TASK *)0)->thread_base))) : NULL;
}

#define rtai_current_task() thread2rtask(xnpod_current_thread())

#ifdef __cplusplus
extern "C" {
#endif

int __task_pkg_init(void);

void __task_pkg_cleanup(void);

/* Public kernel interface */

int rt_task_add_hook(int type,
		     void (*routine)(void *cookie));

int rt_task_remove_hook(int type,
			void (*routine)(void *cookie));

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_TASK_PLACEHOLDER RT_TASK;

#ifdef __cplusplus
extern "C" {
#endif

int rt_task_bind(RT_TASK *task,
		 const char *name);

static inline int rt_task_unbind (RT_TASK *task)

{
    task->opaque = RT_HANDLE_INVALID;
    return 0;
}

#ifdef __cplusplus
}
#endif

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface */

int rt_task_create(RT_TASK *task,
		   const char *name,
		   int stksize,
		   int prio,
		   int mode);

int rt_task_start(RT_TASK *task,
		  void (*fun)(void *cookie),
		  void *cookie);

int rt_task_suspend(RT_TASK *task);

int rt_task_resume(RT_TASK *task);

int rt_task_delete(RT_TASK *task);

int rt_task_yield(void);

int rt_task_set_periodic(RT_TASK *task,
			 RTIME idate,
			 RTIME period);

int rt_task_wait_period(void);

int rt_task_set_priority(RT_TASK *task,
			 int prio);

int rt_task_sleep(RTIME delay);

int rt_task_sleep_until(RTIME date);

int rt_task_unblock(RT_TASK *task);

int rt_task_inquire(RT_TASK *task,
		     RT_TASK_INFO *info);

int rt_task_catch(void (*handler)(rt_sigset_t));

int rt_task_notify(RT_TASK *task,
		   rt_sigset_t signals);

int rt_task_set_mode(int clrmask,
		     int setmask,
		     int *mode_r);

RT_TASK *rt_task_self(void);

int rt_task_slice(RT_TASK *task,
		  RTIME quantum);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_TASK_H */
