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
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _RTAI_TASK_H
#define _RTAI_TASK_H

#include <nucleus/fusion.h>
#include <nucleus/thread.h>
#include <rtai/timer.h>

/* Creation flags. */
#define T_FPU     XNFPU
/* <!> Low bits must conflict with XNFPU|XNSHADOW. */
#define T_CPU(id) ((id) & 0xff)

/* Status flags. */
#define T_BLOCKED XNPEND
#define T_DELAYED XNDELAY
#define T_READY   XNREADY
#define T_DORMANT XNDORMANT
#define T_STARTED XNSTARTED
#define T_BOOST   XNBOOST
#define T_LOCK    XNLOCK
#define T_RRB     XNRRB

/* Task hook types. */
#define RT_HOOK_TSTART  XNHOOK_THREAD_START
#define RT_HOOK_TSWITCH XNHOOK_THREAD_SWITCH
#define RT_HOOK_TDELETE XNHOOK_THREAD_DELETE
#define RT_HOOK_TASKPTR(cookie) thread2rtask(cookie)

/* Priority range. */
#define T_HIPRIO  FUSION_LOW_PRI
#define T_LOPRIO  FUSION_HIGH_PRI
#define rtprio2xn(rtprio) (FUSION_HIGH_PRI - (rtprio) + 1)
#define xnprio2rt(xnprio) (T_LOPRIO + (xnprio) - 1)

typedef struct rt_task_placeholder {
    rt_handle_t opaque;
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

    xntimer_t timer;

#define link2rtask(laddr) \
((RT_TASK *)(((char *)laddr) - (int)(&((RT_TASK *)0)->link)))

    xnthread_t thread_base;

#define thread2rtask(taddr) \
((taddr) ? ((RT_TASK *)(((char *)(taddr)) - (int)(&((RT_TASK *)0)->thread_base))) : NULL)

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    int suspend_depth;

    int overrun;

    union { /* Saved args for current synch. wait operation. */

	struct {
	    int mode;
	    unsigned long mask;
	} event;

	struct rt_queue_msg *qmsg;

    } wait_args;

} RT_TASK;

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

int rt_task_bind(RT_TASK *task,
		 const char *name);

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

int rt_task_inquire (RT_TASK *task,
		     RT_TASK_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_TASK_H */
