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

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/shadow.h>
#include <rtai/syscall.h>
#include <rtai/registry.h>
#include <rtai/task.h>
#include <rtai/timer.h>
#include <rtai/sem.h>
#include <rtai/event.h>
#include <rtai/mutex.h>
#include <rtai/cond.h>

/* This file implements the RTAI syscall wrappers;
 *
 * o Unchecked uaccesses are used to fetch args since the syslib is
 * trusted. We currently assume that the caller's memory is locked and
 * committed.
 *
 * o All skin services (re-)check the object descriptor they are
 * passed; so there is no race between a call to rt_registry_fetch()
 * where the user-space handle is converted to a descriptor pointer,
 * and the use of it in the actual syscall.
 */

static int __muxid;

static int __rt_bind_helper (struct task_struct *curr,
			     struct pt_regs *regs,
			     unsigned magic)
{
    char name[XNOBJECT_NAME_LEN];
    RT_TASK_PLACEHOLDER ph;
    void *objaddr;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)) ||
	!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	return -EFAULT;

    __xn_copy_from_user(curr,name,(const char *)__xn_reg_arg2(regs),sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    err = rt_registry_bind(name,RT_TIME_INFINITE,&ph.opaque);

    if (!err)
	{
	objaddr = rt_registry_fetch(ph.opaque);
	
	/* Also validate the type of the bound object. */

	if (rtai_test_magic(objaddr,magic))
	    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	else
	    err = -EACCES;
	}

    return err;
}

/*
 * int __rt_task_create(struct rt_arg_bulk *bulk,
 *                      pid_t syncpid,
 *                      int *u_syncp)
 *
 * bulk = {
 * a1: RT_TASK_PLACEHOLDER *task;
 * a2: const char *name;
 * a3: int prio;
 * }
 */

static int __rt_task_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    struct rt_arg_bulk bulk;
    int *u_syncp, err, prio;
    RT_TASK_PLACEHOLDER ph;
    pid_t syncpid;
    RT_TASK *task;
    spl_t s;

    __xn_copy_from_user(curr,&bulk,(void *)__xn_reg_arg1(regs),sizeof(bulk));

    if (!__xn_access_ok(curr,VERIFY_WRITE,bulk.a1,sizeof(ph)))
	return -EFAULT;

    if (bulk.a2)
	{
	if (!__xn_access_ok(curr,VERIFY_READ,bulk.a2,sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char *)bulk.a2,sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	strncpy(curr->comm,name,sizeof(curr->comm));
	curr->comm[sizeof(curr->comm) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Task priority. */
    prio = bulk.a3;
    /* PID of parent thread waiting for sync. */
    syncpid = __xn_reg_arg2(regs);
    /* Semaphore address. */
    u_syncp = (int *)__xn_reg_arg3(regs);

    task = (RT_TASK *)xnmalloc(sizeof(*task));

    if (!task)
	return -ENOMEM;

    /* Force FPU support in user-space. This will lead to a no-op if
       the platform does not support it. */

    err = rt_task_create(task,name,0,prio,XNFPU|XNSHADOW);

    if (err == 0)
	{
	/* We don't want some funny guy to rip the new TCB off while
	   two user-space threads are being synchronized on it, so
	   enter a critical section. Do *not* take the big lock here:
	   this is useless since deleting a thread through an
	   inter-CPU request requires the target CPU to accept IPIs,
	   and this is bugous since xnshadow_map() would block
	   "current" with the superlock held. */

	splhigh(s);

	/* Copy back the registry handle to the ph struct
	   _before_ current is suspended in xnshadow_map(). */

	ph.opaque = task->handle;
	__xn_copy_to_user(curr,(void *)bulk.a1,&ph,sizeof(ph));

	/* The following service blocks until rt_task_start() is
	   issued. */

	xnshadow_map(&task->thread_base,
		     syncpid,
		     u_syncp);

	splexit(s);

	/* Pass back the entry and the cookie pointers obtained from
	   rt_task_start(). */

	bulk.a1 = (u_long)task->thread_base.entry;
	bulk.a2 = (u_long)task->thread_base.cookie;
	__xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&bulk,sizeof(bulk));
	}
    else
	{
	xnfree(task);
	xnshadow_sync_post(syncpid,u_syncp,err);
	}

    return err;
}

/*
 * int __rt_task_bind(RT_TASK_PLACEHOLDER *ph,
 *                    const char *name)
 */

static int __rt_task_bind (struct task_struct *curr, struct pt_regs *regs) {

    return __rt_bind_helper(curr,regs,RTAI_TASK_MAGIC);
}

/*
 * int __rt_task_start(RT_TASK_PLACEHOLDER *ph,
 *                     void (*entry)(void *cookie),
 *                     void *cookie)
 */

static int __rt_task_start (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)rt_registry_fetch(ph.opaque);

    if (!task)
	return -ENOENT;

    return rt_task_start(task,
			 (void (*)(void *))__xn_reg_arg2(regs),
			 (void *)__xn_reg_arg3(regs));
}

/*
 * int __rt_task_suspend(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_suspend (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task = NULL;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else if (xnpod_regular_p())
	goto call_on_self;

    if (!task)
	return -ENOENT;

call_on_self:

    return rt_task_suspend(task);
}

/*
 * int __rt_task_resume(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_resume (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)rt_registry_fetch(ph.opaque);

    if (!task)
	return -ENOENT;

    return rt_task_resume(task);
}

/*
 * int __rt_task_delete(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task = NULL;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else if (xnpod_regular_p())
	goto call_on_self;

    if (!task)
	return -ENOENT;

call_on_self:

    return rt_task_delete(task);
}

/*
 * int __rt_task_yield(void)
 */

static int __rt_task_yield (struct task_struct *curr, struct pt_regs *regs) {

    return rt_task_yield();
}

/*
 * int __rt_task_set_periodic(RT_TASK_PLACEHOLDER *ph,
 *			         RTIME idate,
 *			         RTIME period)
 */

static int __rt_task_set_periodic (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task = NULL;
    RTIME idate, period;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else if (xnpod_regular_p())
	goto call_on_self;

    if (!task)
	return -ENOENT;

call_on_self:

    __xn_copy_from_user(curr,&idate,(void *)__xn_reg_arg2(regs),sizeof(idate));
    __xn_copy_from_user(curr,&period,(void *)__xn_reg_arg3(regs),sizeof(period));

    return rt_task_set_periodic(task,idate,period);
}

/*
 * int __rt_task_wait_period(void)
 */

static int __rt_task_wait_period (struct task_struct *curr, struct pt_regs *regs) {

    return rt_task_wait_period();
}

/*
 * int __rt_task_set_priority(RT_TASK_PLACEHOLDER *ph,
 *			         int prio)
 */

static int __rt_task_set_priority (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task = NULL;
    int prio;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else if (xnpod_regular_p())
	goto call_on_self;

    if (!task)
	return -ENOENT;

call_on_self:

    prio = __xn_reg_arg2(regs);

    return rt_task_set_priority(task,prio);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME delay;

    __xn_copy_from_user(curr,&delay,(void *)__xn_reg_arg1(regs),sizeof(delay));

    return rt_task_sleep(delay);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep_until (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME date;

    __xn_copy_from_user(curr,&date,(void *)__xn_reg_arg1(regs),sizeof(date));

    return rt_task_sleep_until(date);
}

/*
 * int __rt_task_unblock(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_unblock (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)rt_registry_fetch(ph.opaque);

    if (!task)
	return -ENOENT;

    return rt_task_unblock(task);
}

/*
 * int __rt_task_inquire(RT_TASK_PLACEHOLDER *ph,
 *                       RT_TASK_INFO *infop)
 */

static int __rt_task_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task = NULL;
    RT_TASK_INFO info;
    int err;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else if (xnpod_regular_p())
	goto call_on_self;

    if (!task)
	return -ENOENT;

call_on_self:

    err = rt_task_inquire(task,&info);

    if (!err)
	__xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

/*
 * int __rt_timer_start(RTIME *tickvalp)
 */

static int __rt_timer_start (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME tickval;

    __xn_copy_from_user(curr,&tickval,(void *)__xn_reg_arg1(regs),sizeof(tickval));

    if (testbits(nkpod->status,XNTIMED))
	{
	if ((tickval == FUSION_APERIODIC_TIMER && xnpod_get_tickval() == 1) ||
	    (tickval != FUSION_APERIODIC_TIMER && xnpod_get_tickval() == tickval))
	    return 0;

	xnpod_stop_timer();
	}

    if (xnpod_start_timer(tickval,XNPOD_DEFAULT_TICKHANDLER) != 0)
	return -ETIME;

    return 0;
}

/*
 * int __rt_timer_stop(void)
 */

static int __rt_timer_stop (struct task_struct *curr, struct pt_regs *regs)

{
    rt_timer_stop();
    return 0;
}

/*
 * int __rt_timer_read(RTIME *timep)
 */

static int __rt_timer_read (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME now = rt_timer_read();
    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&now,sizeof(now));
    return 0;
}

/*
 * int __rt_timer_tsc(RTIME *tscp)
 */

static int __rt_timer_tsc (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME tsc = rt_timer_tsc();
    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&tsc,sizeof(tsc));
    return 0;
}

/*
 * int __rt_timer_ns2ticks(RTIME *ticksp, RTIME *nsp)
 */

static int __rt_timer_ns2ticks (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME ns, ticks;

    __xn_copy_from_user(curr,&ns,(void *)__xn_reg_arg2(regs),sizeof(ns));
    ticks = rt_timer_ns2ticks(ns);
    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&ticks,sizeof(ticks));

    return 0;
}

/*
 * int __rt_timer_ticks2ns(RTIME *nsp, RTIME *ticksp)
 */

static int __rt_timer_ticks2ns (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME ticks, ns;

    __xn_copy_from_user(curr,&ticks,(void *)__xn_reg_arg2(regs),sizeof(ticks));
    ns = rt_timer_ticks2ns(ticks);
    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&ns,sizeof(ns));

    return 0;
}

/*
 * int __rt_timer_inquire(RT_TIMER_INFO *info)
 */

static int __rt_timer_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TIMER_INFO info;
    int err;

    err = rt_timer_inquire(&info);

    if (!err)
	__xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&info,sizeof(info));

    return err;
}

#if CONFIG_RTAI_OPT_NATIVE_SEM

/*
 * int __rt_sem_create(RT_SEM_PLACEHOLDER *ph,
 *                     const char *name,
 *                     unsigned icount,
 *                     int mode)
 */

static int __rt_sem_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_SEM_PLACEHOLDER ph;
    unsigned icount;
    int err, mode;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Initial semaphore value. */
    icount = (unsigned)__xn_reg_arg3(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg4(regs);

    sem = (RT_SEM *)xnmalloc(sizeof(*sem));

    if (!sem)
	return -ENOMEM;

    err = rt_sem_create(sem,name,icount,mode);

    if (err == 0)
	{
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = sem->handle;
	__xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(sem);

    return err;
}

/*
 * int __rt_sem_bind(RT_SEM_PLACEHOLDER *ph,
 *                   const char *name)
 */

static int __rt_sem_bind (struct task_struct *curr, struct pt_regs *regs) {

    return __rt_bind_helper(curr,regs,RTAI_SEM_MAGIC);
}

/*
 * int __rt_sem_delete(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ENOENT;

    return rt_sem_delete(sem);
}

/*
 * int __rt_sem_p(RT_SEM_PLACEHOLDER *ph,
 *                RTIME *timeoutp)
 */

static int __rt_sem_p (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RTIME timeout;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ENOENT;

    __xn_copy_from_user(curr,&timeout,(void *)__xn_reg_arg2(regs),sizeof(timeout));

    return rt_sem_p(sem,timeout);
}

/*
 * int __rt_sem_v(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_v (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM *sem;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ENOENT;

    return rt_sem_v(sem);
}

/*
 * int __rt_sem_inquire(RT_SEM_PLACEHOLDER *ph,
 *                      RT_SEM_INFO *infop)
 */

static int __rt_sem_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM_INFO info;
    RT_SEM *sem;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ENOENT;

    err = rt_sem_inquire(sem,&info);

    if (!err)
	__xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_SEM */

#define __rt_sem_create  __rt_call_not_available
#define __rt_sem_bind    __rt_call_not_available
#define __rt_sem_delete  __rt_call_not_available
#define __rt_sem_p       __rt_call_not_available
#define __rt_sem_v       __rt_call_not_available
#define __rt_sem_inquire __rt_call_not_available

#endif /* CONFIG_RTAI_OPT_NATIVE_SEM */

#if CONFIG_RTAI_OPT_NATIVE_EVENT

/*
 * int __rt_event_create(RT_EVENT_PLACEHOLDER *ph,
 *                       const char *name,
 *                       unsigned ivalue,
 *                       int mode)
 */

static int __rt_event_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_EVENT_PLACEHOLDER ph;
    unsigned ivalue;
    RT_EVENT *event;
    int err, mode;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Initial event mask value. */
    ivalue = (unsigned)__xn_reg_arg3(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg4(regs);

    event = (RT_EVENT *)xnmalloc(sizeof(*event));

    if (!event)
	return -ENOMEM;

    err = rt_event_create(event,name,ivalue,mode);

    if (err == 0)
	{
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = event->handle;
	__xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(event);

    return err;
}

/*
 * int __rt_event_bind(RT_EVENT_PLACEHOLDER *ph,
 *                     const char *name)
 */

static int __rt_event_bind (struct task_struct *curr, struct pt_regs *regs) {

    return __rt_bind_helper(curr,regs,RTAI_EVENT_MAGIC);
}

/*
 * int __rt_event_delete(RT_EVENT_PLACEHOLDER *ph)
 */

static int __rt_event_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ENOENT;

    return rt_event_delete(event);
}

/*
 * int __rt_event_pend(RT_EVENT_PLACEHOLDER *ph,
                       unsigned long mask,
                       unsigned long *mask_r,
                       int mode,
 *                     RTIME *timeoutp)
 */

static int __rt_event_pend (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long mask, mask_r;
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;
    RTIME timeout;
    int mode, err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ENOENT;

    mask = (unsigned long)__xn_reg_arg2(regs);
    mode = (int)__xn_reg_arg4(regs);
    __xn_copy_from_user(curr,&timeout,(void *)__xn_reg_arg5(regs),sizeof(timeout));

    err = rt_event_pend(event,mask,&mask_r,mode,timeout);

    __xn_copy_to_user(curr,(void *)__xn_reg_arg3(regs),&mask_r,sizeof(mask_r));

    return err;
}

/*
 * int __rt_event_post(RT_EVENT_PLACEHOLDER *ph,
 *                     unsigned long mask)
 */

static int __rt_event_post (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    unsigned long mask;
    RT_EVENT *event;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ENOENT;

    mask = (unsigned long)__xn_reg_arg2(regs);

    return rt_event_post(event,mask);
}

/*
 * int __rt_event_inquire(RT_EVENT_PLACEHOLDER *ph,
 *                        RT_EVENT_INFO *infop)
 */

static int __rt_event_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT_INFO info;
    RT_EVENT *event;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ENOENT;

    err = rt_event_inquire(event,&info);

    if (!err)
	__xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_EVENT */

#define __rt_event_create  __rt_call_not_available
#define __rt_event_bind    __rt_call_not_available
#define __rt_event_delete  __rt_call_not_available
#define __rt_event_pend    __rt_call_not_available
#define __rt_event_post    __rt_call_not_available
#define __rt_event_inquire __rt_call_not_available

#endif /* CONFIG_RTAI_OPT_NATIVE_EVENT */

#if CONFIG_RTAI_OPT_NATIVE_MUTEX

/*
 * int __rt_mutex_create(RT_MUTEX_PLACEHOLDER *ph,
 *                       const char *name)
 */

static int __rt_mutex_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    mutex = (RT_MUTEX *)xnmalloc(sizeof(*mutex));

    if (!mutex)
	return -ENOMEM;

    err = rt_mutex_create(mutex,name);

    if (err == 0)
	{
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = mutex->handle;
	__xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(mutex);

    return err;
}

/*
 * int __rt_mutex_bind(RT_MUTEX_PLACEHOLDER *ph,
 *                     const char *name)
 */

static int __rt_mutex_bind (struct task_struct *curr, struct pt_regs *regs) {

    return __rt_bind_helper(curr,regs,RTAI_MUTEX_MAGIC);
}

/*
 * int __rt_mutex_delete(RT_MUTEX_PLACEHOLDER *ph)
 */

static int __rt_mutex_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ENOENT;

    return rt_mutex_delete(mutex);
}

/*
 * int __rt_mutex_lock(RT_MUTEX_PLACEHOLDER *ph)
 *
 */

static int __rt_mutex_lock (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ENOENT;

    return rt_mutex_lock(mutex);
}

/*
 * int __rt_mutex_unlock(RT_MUTEX_PLACEHOLDER *ph)
 */

static int __rt_mutex_unlock (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ENOENT;

    return rt_mutex_unlock(mutex);
}

/*
 * int __rt_mutex_inquire(RT_MUTEX_PLACEHOLDER *ph,
 *                        RT_MUTEX_INFO *infop)
 */

static int __rt_mutex_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX_INFO info;
    RT_MUTEX *mutex;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ENOENT;

    err = rt_mutex_inquire(mutex,&info);

    if (!err)
	__xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_MUTEX */

#define __rt_mutex_create  __rt_call_not_available
#define __rt_mutex_bind    __rt_call_not_available
#define __rt_mutex_delete  __rt_call_not_available
#define __rt_mutex_lock    __rt_call_not_available
#define __rt_mutex_unlock  __rt_call_not_available
#define __rt_mutex_inquire __rt_call_not_available

#endif /* CONFIG_RTAI_OPT_NATIVE_MUTEX */

#if CONFIG_RTAI_OPT_NATIVE_COND

/*
 * int __rt_cond_create(RT_COND_PLACEHOLDER *ph,
 *                      const char *name)
 */

static int __rt_cond_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    cond = (RT_COND *)xnmalloc(sizeof(*cond));

    if (!cond)
	return -ENOMEM;

    err = rt_cond_create(cond,name);

    if (err == 0)
	{
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = cond->handle;
	__xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(cond);

    return err;
}

/*
 * int __rt_cond_bind(RT_COND_PLACEHOLDER *ph,
 *                   const char *name)
 */

static int __rt_cond_bind (struct task_struct *curr, struct pt_regs *regs) {

    return __rt_bind_helper(curr,regs,RTAI_COND_MAGIC);
}

/*
 * int __rt_cond_delete(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ENOENT;

    return rt_cond_delete(cond);
}

/*
 * int __rt_cond_wait(RT_COND_PLACEHOLDER *cph,
 *                    RT_MUTEX_PLACEHOLDER *mph,
 *                    RTIME *timeoutp)
 */

static int __rt_cond_wait (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER cph, mph;
    RT_MUTEX *mutex;
    RT_COND *cond;
    RTIME timeout;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(cph)) ||
	!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(mph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&cph,(void *)__xn_reg_arg1(regs),sizeof(cph));
    __xn_copy_from_user(curr,&mph,(void *)__xn_reg_arg2(regs),sizeof(mph));

    cond = (RT_COND *)rt_registry_fetch(cph.opaque);

    if (!cond)
	return -ENOENT;

    mutex = (RT_MUTEX *)rt_registry_fetch(mph.opaque);

    if (!mutex)
	return -ENOENT;

    __xn_copy_from_user(curr,&timeout,(void *)__xn_reg_arg3(regs),sizeof(timeout));

    return rt_cond_wait(cond,mutex,timeout);
}

/*
 * int __rt_cond_signal(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_signal (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ENOENT;

    return rt_cond_signal(cond);
}

/*
 * int __rt_cond_broadcast(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_broadcast (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ENOENT;

    return rt_cond_broadcast(cond);
}

/*
 * int __rt_cond_inquire(RT_COND_PLACEHOLDER *ph,
 *                       RT_COND_INFO *infop)
 */

static int __rt_cond_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND_INFO info;
    RT_COND *cond;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ENOENT;

    err = rt_cond_inquire(cond,&info);

    if (!err)
	__xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_COND */

#define __rt_cond_create    __rt_call_not_available
#define __rt_cond_bind      __rt_call_not_available
#define __rt_cond_delete    __rt_call_not_available
#define __rt_cond_wait      __rt_call_not_available
#define __rt_cond_signal    __rt_call_not_available
#define __rt_cond_broadcast __rt_call_not_available
#define __rt_cond_inquire   __rt_call_not_available

#endif /* CONFIG_RTAI_OPT_NATIVE_COND */

static  __attribute__((unused))
int __rt_call_not_available (struct task_struct *curr, struct pt_regs *regs) {
    return -ENOSYS;
}

static xnsysent_t __systab[] = {
    { &__rt_task_create, __xn_flag_init },
    { &__rt_task_bind, __xn_flag_init },
    { &__rt_task_start, __xn_flag_anycall },
    { &__rt_task_suspend, __xn_flag_anycall },
    { &__rt_task_resume, __xn_flag_anycall },
    { &__rt_task_delete, __xn_flag_anycall },
    { &__rt_task_yield, __xn_flag_regular },
    { &__rt_task_set_periodic, __xn_flag_regular },
    { &__rt_task_wait_period, __xn_flag_regular },
    { &__rt_task_set_priority, __xn_flag_anycall },
    { &__rt_task_sleep, __xn_flag_regular },
    { &__rt_task_sleep_until, __xn_flag_regular },
    { &__rt_task_unblock, __xn_flag_anycall },
    { &__rt_task_inquire, __xn_flag_anycall },
    { &__rt_timer_start, __xn_flag_anycall },
    { &__rt_timer_stop, __xn_flag_anycall },
    { &__rt_timer_read, __xn_flag_anycall },
    { &__rt_timer_tsc, __xn_flag_anycall },
    { &__rt_timer_ns2ticks, __xn_flag_anycall },
    { &__rt_timer_ticks2ns, __xn_flag_anycall },
    { &__rt_timer_inquire, __xn_flag_anycall },
    { &__rt_sem_create, __xn_flag_anycall },
    { &__rt_sem_bind, __xn_flag_regular },
    { &__rt_sem_delete, __xn_flag_anycall },
    { &__rt_sem_p, __xn_flag_regular },
    { &__rt_sem_v, __xn_flag_anycall },
    { &__rt_sem_inquire, __xn_flag_anycall },
    { &__rt_event_create, __xn_flag_anycall },
    { &__rt_event_bind, __xn_flag_regular },
    { &__rt_event_delete, __xn_flag_anycall },
    { &__rt_event_pend, __xn_flag_regular },
    { &__rt_event_post, __xn_flag_anycall },
    { &__rt_event_inquire, __xn_flag_anycall },
    { &__rt_mutex_create, __xn_flag_anycall },
    { &__rt_mutex_bind, __xn_flag_regular },
    { &__rt_mutex_delete, __xn_flag_anycall },
    { &__rt_mutex_lock, __xn_flag_regular },
    { &__rt_mutex_unlock, __xn_flag_shadow },
    { &__rt_mutex_inquire, __xn_flag_anycall },
    { &__rt_cond_create, __xn_flag_anycall },
    { &__rt_cond_bind, __xn_flag_regular },
    { &__rt_cond_delete, __xn_flag_anycall },
    { &__rt_cond_wait, __xn_flag_regular },
    { &__rt_cond_signal, __xn_flag_anycall },
    { &__rt_cond_broadcast, __xn_flag_anycall },
    { &__rt_cond_inquire, __xn_flag_anycall },
};

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == RTAI_SKIN_MAGIC &&
	testbits(thread->status,XNSHADOW))
	xnshadow_unmap(thread);
}

int __syscall_pkg_init (void)

{
    __muxid = xnshadow_register_skin("native",
				     RTAI_SKIN_MAGIC,
				     sizeof(__systab) / sizeof(__systab[0]),
				     __systab,
				     NULL);
    if (__muxid < 0)
	return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    
    return 0;
}

void __syscall_pkg_cleanup (void)

{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_skin(__muxid);
}
