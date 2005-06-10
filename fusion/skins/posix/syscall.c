/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org> 
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

#include <posix/syscall.h>
#include <posix/posix.h>
#include <posix/thread.h>

static int __muxid;

int __pthread_create (struct task_struct *curr, struct pt_regs *regs)

{
    struct sched_param param;
    pthread_t internal_tid;
    pthread_attr_t attr;
    int err;

    if (curr->policy != SCHED_FIFO) /* Only allow FIFO for now. */
	return -EINVAL;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(internal_tid)))
	return -EFAULT;

    /* Build a default thread attribute, then make sure that a few
       critical fields are set in a compatible fashion wrt to the
       calling context. */

    pthread_attr_init(&attr);
    attr.policy = curr->policy;
    param.sched_priority = curr->rt_priority;
    attr.schedparam = param;
    attr.fp = 1;
    attr.name = curr->comm;

    err = pthread_create(&internal_tid,&attr,NULL,NULL);

    if (err)
	return -err; /* Conventionally, our error codes are negative. */

    err = xnshadow_map(&internal_tid->threadbase,NULL);

    if (!err)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg1(regs),
			  &internal_tid,
			  sizeof(internal_tid));
    else
        pse51_thread_abort(internal_tid, NULL);
	
    return err;
}

int __pthread_detach (struct task_struct *curr, struct pt_regs *regs)

{ 
    pthread_t internal_tid = (pthread_t)__xn_reg_arg1(regs);
    return -pthread_detach(internal_tid);
}

int __pthread_setschedparam (struct task_struct *curr, struct pt_regs *regs)

{ 
    struct sched_param param;
    pthread_t internal_tid;
    int policy;

    policy = __xn_reg_arg2(regs);

    if (policy != SCHED_FIFO)
	/* User-space POSIX shadows only support SCHED_FIFO for now. */
	return -EINVAL;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(param)))
	return -EFAULT;

    internal_tid = (pthread_t)__xn_reg_arg1(regs);

    __xn_copy_from_user(curr,
			&param,
			(void __user *)__xn_reg_arg3(regs),
			sizeof(param));

    return -pthread_setschedparam(internal_tid,policy,&param);
}

int __sched_yield (struct task_struct *curr, struct pt_regs *regs)

{
    return -sched_yield();
}

int __pthread_make_periodic_np (struct task_struct *curr, struct pt_regs *regs)

{ 
    struct timespec startt, periodt;
    pthread_t internal_tid;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(startt)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(periodt)))
	return -EFAULT;

    internal_tid = (pthread_t)__xn_reg_arg1(regs);

    __xn_copy_from_user(curr,
			&startt,
			(void __user *)__xn_reg_arg2(regs),
			sizeof(startt));

    __xn_copy_from_user(curr,
			&periodt,
			(void __user *)__xn_reg_arg3(regs),
			sizeof(periodt));

    return -pthread_make_periodic_np(internal_tid,&startt,&periodt);
}

int __pthread_wait_np (struct task_struct *curr, struct pt_regs *regs)

{
    return -pthread_wait_np();
}

int __sem_init (struct task_struct *curr, struct pt_regs *regs)

{
    unsigned long handle;
    unsigned value;
    int pshared;
    sem_t *sem;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(handle)))
	return -EFAULT;

    sem = (sem_t *)xnmalloc(sizeof(*sem));

    if (!sem)
	return -ENOMEM;

    pshared = (int)__xn_reg_arg2(regs);
    value = (unsigned)__xn_reg_arg3(regs);

    if (sem_init(sem,pshared,value) == -1)
        return -thread_errno();

    handle = (unsigned long)sem;

    __xn_copy_to_user(curr,
		      (void __user *)__xn_reg_arg1(regs),
		      &handle,
		      sizeof(handle));
    return 0;
}

int __sem_post (struct task_struct *curr, struct pt_regs *regs)

{
    sem_t *sem = (sem_t *)__xn_reg_arg1(regs);
    return sem_post(sem) == 0 ? 0 : -thread_errno();
}

int __sem_wait (struct task_struct *curr, struct pt_regs *regs)

{
    sem_t *sem = (sem_t *)__xn_reg_arg1(regs);
    return sem_wait(sem) == 0 ? 0 : -thread_errno();
}

int __sem_destroy (struct task_struct *curr, struct pt_regs *regs)

{
    sem_t *sem = (sem_t *)__xn_reg_arg1(regs);
    int err;

    err = sem_destroy(sem);

    if (err)
	return -thread_errno();

    /* The caller first checked its own magic value
       (SHADOW_SEMAPHORE_MAGIC) before calling us with our internal
       handle, then the kernel skin did the same to validate our
       handle (PSE51_SEM_MAGIC), so at this point, if everything has
       been ok so far, we can reasonably expect the sem block to be
       valid, so let's free it. */

    xnfree(sem);

    return 0;
}

int __clock_getres (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ts)))
	return -EFAULT;

    err = clock_getres(CLOCK_MONOTONIC,&ts);

    if (!err)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg1(regs),
			  &ts,
			  sizeof(ts));
    return -err;
}

int __clock_gettime (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(ts)))
	return -EFAULT;

    err = clock_gettime(CLOCK_MONOTONIC,&ts);

    if (!err)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg1(regs),
			  &ts,
			  sizeof(ts));
    return -err;
}

int __clock_settime (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(ts)))
	return -EFAULT;

    __xn_copy_from_user(curr,
			&ts,
			(void __user *)__xn_reg_arg1(regs),
			sizeof(ts));

    return -clock_settime(CLOCK_MONOTONIC,&ts);
}

int __clock_nanosleep (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec rqt, rmt, *rmtp = NULL;
    int flags, err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(rqt)))
	return -EFAULT;

    if (__xn_reg_arg3(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(rmt)))
	    return -EFAULT;

	rmtp = &rmt;
	}

    flags = (int)__xn_reg_arg1(regs);

    __xn_copy_from_user(curr,
			&rqt,
			(void __user *)__xn_reg_arg2(regs),
			sizeof(rqt));

    err = clock_nanosleep(CLOCK_MONOTONIC,flags,&rqt,rmtp);

    if (err)
	return -err;

    if (rmtp)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg3(regs),
			  rmtp,
			  sizeof(*rmtp));
    return 0;
}

int __mutex_init (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_mutexattr_t attr;
    pthread_mutex_t *mutex;
    unsigned long handle;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(handle)))
	return -EFAULT;

    mutex = (pthread_mutex_t *)xnmalloc(sizeof(*mutex));

    if (!mutex)
	return -ENOMEM;

    /* Recursive + PIP forced. */
    pthread_mutexattr_init(&attr);
    pthread_mutexattr_setprotocol(&attr,PTHREAD_PRIO_INHERIT);
    err = pthread_mutex_init(mutex,&attr);

    if (err)
        return -err;

    handle = (unsigned long)mutex;

    __xn_copy_to_user(curr,
		      (void __user *)__xn_reg_arg1(regs),
		      &handle,
		      sizeof(handle));
    return 0;
}

int __mutex_destroy (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_mutex_t *mutex = (pthread_mutex_t *)__xn_reg_arg1(regs);
    int err;

    err = pthread_mutex_destroy(mutex);

    if (err)
	return -err;

    /* Same comment as for sem_destroy(): if everything has been ok so
       far, we can reasonably expect the mutex block to be valid, so
       let's free it. */

    xnfree(mutex);

    return 0;
}

int __mutex_lock (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_mutex_t *mutex = (pthread_mutex_t *)__xn_reg_arg1(regs);
    return -pthread_mutex_lock(mutex);
}

int __mutex_timedlock (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_mutex_t *mutex = (pthread_mutex_t *)__xn_reg_arg1(regs);
    struct timespec ts;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(ts)))
	return -EFAULT;

    __xn_copy_from_user(curr,
			&ts,
			(void __user *)__xn_reg_arg2(regs),
			sizeof(ts));

    return -pthread_mutex_timedlock(mutex,&ts);
}

int __mutex_trylock (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_mutex_t *mutex = (pthread_mutex_t *)__xn_reg_arg1(regs);
    return -pthread_mutex_trylock(mutex);
}

int __mutex_unlock (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_mutex_t *mutex = (pthread_mutex_t *)__xn_reg_arg1(regs);
    return -pthread_mutex_unlock(mutex);
}

int __cond_init (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_cond_t *cond;
    unsigned long handle;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(handle)))
	return -EFAULT;

    cond = (pthread_cond_t *)xnmalloc(sizeof(*cond));

    if (!cond)
	return -ENOMEM;

    err = pthread_cond_init(cond,NULL);	/* Always use default attribute. */

    if (err)
        return -err;

    handle = (unsigned long)cond;

    __xn_copy_to_user(curr,
		      (void __user *)__xn_reg_arg1(regs),
		      &handle,
		      sizeof(handle));
    return 0;
}

int __cond_destroy (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_cond_t *cond = (pthread_cond_t *)__xn_reg_arg1(regs);
    int err;

    err = pthread_cond_destroy(cond);

    if (err)
	return -err;

    xnfree(cond);

    return 0;
}

int __cond_wait (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_cond_t *cond = (pthread_cond_t *)__xn_reg_arg1(regs);
    pthread_mutex_t *mutex = (pthread_mutex_t *)__xn_reg_arg2(regs);
    return -pthread_cond_wait(cond,mutex);
}

int __cond_timedwait (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_cond_t *cond = (pthread_cond_t *)__xn_reg_arg1(regs);
    pthread_mutex_t *mutex = (pthread_mutex_t *)__xn_reg_arg2(regs);
    struct timespec ts;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(ts)))
	return -EFAULT;

    __xn_copy_from_user(curr,
			&ts,
			(void __user *)__xn_reg_arg3(regs),
			sizeof(ts));

    return -pthread_cond_timedwait(cond,mutex,&ts);
}

int __cond_signal (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_cond_t *cond = (pthread_cond_t *)__xn_reg_arg1(regs);
    return -pthread_cond_signal(cond);
}

int __cond_broadcast (struct task_struct *curr, struct pt_regs *regs)

{
    pthread_cond_t *cond = (pthread_cond_t *)__xn_reg_arg1(regs);
    return -pthread_cond_broadcast(cond);
}

static xnsysent_t __systab[] = {
    [__pse51_thread_create ] = { &__pthread_create, __xn_exec_init },
    [__pse51_thread_detach ] = { &__pthread_detach, __xn_exec_any },
    [__pse51_thread_setschedparam ] = { &__pthread_setschedparam, __xn_exec_any },
    [__pse51_sched_yield ] = { &__sched_yield, __xn_exec_primary },
    [__pse51_thread_make_periodic ] = { &__pthread_make_periodic_np, __xn_exec_primary },
    [__pse51_thread_wait] = { &__pthread_wait_np, __xn_exec_primary },
    [__pse51_sem_init] = { &__sem_init, __xn_exec_any },
    [__pse51_sem_destroy] = { &__sem_destroy, __xn_exec_any },
    [__pse51_sem_post] = { &__sem_post, __xn_exec_any },
    [__pse51_sem_wait] = { &__sem_wait, __xn_exec_primary },
    [__pse51_clock_getres] = { &__clock_getres, __xn_exec_any },
    [__pse51_clock_gettime] = { &__clock_gettime, __xn_exec_any },
    [__pse51_clock_settime] = { &__clock_settime, __xn_exec_any },
    [__pse51_clock_nanosleep] = { &__clock_nanosleep, __xn_exec_primary },
    [__pse51_mutex_init] = { &__mutex_init, __xn_exec_any },
    [__pse51_mutex_destroy] = { &__mutex_destroy, __xn_exec_any },
    [__pse51_mutex_lock] = { &__mutex_lock, __xn_exec_primary },
    [__pse51_mutex_timedlock] = { &__mutex_timedlock, __xn_exec_primary },
    [__pse51_mutex_trylock] = { &__mutex_trylock, __xn_exec_primary },
    [__pse51_mutex_unlock] = { &__mutex_unlock, __xn_exec_primary },
    [__pse51_cond_init] = { &__cond_init, __xn_exec_any },
    [__pse51_cond_destroy] = { &__cond_destroy, __xn_exec_any },
    [__pse51_cond_wait] = { &__cond_wait, __xn_exec_primary },
    [__pse51_cond_timedwait] = { &__cond_timedwait, __xn_exec_primary },
    [__pse51_cond_signal] = { &__cond_signal, __xn_exec_any },
    [__pse51_cond_broadcast] = { &__cond_broadcast, __xn_exec_any },
};

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == PSE51_SKIN_MAGIC &&
	testbits(thread->status,XNSHADOW))
	xnshadow_unmap(thread);
}

int __pse51_syscall_init (void)

{
    __muxid =
	xnshadow_register_interface("posix",
				    PSE51_SKIN_MAGIC,
				    sizeof(__systab) / sizeof(__systab[0]),
				    __systab,
				    NULL);
    if (__muxid < 0)
	return -ENOSYS;

    xnpod_add_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    
    return 0;
}

void __pse51_syscall_cleanup (void)

{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_interface(__muxid);
}
