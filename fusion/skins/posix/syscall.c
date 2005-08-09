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
#include <posix/mutex.h>
#include <posix/cond.h>
#include <posix/jhash.h>
#include <posix/mq.h>
#include <posix/intr.h>

static int __muxid;

int __pse51_errptd;

struct pthread_jhash {

#define PTHREAD_HASHBITS 8

    pthread_t k_tid;
    struct pse51_hkey hkey;
    struct pthread_jhash *next;
};

static struct pthread_jhash *__jhash_buckets[1<<PTHREAD_HASHBITS]; /* Guaranteed zero */

/* We want to keep the native pthread_t token unmodified for
   RTAI/fusion mapped threads, and keep it pointing at a genuine
   NPTL/LinuxThreads descriptor, so that portions of the POSIX
   interface which are not overriden by fusion fall back to the
   original Linux services. If the latter invoke Linux system calls,
   the associated shadow thread will simply switch to secondary exec
   mode to perform them. For this reason, we need an external index to
   map regular pthread_t values to fusion's internal thread ids used
   in syscalling the POSIX skin, so that the outer interface can keep
   on using the former transparently. Semaphores and mutexes do not
   have this constraint, since we fully override their respective
   interfaces with RTAI/fusion-based replacements. */

static inline struct pthread_jhash *__pthread_hash (const struct pse51_hkey *hkey,
						    pthread_t k_tid)
{
    struct pthread_jhash **bucketp;
    struct pthread_jhash *slot;
    u32 hash;
    spl_t s;

    slot = (struct pthread_jhash *)xnmalloc(sizeof(*slot));

    if (!slot)
	return NULL;

    slot->hkey = *hkey;
    slot->k_tid = k_tid;

    hash = jhash2((u32 *)&slot->hkey,
		  sizeof(slot->hkey)/sizeof(u32),
		  0);

    bucketp = &__jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];

    xnlock_get_irqsave(&nklock,s);
    slot->next = *bucketp;
    *bucketp = slot;
    xnlock_put_irqrestore(&nklock,s);

    return slot;
}

static inline void __pthread_unhash (const struct pse51_hkey *hkey)

{
    struct pthread_jhash **tail, *slot;
    u32 hash;
    spl_t s;

    hash = jhash2((u32 *)hkey,
		  sizeof(*hkey)/sizeof(u32),
		  0);

    tail = &__jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];

    xnlock_get_irqsave(&nklock,s);

    slot = *tail;

    while (slot != NULL &&
	   (slot->hkey.u_tid != hkey->u_tid ||
	    slot->hkey.mm != hkey->mm))
	{
	tail = &slot->next;
    	slot = *tail;
	}

    if (slot)
	*tail = slot->next;

    xnlock_put_irqrestore(&nklock,s);

    if (slot)
	xnfree(slot);
}

static pthread_t __pthread_find (const struct pse51_hkey *hkey)

{
    struct pthread_jhash *slot;
    pthread_t k_tid;
    u32 hash;
    spl_t s;

    hash = jhash2((u32 *)hkey,
		  sizeof(*hkey)/sizeof(u32),
		  0);

    xnlock_get_irqsave(&nklock,s);

    slot = __jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];

    while (slot != NULL &&
	   (slot->hkey.u_tid != hkey->u_tid ||
	    slot->hkey.mm != hkey->mm))
    	slot = slot->next;

    k_tid = slot ? slot->k_tid : NULL;

    xnlock_put_irqrestore(&nklock,s);

    return k_tid;
}

int __pthread_create (struct task_struct *curr, struct pt_regs *regs)

{
    struct sched_param param;
    struct pse51_hkey hkey;
    pthread_attr_t attr;
    pthread_t k_tid;
    int err;

    if (curr->policy != SCHED_FIFO) /* Only allow FIFO for now. */
	return -EINVAL;

    /* We have been passed the pthread_t identifier the user-space
       POSIX library has assigned to our caller; we'll index our
       internal pthread_t descriptor in kernel space on it. */
    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;

    /* Build a default thread attribute, then make sure that a few
       critical fields are set in a compatible fashion wrt to the
       calling context. */

    pthread_attr_init(&attr);
    attr.policy = curr->policy;
    param.sched_priority = curr->rt_priority;
    attr.schedparam = param;
    attr.fp = 1;
    attr.name = curr->comm;

    err = pthread_create(&k_tid,&attr,NULL,NULL);

    if (err)
	return -err; /* Conventionally, our error codes are negative. */

    err = xnshadow_map(&k_tid->threadbase,NULL);

    if (!err && !__pthread_hash(&hkey,k_tid))
	err = -ENOMEM;

    if (err)
        pse51_thread_abort(k_tid, NULL);
    else
	k_tid->hkey = hkey;
	
    return err;
}

int __pthread_detach (struct task_struct *curr, struct pt_regs *regs)

{ 
    struct pse51_hkey hkey;
    pthread_t k_tid;

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    return -pthread_detach(k_tid);
}

int __pthread_setschedparam (struct task_struct *curr, struct pt_regs *regs)

{ 
    struct sched_param param;
    struct pse51_hkey hkey;
    pthread_t k_tid;
    int policy;

    policy = __xn_reg_arg2(regs);

    if (policy != SCHED_FIFO)
	/* User-space POSIX shadows only support SCHED_FIFO for now. */
	return -EINVAL;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(param)))
	return -EFAULT;

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    __xn_copy_from_user(curr,
			&param,
			(void __user *)__xn_reg_arg3(regs),
			sizeof(param));

    return -pthread_setschedparam(k_tid,policy,&param);
}

int __sched_yield (struct task_struct *curr, struct pt_regs *regs)

{
    return -sched_yield();
}

int __pthread_make_periodic_np (struct task_struct *curr, struct pt_regs *regs)

{ 
    struct timespec startt, periodt;
    struct pse51_hkey hkey;
    pthread_t k_tid;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(startt)))
	return -EFAULT;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(periodt)))
	return -EFAULT;

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    __xn_copy_from_user(curr,
			&startt,
			(void __user *)__xn_reg_arg2(regs),
			sizeof(startt));

    __xn_copy_from_user(curr,
			&periodt,
			(void __user *)__xn_reg_arg3(regs),
			sizeof(periodt));

    return -pthread_make_periodic_np(k_tid,&startt,&periodt);
}

int __pthread_wait_np (struct task_struct *curr, struct pt_regs *regs)

{
    return -pthread_wait_np();
}

int __pthread_set_mode_np (struct task_struct *curr, struct pt_regs *regs)

{
    xnflags_t clrmask, setmask;
    struct pse51_hkey hkey;
    pthread_t k_tid;
    spl_t s;
    int err;

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);
    clrmask = __xn_reg_arg2(regs);
    setmask = __xn_reg_arg3(regs);

    if ((clrmask & ~(XNSHIELD|XNTRAPSW)) != 0 ||
	(setmask & ~(XNSHIELD|XNTRAPSW)) != 0)
	return -EINVAL;

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(k_tid, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return -ESRCH;
	}

    err = xnpod_set_thread_mode(&k_tid->threadbase,clrmask,setmask);

    xnlock_put_irqrestore(&nklock, s);

    return err;
}

int __pthread_set_name_np (struct task_struct *curr, struct pt_regs *regs)

{ 
    char name[XNOBJECT_NAME_LEN];
    struct pse51_hkey hkey;
    pthread_t k_tid;
    spl_t s;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(name)))
	return -EFAULT;

    __xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg2(regs),sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    hkey.u_tid = __xn_reg_arg1(regs);
    hkey.mm = curr->mm;
    k_tid = __pthread_find(&hkey);

    xnlock_get_irqsave(&nklock, s);

    if (!pse51_obj_active(k_tid, PSE51_THREAD_MAGIC, struct pse51_thread))
	{
        xnlock_put_irqrestore(&nklock, s);
        return -ESRCH;
	}

    strcpy(xnthread_name(&k_tid->threadbase),name);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
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
        return -thread_get_errno();

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
    return sem_post(sem) == 0 ? 0 : -thread_get_errno();
}

int __sem_wait (struct task_struct *curr, struct pt_regs *regs)

{
    sem_t *sem = (sem_t *)__xn_reg_arg1(regs);
    return sem_wait(sem) == 0 ? 0 : -thread_get_errno();
}

int __sem_trywait (struct task_struct *curr, struct pt_regs *regs)

{
    sem_t *sem = (sem_t *)__xn_reg_arg1(regs);
    return sem_trywait(sem) == 0 ? 0 : -thread_get_errno();
}

int __sem_getvalue (struct task_struct *curr, struct pt_regs *regs)

{
    sem_t *sem = (sem_t *)__xn_reg_arg1(regs);
    int err, sval;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(sval)))
	return -EFAULT;

    err = sem_getvalue(sem,&sval);

    if (err)
	return -thread_get_errno();

    __xn_copy_to_user(curr,
		      (void __user *)__xn_reg_arg2(regs),
		      &sval,
		      sizeof(sval));
    return 0;
}

int __sem_destroy (struct task_struct *curr, struct pt_regs *regs)

{
    sem_t *sem = (sem_t *)__xn_reg_arg1(regs);
    int err;

    err = sem_destroy(sem);

    if (err)
	return -thread_get_errno();

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
    clockid_t clock_id;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(ts)))
	return -EFAULT;

    clock_id = __xn_reg_arg1(regs);

    err = clock_getres(clock_id,&ts);

    if (!err)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg2(regs),
			  &ts,
			  sizeof(ts));
    return err ? -thread_get_errno() : 0;
}

int __clock_gettime (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;
    clockid_t clock_id;
    int err;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(ts)))
	return -EFAULT;

    clock_id = __xn_reg_arg1(regs);

    err = clock_gettime(clock_id,&ts);

    if (!err)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg2(regs),
			  &ts,
			  sizeof(ts));
    return err ? -thread_get_errno() : 0;
}

int __clock_settime (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec ts;
    clockid_t clock_id;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(ts)))
	return -EFAULT;

    clock_id = __xn_reg_arg1(regs);

    __xn_copy_from_user(curr,
			&ts,
			(void __user *)__xn_reg_arg2(regs),
			sizeof(ts));

    return clock_settime(clock_id,&ts) ? -thread_get_errno() : 0;
}

int __clock_nanosleep (struct task_struct *curr, struct pt_regs *regs)

{
    struct timespec rqt, rmt, *rmtp = NULL;
    clockid_t clock_id;
    int flags, err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(rqt)))
	return -EFAULT;

    if (__xn_reg_arg3(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(rmt)))
	    return -EFAULT;

	rmtp = &rmt;
	}

    clock_id = __xn_reg_arg1(regs);

    flags = (int)__xn_reg_arg2(regs);

    __xn_copy_from_user(curr,
			&rqt,
			(void __user *)__xn_reg_arg3(regs),
			sizeof(rqt));

    err = clock_nanosleep(clock_id,flags,&rqt,rmtp);

    if (err != EINTR)
	return -err;

    if (rmtp)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg4(regs),
			  rmtp,
			  sizeof(*rmtp));
    return -EINTR;
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
    pthread_mutexattr_settype(&attr,PTHREAD_MUTEX_RECURSIVE);
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
    return -pse51_mutex_timedlock_break(mutex, XN_INFINITE);
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

    return -pse51_mutex_timedlock_break(mutex,ts2ticks_ceil(&ts)+1);
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
    return -pse51_cond_timedwait_internal(cond, mutex, XN_INFINITE);
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

    return -pse51_cond_timedwait_internal(cond,mutex,ts2ticks_ceil(&ts)+1);
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

int __mq_open (struct task_struct *curr, struct pt_regs *regs)

{
    struct mq_attr attr;
    char name[64];
    mode_t mode;
    int oflags;
    mqd_t q;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(name)))
	return -EFAULT;

    __xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg1(regs),sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    oflags = __xn_reg_arg2(regs);
    mode = __xn_reg_arg3(regs);

    if (__xn_reg_arg4(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg4(regs),sizeof(attr)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&attr,(struct mq_attr *)__xn_reg_arg4(regs),sizeof(attr));
	}
    else
	{
	/* Won't be used, but still, we make sure that it can't be
	   used. */
	attr.mq_flags = 0;
	attr.mq_maxmsg = 0;
	attr.mq_msgsize = 0;
	attr.mq_curmsgs = 0;
	}

    q = mq_open(name,oflags,mode,&attr);
    
    return q == (mqd_t)-1 ? -thread_get_errno() : 0;
}

int __mq_close (struct task_struct *curr, struct pt_regs *regs)
{
    mqd_t q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(q)))
	return -EFAULT;

    __xn_copy_from_user(curr,&q,(mqd_t *)__xn_reg_arg1(regs),sizeof(q));

    err = mq_close(q);

    return err ? -thread_get_errno() : 0;
}

int __mq_unlink (struct task_struct *curr, struct pt_regs *regs)
{
    char name[64];
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(name)))
	return -EFAULT;

    __xn_strncpy_from_user(curr,name,(const char __user *)__xn_reg_arg1(regs),sizeof(name) - 1);
    name[sizeof(name) - 1] = '\0';

    err = mq_unlink(name);

    return err ? -thread_get_errno() : 0;
}

int __mq_getattr (struct task_struct *curr, struct pt_regs *regs)
{
    struct mq_attr attr;
    mqd_t q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(q)))
	return -EFAULT;

    __xn_copy_from_user(curr,&q,(mqd_t *)__xn_reg_arg1(regs),sizeof(q));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),sizeof(attr)))
	return -EFAULT;

    err = mq_getattr(q,&attr);

    if (err)
	return -thread_get_errno();

    __xn_copy_to_user(curr,
		      (void __user *)__xn_reg_arg2(regs),
		      &attr,
		      sizeof(attr));
    return 0;
}

int __mq_setattr (struct task_struct *curr, struct pt_regs *regs)
{
    struct mq_attr attr, oattr;
    mqd_t q;
    int err;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(q)))
	return -EFAULT;

    __xn_copy_from_user(curr,&q,(mqd_t *)__xn_reg_arg1(regs),sizeof(q));

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(attr)))
	return -EFAULT;

    __xn_copy_from_user(curr,&attr,(struct mq_attr *)__xn_reg_arg2(regs),sizeof(attr));

    if (__xn_reg_arg3(regs) &&
	!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg3(regs),sizeof(oattr)))
	return -EFAULT;

    err = mq_setattr(q,&attr,&oattr);

    if (err)
	return -thread_get_errno();

    if (__xn_reg_arg3(regs))
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg3(regs),
			  &oattr,
			  sizeof(oattr));
    return 0;
}

int __mq_send (struct task_struct *curr, struct pt_regs *regs)
{
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    size_t len;
    int err;
    mqd_t q;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(q)))
	return -EFAULT;

    __xn_copy_from_user(curr,&q,(mqd_t *)__xn_reg_arg1(regs),sizeof(q));

    len = (size_t)__xn_reg_arg3(regs);
    prio = __xn_reg_arg4(regs);

    if (len > 0)
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),len))
	    return -EFAULT;

	/* Try optimizing a bit here: if the message size can fit into
	   our local buffer, use the latter; otherwise, take the slow
	   path and fetch a larger buffer from the system heap. Most
	   messages are expected to be short enough to fit on the
	   stack anyway. */

	if (len <= sizeof(tmp_buf))
	    tmp_area = tmp_buf;
	else
	    {
	    tmp_area = xnmalloc(len);

	    if (!tmp_area)
		return -ENOMEM;
	    }

	__xn_copy_from_user(curr,tmp_area,(void __user *)__xn_reg_arg2(regs),len);
	}
    else
	tmp_area = NULL;

    err = mq_send(q,tmp_area,len,prio);

    if (tmp_area && tmp_area != tmp_buf)
	xnfree(tmp_area);

    return err ? -thread_get_errno() : 0;
}

int __mq_timedsend (struct task_struct *curr, struct pt_regs *regs)
{
    struct timespec timeout, *timeoutp;
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    size_t len;
    int err;
    mqd_t q;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(q)))
	return -EFAULT;

    if (__xn_reg_arg5(regs) &&
	!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg5(regs),sizeof(timeout)))
	return -EFAULT;

    __xn_copy_from_user(curr,&q,(mqd_t *)__xn_reg_arg1(regs),sizeof(q));

    len = (size_t)__xn_reg_arg3(regs);
    prio = __xn_reg_arg4(regs);

    if (len > 0)
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),len))
	    return -EFAULT;

	if (len <= sizeof(tmp_buf))
	    tmp_area = tmp_buf;
	else
	    {
	    tmp_area = xnmalloc(len);

	    if (!tmp_area)
		return -ENOMEM;
	    }

	__xn_copy_from_user(curr,tmp_area,(void __user *)__xn_reg_arg2(regs),len);
	}
    else
	tmp_area = NULL;

    if (__xn_reg_arg5(regs))
	{
	__xn_copy_from_user(curr,
			    &timeout,
			    (struct timespec __user *)__xn_reg_arg5(regs),
			    sizeof(timeout));
	timeoutp = &timeout;
	}
    else
	timeoutp = NULL;

    err = mq_timedsend(q,tmp_area,len,prio,timeoutp);

    if (tmp_area && tmp_area != tmp_buf)
	xnfree(tmp_area);

    return err ? -thread_get_errno() : 0;
}

int __mq_receive (struct task_struct *curr, struct pt_regs *regs)
{
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    ssize_t len;
    mqd_t q;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(q)))
	return -EFAULT;

    __xn_copy_from_user(curr,&q,(mqd_t *)__xn_reg_arg1(regs),sizeof(q));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(len)))
	return -EFAULT;

    __xn_copy_from_user(curr,&len,(ssize_t *)__xn_reg_arg3(regs),sizeof(len));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(prio)))
	return -EFAULT;

    if (len > 0)
	{
	if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),len))
	    return -EFAULT;

	if (len <= sizeof(tmp_buf))
	    tmp_area = tmp_buf;
	else
	    {
	    tmp_area = xnmalloc(len);

	    if (!tmp_area)
		return -ENOMEM;
	    }
	}
    else
	tmp_area = NULL;

    len = mq_receive(q,tmp_area,len,&prio);

    if (len == -1)
	return -thread_get_errno();

    __xn_copy_to_user(curr,
		      (void __user *)__xn_reg_arg3(regs),
		      &len,
		      sizeof(len));

    if (len > 0)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg2(regs),
			  tmp_area,
			  len);

    if (tmp_area && tmp_area != tmp_buf)
	xnfree(tmp_area);

    return 0;
}

int __mq_timedreceive (struct task_struct *curr, struct pt_regs *regs)
{
    struct timespec timeout, *timeoutp;
    char tmp_buf[PSE51_MQ_FSTORE_LIMIT];
    caddr_t tmp_area;
    unsigned prio;
    ssize_t len;
    mqd_t q;

    if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg1(regs),sizeof(q)))
	return -EFAULT;

    __xn_copy_from_user(curr,&q,(mqd_t *)__xn_reg_arg1(regs),sizeof(q));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg3(regs),sizeof(len)))
	return -EFAULT;

    __xn_copy_from_user(curr,&len,(ssize_t *)__xn_reg_arg3(regs),sizeof(len));

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg4(regs),sizeof(prio)))
	return -EFAULT;

    if (__xn_reg_arg5(regs) &&
	!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg5(regs),sizeof(timeout)))
	return -EFAULT;

    if (len > 0)
	{
	if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg2(regs),len))
	    return -EFAULT;

	if (len <= sizeof(tmp_buf))
	    tmp_area = tmp_buf;
	else
	    {
	    tmp_area = xnmalloc(len);

	    if (!tmp_area)
		return -ENOMEM;
	    }
	}
    else
	tmp_area = NULL;

    if (__xn_reg_arg5(regs))
	{
	__xn_copy_from_user(curr,
			    &timeout,
			    (struct timespec __user *)__xn_reg_arg5(regs),
			    sizeof(timeout));
	timeoutp = &timeout;
	}
    else
	timeoutp = NULL;

    len = mq_timedreceive(q,tmp_area,len,&prio,timeoutp);

    if (len == -1)
	return -thread_get_errno();

    __xn_copy_to_user(curr,
		      (void __user *)__xn_reg_arg3(regs),
		      &len,
		      sizeof(len));

    if (len > 0)
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg2(regs),
			  tmp_area,
			  len);

    if (tmp_area && tmp_area != tmp_buf)
	xnfree(tmp_area);

    return 0;
}

static int __pse51_intr_handler (xnintr_t *cookie)

{
    struct pse51_interrupt *intr = PTHREAD_IDESC(cookie);

    ++intr->pending;

    if (xnsynch_nsleepers(&intr->synch_base) > 0)
	xnsynch_flush(&intr->synch_base,0);

    if (intr->mode & XN_ISR_CHAINED)
	return XN_ISR_CHAINED|(intr->mode & XN_ISR_ENABLE);

    return XN_ISR_HANDLED|(intr->mode & XN_ISR_ENABLE);
}

int __intr_attach (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr;
    unsigned long handle;
    int err, mode;
    unsigned irq;

    if (!__xn_access_ok(curr,VERIFY_WRITE,__xn_reg_arg1(regs),sizeof(handle)))
	return -EFAULT;

    /* Interrupt line number. */
    irq = (unsigned)__xn_reg_arg2(regs);

    /* Interrupt control mode. */
    mode = (int)__xn_reg_arg3(regs);

    if (mode & ~(XN_ISR_ENABLE|XN_ISR_CHAINED))
	return -EINVAL;

    intr = (struct pse51_interrupt *)xnmalloc(sizeof(*intr));

    if (!intr)
	return -ENOMEM;

    err = pse51_intr_attach(intr,irq,&__pse51_intr_handler,NULL);

    if (err == 0)
	{
	intr->mode = mode;
	handle = (unsigned long)intr;
	__xn_copy_to_user(curr,
			  (void __user *)__xn_reg_arg1(regs),
			  &handle,
			  sizeof(handle));
	}
    else
	xnfree(intr);

    return -err;
}

int __intr_detach (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr = (struct pse51_interrupt *)__xn_reg_arg1(regs);
    int err = pse51_intr_detach(intr);

    if (!err)
	xnfree(intr);

    return -err;
}

int __intr_wait (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr = (struct pse51_interrupt *)__xn_reg_arg1(regs);
    struct timespec ts;
    xnticks_t timeout;
    pthread_t thread;
    int err = 0;
    spl_t s;

    if (__xn_reg_arg2(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,__xn_reg_arg2(regs),sizeof(ts)))
	    return -EFAULT;

	__xn_copy_from_user(curr,
			    &ts,
			    (void __user *)__xn_reg_arg2(regs),
			    sizeof(ts));

	if (ts.tv_sec == 0 && ts.tv_nsec == 0)
	    return -EINVAL;

	timeout = ts2ticks_ceil(&ts)+1;
	}
    else
	timeout = XN_INFINITE;

    xnlock_get_irqsave(&nklock,s);

    if (!pse51_obj_active(intr, PSE51_INTR_MAGIC, struct pse51_interrupt))
	{
        xnlock_put_irqrestore(&nklock, s);
        return -EINVAL;
	}

    if (!intr->pending)
	{
	thread = pse51_current_thread();

	if (xnthread_base_priority(&thread->threadbase) != FUSION_IRQ_PRIO)
	    /* Renice the waiter above all regular threads if needed. */
	    xnpod_renice_thread(&thread->threadbase,FUSION_IRQ_PRIO);

	xnsynch_sleep_on(&intr->synch_base,timeout);
        
	if (xnthread_test_flags(&thread->threadbase,XNRMID))
	    err = -EIDRM; /* Interrupt object deleted while pending. */
	else if (xnthread_test_flags(&thread->threadbase,XNTIMEO))
	    err = -ETIMEDOUT; /* Timeout.*/
	else if (xnthread_test_flags(&thread->threadbase,XNBREAK))
	    err = -EINTR; /* Unblocked.*/
	else
	    err = intr->pending;
	}
    else
	err = intr->pending;

    intr->pending = 0;
    
    xnlock_put_irqrestore(&nklock,s);

    return err;
}

int __intr_control (struct task_struct *curr, struct pt_regs *regs)
{
    struct pse51_interrupt *intr = (struct pse51_interrupt *)__xn_reg_arg1(regs);
    int cmd = (int)__xn_reg_arg2(regs);

    return pse51_intr_control(intr,cmd);
}

int __timer_create (struct task_struct *curr, struct pt_regs *regs)
{
    return 0;
}

int __timer_delete (struct task_struct *curr, struct pt_regs *regs)
{
    return 0;
}

int __timer_settime (struct task_struct *curr, struct pt_regs *regs)
{
    return 0;
}

int __timer_gettime (struct task_struct *curr, struct pt_regs *regs)
{
    return 0;
}

int __timer_getoverrun (struct task_struct *curr, struct pt_regs *regs)
{
    return 0;
}

#if 0
int __itimer_set (struct task_struct *curr, struct pt_regs *regs)
{
    pthread_t thread = pse51_current_thread();
    xnticks_t delay, interval;
    struct itimerval itv;

    if (__xn_reg_arg1(regs))
	{
	if (!__xn_access_ok(curr,VERIFY_READ,(void *)__xn_reg_arg1(regs),sizeof(itv)))
	    return -EFAULT;

	__xn_copy_from_user(curr,&itv,(void *)__xn_reg_arg1(regs),sizeof(itv));
	}
    else
	memset(&itv,0,sizeof(itv));

    if (__xn_reg_arg2(regs) &&
	!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg2(regs),sizeof(itv)))
	return -EFAULT;

    xntimer_stop(&thread->itimer);

    delay = xnshadow_tv2ticks(&itv.it_value);
    interval = xnshadow_tv2ticks(&itv.it_interval);

    if (delay > 0)
	xntimer_start(&thread->itimer,delay,interval);

    if (__xn_reg_arg2(regs))
	{
	interval = xntimer_interval(&thread->itimer);

	if (xntimer_running_p(&thread->itimer))
	    {
	    delay = xntimer_get_timeout(&thread->itimer);
	    
	    if (delay == 0)
		delay = 1;
	    }
	else
	    delay = 0;

	xnshadow_ticks2tv(delay,&itv.it_value);
	xnshadow_ticks2tv(interval,&itv.it_interval);
	__xn_copy_to_user(curr,(void *)__xn_reg_arg2(regs),&itv,sizeof(itv));
	}

    return 0;
}

int __itimer_get (struct task_struct *curr, struct pt_regs *regs)
{
    pthread_t thread = pse51_current_thread();
    xnticks_t delay, interval;
    struct itimerval itv;

    if (!__xn_access_ok(curr,VERIFY_WRITE,(void *)__xn_reg_arg1(regs),sizeof(itv)))
	return -EFAULT;

    interval = xntimer_interval(&thread->itimer);

    if (xntimer_running_p(&thread->itimer))
	{
	delay = xntimer_get_timeout(&thread->itimer);
	
	if (delay == 0) /* Cannot be negative in this context. */
	    delay = 1;
	}
    else
	delay = 0;

    xnshadow_ticks2tv(delay,&itv.it_value);
    xnshadow_ticks2tv(interval,&itv.it_interval);
    __xn_copy_to_user(curr,(void *)__xn_reg_arg1(regs),&itv,sizeof(itv));

    return 0;
}
#endif

static xnsysent_t __systab[] = {
    [__pse51_thread_create ] = { &__pthread_create, __xn_exec_init },
    [__pse51_thread_detach ] = { &__pthread_detach, __xn_exec_any },
    [__pse51_thread_setschedparam ] = { &__pthread_setschedparam, __xn_exec_any },
    [__pse51_sched_yield ] = { &__sched_yield, __xn_exec_primary },
    [__pse51_thread_make_periodic ] = { &__pthread_make_periodic_np, __xn_exec_primary },
    [__pse51_thread_wait] = { &__pthread_wait_np, __xn_exec_primary },
    [__pse51_thread_set_mode] = { &__pthread_set_mode_np, __xn_exec_primary },
    [__pse51_thread_set_name] = { &__pthread_set_name_np, __xn_exec_any },
    [__pse51_sem_init] = { &__sem_init, __xn_exec_any },
    [__pse51_sem_destroy] = { &__sem_destroy, __xn_exec_any },
    [__pse51_sem_post] = { &__sem_post, __xn_exec_any },
    [__pse51_sem_wait] = { &__sem_wait, __xn_exec_primary },
    [__pse51_sem_trywait] = { &__sem_trywait, __xn_exec_primary },
    [__pse51_sem_getvalue] = { &__sem_getvalue, __xn_exec_primary },
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
    [__pse51_mq_open] = { &__mq_open, __xn_exec_any },
    [__pse51_mq_close] = { &__mq_close, __xn_exec_any },
    [__pse51_mq_unlink] = { &__mq_unlink, __xn_exec_any },
    [__pse51_mq_getattr] = { &__mq_getattr, __xn_exec_any },
    [__pse51_mq_setattr] = { &__mq_setattr, __xn_exec_any },
    [__pse51_mq_send] = { &__mq_send, __xn_exec_primary },
    [__pse51_mq_timedsend] = { &__mq_timedsend, __xn_exec_primary },
    [__pse51_mq_receive] = { &__mq_receive, __xn_exec_primary },
    [__pse51_mq_timedreceive] = { &__mq_timedreceive, __xn_exec_primary },
    [__pse51_intr_attach] = { &__intr_attach, __xn_exec_any },
    [__pse51_intr_detach] = { &__intr_detach, __xn_exec_any },
    [__pse51_intr_wait] = { &__intr_wait, __xn_exec_primary },
    [__pse51_intr_control] = { &__intr_control, __xn_exec_any },
    [__pse51_timer_create] = { &__timer_create, __xn_exec_any },
    [__pse51_timer_delete] = { &__timer_delete, __xn_exec_any },
    [__pse51_timer_settime] = { &__timer_settime, __xn_exec_any },
    [__pse51_timer_gettime] = { &__timer_gettime, __xn_exec_any },
    [__pse51_timer_getoverrun] = { &__timer_getoverrun, __xn_exec_any },
};

static void __shadow_delete_hook (xnthread_t *thread)

{
    if (xnthread_get_magic(thread) == PSE51_SKIN_MAGIC &&
	testbits(thread->status,XNSHADOW))
	{
	pthread_t k_tid = thread2pthread(thread);
	__pthread_unhash(&k_tid->hkey);
	xnshadow_unmap(thread);
	}
}

int pse51_syscall_init (void)

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

    __pse51_errptd = rthal_alloc_ptdkey();
    
    return 0;
}

void pse51_syscall_cleanup (void)

{
    xnpod_remove_hook(XNHOOK_THREAD_DELETE,&__shadow_delete_hook);
    xnshadow_unregister_interface(__muxid);
    rthal_free_ptdkey(__pse51_errptd);
}
