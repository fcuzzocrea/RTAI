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
#include <sys/mman.h>
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <limits.h>
#include <rtai/syscall.h>
#include <rtai/task.h>

extern pthread_key_t __rtai_tskey;

extern int __rtai_muxid;

int __init_skin(void);

/* Public RTAI interface. */

struct rt_task_iargs {
    RT_TASK *task;
    const char *name;
    int prio;
    pid_t ppid;
    int *syncp;
};

static void *rt_task_trampoline (void *cookie)

{
    struct rt_task_iargs *iargs = (struct rt_task_iargs *)cookie;
    char stack[PTHREAD_STACK_MIN];
    void (*entry)(void *cookie);
    struct sched_param param;
    struct rt_arg_bulk bulk;
    int err;

    /* Ok, this looks like weird, but we need this. */
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);

    mlockall(MCL_CURRENT|MCL_FUTURE);
    memset(stack,0,sizeof(stack));

    bulk.a1 = (u_long)iargs->task;
    bulk.a2 = (u_long)iargs->name;
    bulk.a3 = (u_long)iargs->prio;

    err = XENOMAI_SKINCALL3(__rtai_muxid,
			    __rtai_task_create,
			    &bulk,
			    iargs->ppid,
			    iargs->syncp);
    if (err)
	goto fail;

    /* Wait on the barrier for the task to be started. The barrier
       could be released in order to process Linux signals while the
       RTAI shadow is still dormant; in such a case, resume wait. */

    do
	err = XENOMAI_SYSCALL2(__xn_sys_barrier,&entry,&cookie);
    while (err == -EINTR);

    if (err)
	goto fail;

    entry(cookie);

 fail:

    return NULL;
}

int rt_task_create (RT_TASK *task,
		    const char *name,
		    int stksize,
		    int prio,
		    int mode)	/* Ignored. */
{
    struct rt_task_iargs iargs;
    struct sched_param param;
    pthread_attr_t thattr;
    int syncflag = 0;
    pthread_t thid;

    /* Try to attach to the in-kernel skin on-the-fly at the first
       task creation. */

    if (__rtai_muxid < 0 && __init_skin() < 0)
	return -ENOSYS;

    /* Migrate this thread to the Linux domain since we are about to
       issue a series of regular kernel syscalls in order to create
       the new Linux thread, which in turn will be mapped to a
       real-time shadow. */

    XENOMAI_SYSCALL1(__xn_sys_migrate,FUSION_LINUX_DOMAIN);

    iargs.task = task;
    iargs.name = name;
    iargs.prio = prio;
    iargs.ppid = getpid();
    iargs.syncp = &syncflag;

    pthread_attr_init(&thattr);

    if (stksize > 0)
	{
	if (stksize < PTHREAD_STACK_MIN * 2)
	    stksize = PTHREAD_STACK_MIN * 2;

	pthread_attr_setstacksize(&thattr,stksize);
	}

    pthread_attr_setdetachstate(&thattr,PTHREAD_CREATE_DETACHED);
    pthread_attr_setschedpolicy(&thattr,SCHED_FIFO);
    param.sched_priority = sched_get_priority_max(SCHED_FIFO);
    pthread_attr_setschedparam(&thattr,&param);
    pthread_create(&thid,&thattr,&rt_task_trampoline,&iargs);

    /* Wait for sync with rt_task_trampoline() */
    return XENOMAI_SYSCALL1(__xn_sys_sync,&syncflag);
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

int rt_task_bind (RT_TASK *task,
		  const char *name)
{
    if (__rtai_muxid < 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_task_bind,
			     task,
			     name);
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
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_task_delete,
			     task);
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
    return XENOMAI_SKINCALL3(__rtai_muxid,
			     __rtai_task_set_periodic,
			     task,
			     &idate,
			     &period);
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

    if (__rtai_muxid < 0 && __init_skin() < 0)
	return NULL; /* Otherwise, __rtai_tskey must be valid. */

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
