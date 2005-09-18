/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <stdio.h>
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <pthread.h>
#include <signal.h>
#include <limits.h>
#include <rtai/syscall.h>
#include <rtai/task.h>

extern pthread_key_t __rtai_tskey;

extern int __rtai_muxid;

/* Public RTAI interface. */

struct rt_task_iargs {
    RT_TASK *task;
    const char *name;
    int prio;
    int mode;
    xncompletion_t *completionp;
};

static void rt_task_sigharden (int sig)
{
    XENOMAI_SYSCALL1(__xn_sys_migrate,XENOMAI_RTAI_DOMAIN);
}

static void *rt_task_trampoline (void *cookie)

{
    struct rt_task_iargs *iargs = (struct rt_task_iargs *)cookie;
    void (*entry)(void *cookie);
    struct sched_param param;
    struct rt_arg_bulk bulk;
    long err;

    /* Ok, this looks like weird, but we need this. */
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);

    signal(SIGCHLD,&rt_task_sigharden);

    bulk.a1 = (u_long)iargs->task;
    bulk.a2 = (u_long)iargs->name;
    bulk.a3 = (u_long)iargs->prio;
    bulk.a4 = (u_long)iargs->mode;
    bulk.a5 = (u_long)pthread_self();

    err = XENOMAI_SKINCALL2(__rtai_muxid,
			    __rtai_task_create,
			    &bulk,
			    iargs->completionp);
    if (err)
	goto fail;

    /* Wait on the barrier for the task to be started. The barrier
       could be released in order to process Linux signals while the
       RTAI shadow is still dormant; in such a case, resume wait. */

    do
	err = XENOMAI_SYSCALL2(__xn_sys_barrier,&entry,&cookie);
    while (err == -EINTR);

    if (!err)
	entry(cookie);

 fail:

    pthread_exit((void *)err);
}

int rt_task_create (RT_TASK *task,
		    const char *name,
		    int stksize,
		    int prio,
		    int mode)
{
    struct rt_task_iargs iargs;
    xncompletion_t completion;
    struct sched_param param;
    pthread_attr_t thattr;
    pthread_t thid;
    int err;

    /* Migrate this thread to the Linux domain since we are about to
       issue a series of regular kernel syscalls in order to create
       the new Linux thread, which in turn will be mapped to a
       real-time shadow. */

    XENOMAI_SYSCALL1(__xn_sys_migrate,XENOMAI_LINUX_DOMAIN);

    completion.syncflag = 0;
    completion.pid = -1;

    iargs.task = task;
    iargs.name = name;
    iargs.prio = prio;
    iargs.mode = mode;
    iargs.completionp = &completion;

    pthread_attr_init(&thattr);

    if (stksize == 0)
	stksize = PTHREAD_STACK_MIN * 4;
    else if (stksize < PTHREAD_STACK_MIN)
	stksize = PTHREAD_STACK_MIN;

    pthread_attr_setstacksize(&thattr,stksize);
    pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedpolicy(&thattr,SCHED_FIFO);
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&thattr,&param);

    err = pthread_create(&thid,&thattr,&rt_task_trampoline,&iargs);

    if (err)
	return -err;

    /* Wait for sync with rt_task_trampoline() */
    return XENOMAI_SYSCALL1(__xn_sys_completion,&completion);
}

int rt_task_start (RT_TASK *task,
		   void (*entry)(void *cookie),
		   void *cookie)
{
    return XENOMAI_SKINCALL3(__rtai_muxid,
			     __rtai_task_start,
			     task,
			     entry,
			     cookie);
}

int rt_task_shadow (RT_TASK *task,
		    const char *name,
		    int prio,
		    int mode)
{
    struct rt_arg_bulk bulk;

    signal(SIGCHLD,&rt_task_sigharden);

    bulk.a1 = (u_long)task;
    bulk.a2 = (u_long)name;
    bulk.a3 = (u_long)prio;
    bulk.a4 = (u_long)mode;
    bulk.a5 = (u_long)pthread_self();

    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_create,
			     &bulk,
			     NULL);
}

int rt_task_bind (RT_TASK *task,
		  const char *name,
		  RTIME timeout)
{
    return XENOMAI_SKINCALL3(__rtai_muxid,
			     __rtai_task_bind,
			     task,
			     name,
			     &timeout);
}

int rt_task_suspend (RT_TASK *task)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_task_suspend,
			     task);
}

int rt_task_resume (RT_TASK *task)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_task_resume,
			     task);
}

int rt_task_delete (RT_TASK *task)

{
    int err = XENOMAI_SKINCALL1(__rtai_muxid,
				__rtai_task_delete,
				task);
    if (!err)
	pthread_cancel((pthread_t)task->opaque2);

    return err;
}

int rt_task_yield (void)

{
    return XENOMAI_SKINCALL0(__rtai_muxid,
			     __rtai_task_yield);
}

int rt_task_set_periodic (RT_TASK *task,
			  RTIME idate,
			  RTIME period)

{
    int err = XENOMAI_SKINCALL3(__rtai_muxid,
                                __rtai_task_set_periodic,
                                task,
                                &idate,
                                &period);
    if(err == -ETIMEDOUT)
        fprintf(stderr,
                "WARNING: starting with RTAI/fusion 0.9, the start time passed\n"
                "to rt_task_set_periodic() should use rt_timer_read() as a time"
                "base.\n");

    return err;
}

int rt_task_wait_period (void)

{
    return XENOMAI_SKINCALL0(__rtai_muxid,
			     __rtai_task_wait_period);
}

int rt_task_set_priority (RT_TASK *task,
			  int prio)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_set_priority,
			     task,
			     prio);
}

int rt_task_sleep (RTIME delay)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_task_sleep,
			     &delay);

}

int rt_task_sleep_until (RTIME date)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_task_sleep_until,
			     &date);

}

int rt_task_unblock (RT_TASK *task)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_task_unblock,
			     task);
}

int rt_task_inquire (RT_TASK *task,
		     RT_TASK_INFO *info)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_inquire,
			     task,
			     info);
}

int rt_task_notify (RT_TASK *task,
		    rt_sigset_t signals)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_notify,
			     task,
			     signals);
}

int rt_task_set_mode (int clrmask,
		      int setmask,
		      int *oldmode)
{
    return XENOMAI_SKINCALL3(__rtai_muxid,
			     __rtai_task_set_mode,
			     clrmask,
			     setmask,
			     oldmode);
}

RT_TASK *rt_task_self (void)

{
    RT_TASK *self;

    self = (RT_TASK *)pthread_getspecific(__rtai_tskey);

    if (self)
	return self;

    self = (RT_TASK *)malloc(sizeof(*self));

    if (!self || XENOMAI_SKINCALL1(__rtai_muxid,__rtai_task_self,self) != 0)
	return NULL;

    pthread_setspecific(__rtai_tskey,self);

    return self;
}

int rt_task_slice (RT_TASK *task,
		   RTIME quantum)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_slice,
			     task,
			     &quantum);
}

#ifdef CONFIG_RTAI_OPT_NATIVE_MPS

ssize_t rt_task_send (RT_TASK *task,
		      RT_TASK_MCB *mcb_s,
		      RT_TASK_MCB *mcb_r,
		      RTIME timeout)
{
    return (ssize_t)XENOMAI_SKINCALL4(__rtai_muxid,
				      __rtai_task_send,
				      task,
				      mcb_s,
				      mcb_r,
				      &timeout);
}

int rt_task_receive (RT_TASK_MCB *mcb_r,
		     RTIME timeout)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_receive,
			     mcb_r,
			     &timeout);
}

int rt_task_reply (int flowid,
		   RT_TASK_MCB *mcb_s)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_reply,
			     flowid,
			     mcb_s);
}

#endif /* CONFIG_RTAI_OPT_NATIVE_MPS */
