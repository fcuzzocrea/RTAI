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
#include <rtai/queue.h>
#include <rtai/heap.h>
#include <rtai/alarm.h>

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
			     unsigned magic,
			     void **objaddrp)
{
    char name[XNOBJECT_NAME_LEN];
    RT_TASK_PLACEHOLDER ph;
    void *objaddr;
    spl_t s;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)) ||
	!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	return -EFAULT;

    __xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    err = rt_registry_bind(name,TM_INFINITE,&ph.opaque);

    if (!err)
	{
	xnlock_get_irqsave(&nklock,s);

	objaddr = rt_registry_fetch(ph.opaque);
	
	/* Also validate the type of the bound object. */

	if (rtai_test_magic(objaddr,magic))
	    {
	    if (objaddrp)
		*objaddrp = objaddr;
	    else
		__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	    }
	else
	    err = -EACCES;

	xnlock_put_irqrestore(&nklock,s);
	}

    return err;
}

static RT_TASK *__rt_task_current (struct task_struct *curr)

{
    xnthread_t *thread = xnshadow_thread(curr);

    /* Don't call rt_task_self() which does not know about relaxed
       tasks, but rather use the shadow information directly. */

    if (!thread || xnthread_get_magic(thread) != RTAI_SKIN_MAGIC)
	return NULL;

    return thread2rtask(thread); /* Convert TCB pointers. */
}

/*
 * int __rt_task_create(struct rt_arg_bulk *bulk,
 *                      pid_t syncpid,
 *                      int __user *u_syncp)
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
    RT_TASK_PLACEHOLDER ph;
    int __user *u_syncp;
    int err, prio;
    pid_t syncpid;
    RT_TASK *task;
    spl_t s;

    __xn_copy_from_user(curr,&bulk,(void __user *)__xn_reg_arg1(regs),sizeof(bulk));

    if (!__xn_access_ok(curr,VERIFY_WRITE,bulk.a1,sizeof(ph)))
	return -EFAULT;

    if (bulk.a2)
	{
	if (!__xn_access_ok(curr,VERIFY_READ,bulk.a2,sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char __user *)bulk.a2,sizeof(name) - 1);
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
    u_syncp = (int __user *)__xn_reg_arg3(regs);

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
	   inter-CPU request requires the target CPU to accept
	   IPIs. */

	splhigh(s);

	/* Copy back the registry handle to the ph struct. */
	ph.opaque = task->handle;
	__xn_copy_to_user(curr,(void __user *)bulk.a1,&ph,sizeof(ph));
	xnshadow_map(&task->thread_base,syncpid,u_syncp);

	splexit(s);
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

    return __rt_bind_helper(curr,regs,RTAI_TASK_MAGIC,NULL);
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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)rt_registry_fetch(ph.opaque);

    if (!task)
	return -ESRCH;

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
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)rt_registry_fetch(ph.opaque);

    if (!task)
	return -ESRCH;

    return rt_task_resume(task);
}

/*
 * int __rt_task_delete(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    return rt_task_delete(task); /* TCB freed in delete hook. */
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
    RTIME idate, period;
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    __xn_copy_from_user(curr,&idate,(void __user *)__xn_reg_arg2(regs),sizeof(idate));
    __xn_copy_from_user(curr,&period,(void __user *)__xn_reg_arg3(regs),sizeof(period));

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
 *                            int prio)
 */

static int __rt_task_set_priority (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;
    int prio;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    prio = __xn_reg_arg2(regs);

    return rt_task_set_priority(task,prio);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME delay;

    __xn_copy_from_user(curr,&delay,(void __user *)__xn_reg_arg1(regs),sizeof(delay));

    return rt_task_sleep(delay);
}

/*
 * int __rt_task_sleep(RTIME delay)
 */

static int __rt_task_sleep_until (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME date;

    __xn_copy_from_user(curr,&date,(void __user *)__xn_reg_arg1(regs),sizeof(date));

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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    task = (RT_TASK *)rt_registry_fetch(ph.opaque);

    if (!task)
	return -ESRCH;

    return rt_task_unblock(task);
}

/*
 * int __rt_task_inquire(RT_TASK_PLACEHOLDER *ph,
 *                       RT_TASK_INFO *infop)
 */

static int __rt_task_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK_INFO info;
    RT_TASK *task;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    err = rt_task_inquire(task,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

/*
 * int __rt_task_notify(RT_TASK_PLACEHOLDER *ph,
 *                      rt_sigset_t signals)
 */

static int __rt_task_notify (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    rt_sigset_t signals;
    RT_TASK *task;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    signals = (rt_sigset_t)__xn_reg_arg2(regs);

    return rt_task_notify(task,signals);
}

/*
 * int __rt_task_set_mode(int clrmask,
 *                        int setmask,
 *                        int *mode_r)
 */

static int __rt_task_set_mode (struct task_struct *curr, struct pt_regs *regs)

{
    int err, setmask, clrmask, mode_r;

    if (!__rt_task_current(curr))
	return -ESRCH;

    if (__xn_reg_arg3(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(int)))
	return -EFAULT;

    clrmask = __xn_reg_arg1(regs);
    setmask = __xn_reg_arg2(regs);

    err = rt_task_set_mode(setmask,clrmask,&mode_r);

    if (!err && __xn_reg_arg3(regs))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&mode_r,sizeof(mode_r));

    return err;
}

/*
 * int __rt_task_self(RT_TASK_PLACEHOLDER *ph)
 */

static int __rt_task_self (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    task = __rt_task_current(curr);

    if (!task)
	/* Calls on behalf of a non-task context beget an error for
	   the user-space interface. */
	return -ESRCH;

    ph.opaque = task->handle;	/* Copy back the task handle. */

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return 0;
}

/*
 * int __rt_task_slice(RT_TASK_PLACEHOLDER *ph,
 *                     RTIME quantum)
 */

static int __rt_task_slice (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK_PLACEHOLDER ph;
    RT_TASK *task;
    RTIME quantum;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

	task = (RT_TASK *)rt_registry_fetch(ph.opaque);
	}
    else
	task = __rt_task_current(curr);

    if (!task)
	return -ESRCH;

    __xn_copy_from_user(curr,&quantum,(void __user *)__xn_reg_arg2(regs),sizeof(quantum));

    return rt_task_slice(task,quantum);
}

/*
 * int __rt_timer_start(RTIME *tickvalp)
 */

static int __rt_timer_start (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME tickval;

    __xn_copy_from_user(curr,&tickval,(void __user *)__xn_reg_arg1(regs),sizeof(tickval));

    if (testbits(nkpod->status,XNTIMED))
	{
	if ((tickval == FUSION_APERIODIC_TIMER && xnpod_get_tickval() == 1) ||
	    (tickval != FUSION_APERIODIC_TIMER && xnpod_get_tickval() == tickval))
	    return 0;

	xnpod_stop_timer();
	}

    return xnpod_start_timer(tickval,XNPOD_DEFAULT_TICKHANDLER);
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
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&now,sizeof(now));
    return 0;
}

/*
 * int __rt_timer_tsc(RTIME *tscp)
 */

static int __rt_timer_tsc (struct task_struct *curr, struct pt_regs *regs)

{
    RTIME tsc = rt_timer_tsc();
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&tsc,sizeof(tsc));
    return 0;
}

/*
 * int __rt_timer_ns2ticks(SRTIME *ticksp, SRTIME *nsp)
 */

static int __rt_timer_ns2ticks (struct task_struct *curr, struct pt_regs *regs)

{
    SRTIME ns, ticks;

    __xn_copy_from_user(curr,&ns,(void __user *)__xn_reg_arg2(regs),sizeof(ns));
    ticks = rt_timer_ns2ticks(ns);
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ticks,sizeof(ticks));

    return 0;
}

/*
 * int __rt_timer_ticks2ns(SRTIME *nsp, SRTIME *ticksp)
 */

static int __rt_timer_ticks2ns (struct task_struct *curr, struct pt_regs *regs)

{
    SRTIME ticks, ns;

    __xn_copy_from_user(curr,&ticks,(void __user *)__xn_reg_arg2(regs),sizeof(ticks));
    ns = rt_timer_ticks2ns(ticks);
    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ns,sizeof(ns));

    return 0;
}

/*
 * int __rt_timer_inquire(RT_TIMER_INFO *info)
 */

static int __rt_timer_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TIMER_INFO info;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(info)))
	return -EFAULT;

    err = rt_timer_inquire(&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&info,sizeof(info));

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

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
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
	sem->source = RT_UAPI_SOURCE;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = sem->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
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

    return __rt_bind_helper(curr,regs,RTAI_SEM_MAGIC,NULL);
}

/*
 * int __rt_sem_delete(RT_SEM_PLACEHOLDER *ph)
 */

static int __rt_sem_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_SEM_PLACEHOLDER ph;
    RT_SEM *sem;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

    err = rt_sem_delete(sem);

    if (!err && sem->source == RT_UAPI_SOURCE)
	xnfree(sem);

    return err;
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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg2(regs),sizeof(timeout));

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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

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

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    sem = (RT_SEM *)rt_registry_fetch(ph.opaque);

    if (!sem)
	return -ESRCH;

    err = rt_sem_inquire(sem,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

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

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
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
	event->source = RT_UAPI_SOURCE;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = event->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
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

    return __rt_bind_helper(curr,regs,RTAI_EVENT_MAGIC,NULL);
}

/*
 * int __rt_event_delete(RT_EVENT_PLACEHOLDER *ph)
 */

static int __rt_event_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    err = rt_event_delete(event);

    if (!err && event->source == RT_UAPI_SOURCE)
	xnfree(event);

    return err;
}

/*
 * int __rt_event_wait(RT_EVENT_PLACEHOLDER *ph,
                       unsigned long mask,
                       unsigned long *mask_r,
                       int mode,
 *                     RTIME *timeoutp)
 */

static int __rt_event_wait (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long mask, mask_r;
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;
    RTIME timeout;
    int mode, err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    mask = (unsigned long)__xn_reg_arg2(regs);
    mode = (int)__xn_reg_arg4(regs);
    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg5(regs),sizeof(timeout));

    err = rt_event_wait(event,mask,&mask_r,mode,timeout);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&mask_r,sizeof(mask_r));

    return err;
}

/*
 * int __rt_event_signal(RT_EVENT_PLACEHOLDER *ph,
 *                       unsigned long mask)
 */

static int __rt_event_signal (struct task_struct *curr, struct pt_regs *regs)

{
    RT_EVENT_PLACEHOLDER ph;
    unsigned long mask;
    RT_EVENT *event;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    mask = (unsigned long)__xn_reg_arg2(regs);

    return rt_event_signal(event,mask);
}

/*
 * int __rt_event_clear(RT_EVENT_PLACEHOLDER *ph,
 *                      unsigned long mask,
 *                      unsigned long *mask_r)
 */

static int __rt_event_clear (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long mask, mask_r;
    RT_EVENT_PLACEHOLDER ph;
    RT_EVENT *event;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg3(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(mask_r)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    mask = (unsigned long)__xn_reg_arg2(regs);

    err = rt_event_clear(event,mask,&mask_r);

    if (!err && __xn_reg_arg3(regs))
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&mask_r,sizeof(mask_r));

    return err;
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

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    event = (RT_EVENT *)rt_registry_fetch(ph.opaque);

    if (!event)
	return -ESRCH;

    err = rt_event_inquire(event,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_EVENT */

#define __rt_event_create  __rt_call_not_available
#define __rt_event_bind    __rt_call_not_available
#define __rt_event_delete  __rt_call_not_available
#define __rt_event_wait    __rt_call_not_available
#define __rt_event_signal  __rt_call_not_available
#define __rt_event_clear   __rt_call_not_available
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

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
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
	mutex->source = RT_UAPI_SOURCE;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = mutex->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
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

    return __rt_bind_helper(curr,regs,RTAI_MUTEX_MAGIC,NULL);
}

/*
 * int __rt_mutex_delete(RT_MUTEX_PLACEHOLDER *ph)
 */

static int __rt_mutex_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_MUTEX_PLACEHOLDER ph;
    RT_MUTEX *mutex;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

    err = rt_mutex_delete(mutex);

    if (!err && mutex->source == RT_UAPI_SOURCE)
	xnfree(mutex);

    return err;
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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

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

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    mutex = (RT_MUTEX *)rt_registry_fetch(ph.opaque);

    if (!mutex)
	return -ESRCH;

    err = rt_mutex_inquire(mutex,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

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

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
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
	cond->source = RT_UAPI_SOURCE;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = cond->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
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

    return __rt_bind_helper(curr,regs,RTAI_COND_MAGIC,NULL);
}

/*
 * int __rt_cond_delete(RT_COND_PLACEHOLDER *ph)
 */

static int __rt_cond_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_COND_PLACEHOLDER ph;
    RT_COND *cond;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

    err = rt_cond_delete(cond);

    if (!err && cond->source == RT_UAPI_SOURCE)
	xnfree(cond);

    return err;
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

    __xn_copy_from_user(curr,&cph,(void __user *)__xn_reg_arg1(regs),sizeof(cph));
    __xn_copy_from_user(curr,&mph,(void __user *)__xn_reg_arg2(regs),sizeof(mph));

    cond = (RT_COND *)rt_registry_fetch(cph.opaque);

    if (!cond)
	return -ESRCH;

    mutex = (RT_MUTEX *)rt_registry_fetch(mph.opaque);

    if (!mutex)
	return -ESRCH;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg3(regs),sizeof(timeout));

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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

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

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

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

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    cond = (RT_COND *)rt_registry_fetch(ph.opaque);

    if (!cond)
	return -ESRCH;

    err = rt_cond_inquire(cond,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

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

#if CONFIG_RTAI_OPT_NATIVE_QUEUE

/*
 * int __rt_queue_create(RT_QUEUE_PLACEHOLDER *ph,
 *                       const char *name,
 *                       size_t poolsize,
 *                       size_t qlimit,
 *                       int mode)
 */

static int __rt_queue_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_QUEUE_PLACEHOLDER ph;
    size_t poolsize, qlimit;
    int err, mode;
    RT_QUEUE *q;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Size of memory pool. */
    poolsize = (size_t)__xn_reg_arg3(regs);
    /* Queue limit. */
    qlimit = (size_t)__xn_reg_arg4(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg5(regs);

    q = (RT_QUEUE *)xnmalloc(sizeof(*q));

    if (!q)
	return -ENOMEM;

    err = rt_queue_create(q,name,poolsize,qlimit,mode);

    if (err)
	goto free_and_fail;

    q->source = RT_UAPI_SOURCE;

    /* Copy back the registry handle to the ph struct. */
    ph.opaque = q->handle;
    ph.opaque2 = &q->bufpool;
    ph.mapsize = xnheap_size(&q->bufpool);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return 0;

 free_and_fail:
	
    xnfree(q);

    return err;
}

/*
 * int __rt_queue_bind(RT_QUEUE_PLACEHOLDER *ph,
 *                     const char *name)
 */

static int __rt_queue_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RT_QUEUE *q;
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* First, wait for the queue to appear in the registry. */

    err = __rt_bind_helper(curr,regs,RTAI_QUEUE_MAGIC,(void **)&q);

    if (err)
	goto unlock_and_exit;

    xnlock_put_irqrestore(&nklock,s);

    /* We need to migrate to secondary mode now for mapping the pool
       memory to user-space; we must have entered this syscall in
       primary mode. */

    xnshadow_relax();

    xnlock_get_irqsave(&nklock,s);

    /* Search for the queue again since we released the lock while
       migrating. */

    err = __rt_bind_helper(curr,regs,RTAI_QUEUE_MAGIC,(void **)&q);

    if (err)
	goto unlock_and_exit;

    ph.opaque = q->handle;
    ph.opaque2 = &q->bufpool;
    ph.mapsize = xnheap_size(&q->bufpool);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_delete(RT_QUEUE_PLACEHOLDER *ph)
 */

static int __rt_queue_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RT_QUEUE *q;
    int err = 0;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)rt_registry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    err = rt_queue_delete(q);

    if (!err && q->source == RT_UAPI_SOURCE)
	xnfree(q);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_alloc(RT_QUEUE_PLACEHOLDER *ph,
 *                     size_t size,
 *                     void **bufp)
 */

static int __rt_queue_alloc (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    size_t size;
    RT_QUEUE *q;
    int err = 0;
    void *buf;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(buf)))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)rt_registry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    size = (size_t)__xn_reg_arg2(regs);

    buf = rt_queue_alloc(q,size);

    /* Convert the kernel-based address of buf to the equivalent area
       into the caller's address space. */

    if (buf)
	buf = ph.mapbase + xnheap_shared_offset(&q->bufpool,buf);
    else
	err = -ENOMEM;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg3(regs),&buf,sizeof(buf));

    return err;
}

/*
 * int __rt_queue_free(RT_QUEUE_PLACEHOLDER *ph,
 *                     void *buf)
 */

static int __rt_queue_free (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    void __user *buf;
    RT_QUEUE *q;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    buf = (void __user *)__xn_reg_arg2(regs);

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)rt_registry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. */

    if (buf)
	{
	buf = xnheap_shared_address(&q->bufpool,(caddr_t)buf - ph.mapbase);
	err = rt_queue_free(q,buf);
	}
    else
	err = -EINVAL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_send(RT_QUEUE_PLACEHOLDER *ph,
 *                     void *buf,
 *                     size_t size,
 *                     int mode)
 */

static int __rt_queue_send (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    void __user *buf;
    int err, mode;
    RT_QUEUE *q;
    size_t size;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    /* Buffer to send. */
    buf = (void __user *)__xn_reg_arg2(regs);

    /* Message's payload size. */
    size = (size_t)__xn_reg_arg3(regs);

    /* Sending mode. */
    mode = (int)__xn_reg_arg4(regs);

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)rt_registry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. */

    if (buf)
	{
	buf = xnheap_shared_address(&q->bufpool,(caddr_t)buf - ph.mapbase);
	err = rt_queue_send(q,buf,size,mode);
	}
    else
	err = -EINVAL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_recv(RT_QUEUE_PLACEHOLDER *ph,
 *                     void **bufp,
 *                     RTIME *timeoutp)
 */

static int __rt_queue_recv (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RTIME timeout;
    RT_QUEUE *q;
    void *buf;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(buf)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg3(regs),sizeof(timeout));

    xnlock_get_irqsave(&nklock,s);

    q = (RT_QUEUE *)rt_registry_fetch(ph.opaque);

    if (!q)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    err = (int)rt_queue_recv(q,&buf,timeout);

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. */

    if (err >= 0)
	{
	/* Convert the kernel-based address of buf to the equivalent area
	   into the caller's address space. */
	buf = ph.mapbase + xnheap_shared_offset(&q->bufpool,buf);
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&buf,sizeof(buf));
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_queue_inquire(RT_QUEUE_PLACEHOLDER *ph,
 *                        RT_QUEUE_INFO *infop)
 */

static int __rt_queue_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_QUEUE_PLACEHOLDER ph;
    RT_QUEUE_INFO info;
    RT_QUEUE *q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    q = (RT_QUEUE *)rt_registry_fetch(ph.opaque);

    if (!q)
	return -ESRCH;

    err = rt_queue_inquire(q,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_QUEUE */

#define __rt_queue_create    __rt_call_not_available
#define __rt_queue_bind      __rt_call_not_available
#define __rt_queue_delete    __rt_call_not_available
#define __rt_queue_alloc     __rt_call_not_available
#define __rt_queue_free      __rt_call_not_available
#define __rt_queue_send      __rt_call_not_available
#define __rt_queue_recv      __rt_call_not_available
#define __rt_queue_inquire   __rt_call_not_available

#endif /* CONFIG_RTAI_OPT_NATIVE_QUEUE */

#if CONFIG_RTAI_OPT_NATIVE_HEAP

/*
 * int __rt_heap_create(RT_HEAP_PLACEHOLDER *ph,
 *                      const char *name,
 *                      size_t heapsize,
 *                      int mode)
 */

static int __rt_heap_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_HEAP_PLACEHOLDER ph;
    size_t heapsize;
    int err, mode;
    RT_HEAP *heap;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    /* Size of heap space. */
    heapsize = (size_t)__xn_reg_arg3(regs);
    /* Creation mode. */
    mode = (int)__xn_reg_arg4(regs);

    heap = (RT_HEAP *)xnmalloc(sizeof(*heap));

    if (!heap)
	return -ENOMEM;

    err = rt_heap_create(heap,name,heapsize,mode);

    if (err)
	goto free_and_fail;

    heap->source = RT_UAPI_SOURCE;

    /* Copy back the registry handle to the ph struct. */
    ph.opaque = heap->handle;
    ph.opaque2 = &heap->heap_base;
    ph.mapsize = xnheap_size(&heap->heap_base);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

    return 0;

 free_and_fail:
	
    xnfree(heap);

    return err;
}

/*
 * int __rt_heap_bind(RT_HEAP_PLACEHOLDER *ph,
 *                    const char *name)
 */

static int __rt_heap_bind (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP *heap;
    int err;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* First, wait for the heap to appear in the registry. */

    err = __rt_bind_helper(curr,regs,RTAI_HEAP_MAGIC,(void **)&heap);

    if (err)
	goto unlock_and_exit;

    xnlock_put_irqrestore(&nklock,s);

    /* We need to migrate to secondary mode now for mapping the heap
       memory to user-space; we must have entered this syscall in
       primary mode. */

    xnshadow_relax();

    xnlock_get_irqsave(&nklock,s);

    /* Search for the heap again since we released the lock while
       migrating. */

    err = __rt_bind_helper(curr,regs,RTAI_HEAP_MAGIC,(void **)&heap);

    if (err)
	goto unlock_and_exit;

    ph.opaque = heap->handle;
    ph.opaque2 = &heap->heap_base;
    ph.mapsize = xnheap_size(&heap->heap_base);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_heap_delete(RT_HEAP_PLACEHOLDER *ph)
 */

static int __rt_heap_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP *heap;
    int err = 0;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    xnlock_get_irqsave(&nklock,s);

    heap = (RT_HEAP *)rt_registry_fetch(ph.opaque);

    if (!heap)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    err = rt_heap_delete(heap);

    if (!err && heap->source == RT_UAPI_SOURCE)
	xnfree(heap);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_heap_alloc(RT_HEAP_PLACEHOLDER *ph,
 *                     size_t size,
 *                     RTIME timeout,
 *                     void **bufp)
 */

static int __rt_heap_alloc (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP *heap;
    RTIME timeout;
    size_t size;
    int err = 0;
    void *buf;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&timeout,(void __user *)__xn_reg_arg3(regs),sizeof(timeout));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(buf)))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);

    heap = (RT_HEAP *)rt_registry_fetch(ph.opaque);

    if (!heap)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    size = (size_t)__xn_reg_arg2(regs);

    err = rt_heap_alloc(heap,size,timeout,&buf);

    /* Convert the kernel-based address of buf to the equivalent area
       into the caller's address space. */

    if (!err)
	buf = ph.mapbase + xnheap_shared_offset(&heap->heap_base,buf);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    __xn_copy_to_user(curr,(void __user *)__xn_reg_arg4(regs),&buf,sizeof(buf));

    return err;
}

/*
 * int __rt_heap_free(RT_HEAP_PLACEHOLDER *ph,
 *                    void *buf)
 */

static int __rt_heap_free (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    void __user *buf;
    RT_HEAP *heap;
    int err;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    buf = (void __user *)__xn_reg_arg2(regs);

    xnlock_get_irqsave(&nklock,s);

    heap = (RT_HEAP *)rt_registry_fetch(ph.opaque);

    if (!heap)
	{
	err = -ESRCH;
	goto unlock_and_exit;
	}

    /* Convert the caller-based address of buf to the equivalent area
       into the kernel address space. */

    if (buf)
	{
	buf = xnheap_shared_address(&heap->heap_base,(caddr_t)buf - ph.mapbase);
	err = rt_heap_free(heap,buf);
	}
    else
	err = -EINVAL;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_heap_inquire(RT_HEAP_PLACEHOLDER *ph,
 *                       RT_HEAP_INFO *infop)
 */

static int __rt_heap_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_HEAP_PLACEHOLDER ph;
    RT_HEAP_INFO info;
    RT_HEAP *heap;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    heap = (RT_HEAP *)rt_registry_fetch(ph.opaque);

    if (!heap)
	return -ESRCH;

    err = rt_heap_inquire(heap,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_HEAP */

#define __rt_heap_create    __rt_call_not_available
#define __rt_heap_bind      __rt_call_not_available
#define __rt_heap_delete    __rt_call_not_available
#define __rt_heap_alloc     __rt_call_not_available
#define __rt_heap_free      __rt_call_not_available
#define __rt_heap_inquire   __rt_call_not_available

#endif /* CONFIG_RTAI_OPT_NATIVE_HEAP */

#if CONFIG_RTAI_OPT_NATIVE_ALARM

static void __rt_alarm_handler (RT_ALARM *alarm, void *cookie)

{
    /* Wake up all tasks waiting for the alarm. */
    xnsynch_flush(&alarm->synch_base,0);
}

/*
 * int __rt_alarm_create(RT_ALARM_PLACEHOLDER *ph,
 *                       const char *name)
 */

static int __rt_alarm_create (struct task_struct *curr, struct pt_regs *regs)

{
    char name[XNOBJECT_NAME_LEN];
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	    return -EFAULT;

	__xn_copy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
	name[sizeof(name) - 1] = '\0';
	}
    else
	*name = '\0';

    alarm = (RT_ALARM *)xnmalloc(sizeof(*alarm));

    if (!alarm)
	return -ENOMEM;

    err = rt_alarm_create(alarm,name,&__rt_alarm_handler,NULL);

    if (err == 0)
	{
	alarm->source = RT_UAPI_SOURCE;
	/* Copy back the registry handle to the ph struct. */
	ph.opaque = alarm->handle;
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg1(regs),&ph,sizeof(ph));
	}
    else
	xnfree(alarm);

    return err;
}

/*
 * int __rt_alarm_delete(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_delete (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)rt_registry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    err = rt_alarm_delete(alarm);

    if (!err && alarm->source == RT_UAPI_SOURCE)
	xnfree(alarm);

    return err;
}

/*
 * int __rt_alarm_start(RT_ALARM_PLACEHOLDER *ph,
 *			RTIME value,
 *			RTIME interval)
 */

static int __rt_alarm_start (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RTIME value, interval;
    RT_ALARM *alarm;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)rt_registry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    __xn_copy_from_user(curr,&value,(void __user *)__xn_reg_arg2(regs),sizeof(value));
    __xn_copy_from_user(curr,&interval,(void __user *)__xn_reg_arg3(regs),sizeof(interval));

    return rt_alarm_start(alarm,value,interval);
}

/*
 * int __rt_alarm_stop(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_stop (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)rt_registry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    return rt_alarm_stop(alarm);
}

/*
 * int __rt_alarm_wait(RT_ALARM_PLACEHOLDER *ph)
 */

static int __rt_alarm_wait (struct task_struct *curr, struct pt_regs *regs)

{
    RT_TASK *task = rtai_current_task();
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM *alarm;
    int err = 0;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)rt_registry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    xnlock_get_irqsave(&nklock,s);

    if (xnthread_base_priority(&task->thread_base) != FUSION_IRQ_PRIO)
	/* Renice the waiter above all regular tasks if needed. */
	xnpod_renice_thread(&task->thread_base,
			    FUSION_IRQ_PRIO);

    xnsynch_sleep_on(&alarm->synch_base,XN_INFINITE);

    if (xnthread_test_flags(&task->thread_base,XNRMID))
	err = -EIDRM; /* Alarm deleted while pending. */
    else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/
    
    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*
 * int __rt_alarm_inquire(RT_ALARM_PLACEHOLDER *ph,
 *                        RT_ALARM_INFO *infop)
 */

static int __rt_alarm_inquire (struct task_struct *curr, struct pt_regs *regs)

{
    RT_ALARM_PLACEHOLDER ph;
    RT_ALARM_INFO info;
    RT_ALARM *alarm;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ph)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(info)))
	return -EFAULT;

    __xn_copy_from_user(curr,&ph,(void __user *)__xn_reg_arg1(regs),sizeof(ph));

    alarm = (RT_ALARM *)rt_registry_fetch(ph.opaque);

    if (!alarm)
	return -ESRCH;

    err = rt_alarm_inquire(alarm,&info);

    if (!err)
	__xn_copy_to_user(curr,(void __user *)__xn_reg_arg2(regs),&info,sizeof(info));

    return err;
}

#else /* !CONFIG_RTAI_OPT_NATIVE_ALARM */

#define __rt_alarm_create     __rt_call_not_available
#define __rt_alarm_delete     __rt_call_not_available
#define __rt_alarm_start      __rt_call_not_available
#define __rt_alarm_stop       __rt_call_not_available
#define __rt_alarm_wait       __rt_call_not_available
#define __rt_alarm_inquire    __rt_call_not_available

#endif /* CONFIG_RTAI_OPT_NATIVE_ALARM */

static  __attribute__((unused))
int __rt_call_not_available (struct task_struct *curr, struct pt_regs *regs) {
    return -ENOSYS;
}

static xnsysent_t __systab[] = {
    [__rtai_task_create ] = { &__rt_task_create, __xn_flag_init },
    [__rtai_task_bind ] = { &__rt_task_bind, __xn_flag_init },
    [__rtai_task_start ] = { &__rt_task_start, __xn_flag_anycall },
    [__rtai_task_suspend ] = { &__rt_task_suspend, __xn_flag_regular },
    [__rtai_task_resume ] = { &__rt_task_resume, __xn_flag_anycall },
    [__rtai_task_delete ] = { &__rt_task_delete, __xn_flag_anycall },
    [__rtai_task_yield ] = { &__rt_task_yield, __xn_flag_regular },
    [__rtai_task_set_periodic ] = { &__rt_task_set_periodic, __xn_flag_regular },
    [__rtai_task_wait_period ] = { &__rt_task_wait_period, __xn_flag_regular },
    [__rtai_task_set_priority ] = { &__rt_task_set_priority, __xn_flag_anycall },
    [__rtai_task_sleep ] = { &__rt_task_sleep, __xn_flag_regular },
    [__rtai_task_sleep_until ] = { &__rt_task_sleep_until, __xn_flag_regular },
    [__rtai_task_unblock ] = { &__rt_task_unblock, __xn_flag_anycall },
    [__rtai_task_inquire ] = { &__rt_task_inquire, __xn_flag_anycall },
    [__rtai_task_notify ] = { &__rt_task_notify, __xn_flag_anycall },
    [__rtai_task_set_mode ] = { &__rt_task_set_mode, __xn_flag_regular },
    [__rtai_task_self ] = { &__rt_task_self, __xn_flag_anycall },
    [__rtai_task_slice ] = { &__rt_task_slice, __xn_flag_anycall },
    [__rtai_timer_start ] = { &__rt_timer_start, __xn_flag_anycall },
    [__rtai_timer_stop ] = { &__rt_timer_stop, __xn_flag_anycall },
    [__rtai_timer_read ] = { &__rt_timer_read, __xn_flag_anycall },
    [__rtai_timer_tsc ] = { &__rt_timer_tsc, __xn_flag_anycall },
    [__rtai_timer_ns2ticks ] = { &__rt_timer_ns2ticks, __xn_flag_anycall },
    [__rtai_timer_ticks2ns ] = { &__rt_timer_ticks2ns, __xn_flag_anycall },
    [__rtai_timer_inquire ] = { &__rt_timer_inquire, __xn_flag_anycall },
    [__rtai_sem_create ] = { &__rt_sem_create, __xn_flag_anycall },
    [__rtai_sem_bind ] = { &__rt_sem_bind, __xn_flag_regular },
    [__rtai_sem_delete ] = { &__rt_sem_delete, __xn_flag_anycall },
    [__rtai_sem_p ] = { &__rt_sem_p, __xn_flag_regular },
    [__rtai_sem_v ] = { &__rt_sem_v, __xn_flag_anycall },
    [__rtai_sem_inquire ] = { &__rt_sem_inquire, __xn_flag_anycall },
    [__rtai_event_create ] = { &__rt_event_create, __xn_flag_anycall },
    [__rtai_event_bind ] = { &__rt_event_bind, __xn_flag_regular },
    [__rtai_event_delete ] = { &__rt_event_delete, __xn_flag_anycall },
    [__rtai_event_wait ] = { &__rt_event_wait, __xn_flag_regular },
    [__rtai_event_signal ] = { &__rt_event_signal, __xn_flag_anycall },
    [__rtai_event_clear ] = { &__rt_event_clear, __xn_flag_anycall },
    [__rtai_event_inquire ] = { &__rt_event_inquire, __xn_flag_anycall },
    [__rtai_mutex_create ] = { &__rt_mutex_create, __xn_flag_anycall },
    [__rtai_mutex_bind ] = { &__rt_mutex_bind, __xn_flag_regular },
    [__rtai_mutex_delete ] = { &__rt_mutex_delete, __xn_flag_anycall },
    [__rtai_mutex_lock ] = { &__rt_mutex_lock, __xn_flag_regular },
    [__rtai_mutex_unlock ] = { &__rt_mutex_unlock, __xn_flag_shadow },
    [__rtai_mutex_inquire ] = { &__rt_mutex_inquire, __xn_flag_anycall },
    [__rtai_cond_create ] = { &__rt_cond_create, __xn_flag_anycall },
    [__rtai_cond_bind ] = { &__rt_cond_bind, __xn_flag_regular },
    [__rtai_cond_delete ] = { &__rt_cond_delete, __xn_flag_anycall },
    [__rtai_cond_wait ] = { &__rt_cond_wait, __xn_flag_regular },
    [__rtai_cond_signal ] = { &__rt_cond_signal, __xn_flag_anycall },
    [__rtai_cond_broadcast ] = { &__rt_cond_broadcast, __xn_flag_anycall },
    [__rtai_cond_inquire ] = { &__rt_cond_inquire, __xn_flag_anycall },
    [__rtai_queue_create ] = { &__rt_queue_create, __xn_flag_lostage },
    [__rtai_queue_bind ] = { &__rt_queue_bind, __xn_flag_regular },
    [__rtai_queue_delete ] = { &__rt_queue_delete, __xn_flag_anycall },
    [__rtai_queue_alloc ] = { &__rt_queue_alloc, __xn_flag_anycall },
    [__rtai_queue_free ] = { &__rt_queue_free, __xn_flag_anycall },
    [__rtai_queue_send ] = { &__rt_queue_send, __xn_flag_anycall },
    [__rtai_queue_recv ] = { &__rt_queue_recv, __xn_flag_regular },
    [__rtai_queue_inquire ] = { &__rt_queue_inquire, __xn_flag_anycall },
    [__rtai_heap_create ] = { &__rt_heap_create, __xn_flag_lostage },
    [__rtai_heap_bind ] = { &__rt_heap_bind, __xn_flag_regular },
    [__rtai_heap_delete ] = { &__rt_heap_delete, __xn_flag_anycall },
    [__rtai_heap_alloc ] = { &__rt_heap_alloc, __xn_flag_regular },
    [__rtai_heap_free ] = { &__rt_heap_free, __xn_flag_anycall },
    [__rtai_heap_inquire ] = { &__rt_heap_inquire, __xn_flag_anycall },
    [__rtai_alarm_create ] = { &__rt_alarm_create, __xn_flag_anycall },
    [__rtai_alarm_delete ] = { &__rt_alarm_delete, __xn_flag_anycall },
    [__rtai_alarm_start ] = { &__rt_alarm_start, __xn_flag_anycall },
    [__rtai_alarm_stop ] = { &__rt_alarm_stop, __xn_flag_anycall },
    [__rtai_alarm_wait ] = { &__rt_alarm_wait, __xn_flag_regular },
    [__rtai_alarm_inquire ] = { &__rt_alarm_inquire, __xn_flag_anycall },
};

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == RTAI_SKIN_MAGIC &&
	testbits(thread->status,XNSHADOW))
	xnshadow_unmap(thread);
}

int __syscall_pkg_init (void)

{
    __muxid =
	xnshadow_register_interface("native",
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
    xnshadow_unregister_interface(__muxid);
}
