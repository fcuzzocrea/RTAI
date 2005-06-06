/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <malloc.h>
#include <nucleus/asm/atomic.h>
#include <posix/syscall.h>
#include <posix/lib/jhash.h>
#include <posix/lib/pthread.h>
#include <posix/lib/semaphore.h>

extern int __pse51_muxid;

struct pthread_jhash {

#define PTHREAD_HASHBITS 8

    pthread_t tid;
    unsigned long internal_tid;
    struct pthread_jhash *next;
};

struct pthread_iargs {
    void *(*start)(void *);
    void *arg;
    int prio;
    sem_t sync;
    int ret;
};

static volatile unsigned long __jhash_lock; /* Guaranteed zero */

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
   have this constraint, since we plan to fully override their
   respective interfaces with RTAI/fusion-based replacements. */

#define __jhash_write_lock(lock) \
do { \
    while (atomic_cmpxchg(lock,0,(typeof(*lock))-1) != 0) \
     cpu_relax(); \
} while(0)

#define __jhash_write_unlock(lock) \
do { \
    atomic_xchg(lock,0); \
} while(0)

#define __jhash_read_lock(lock) \
for (;;) { \
     unsigned long lval = *lock; \
     if ((long)lval >= 0 && atomic_cmpxchg(lock,lval,(lval + 1)) == lval) \
         break; \
     cpu_relax(); \
}

#define __jhash_read_unlock(lock) \
for (;;) { \
     unsigned long lval = *lock; \
     if (atomic_cmpxchg(lock,lval,(lval - 1)) == lval) \
         break; \
     cpu_relax(); \
}

static void __pthread_hash (pthread_t tid,
			    unsigned long internal_tid,
			    struct pthread_jhash *slot)
{
    struct pthread_jhash **bucketp;
    uint32_t hash;

    hash = jhash2((uint32_t *)&tid,
		  sizeof(tid)/sizeof(uint32_t),
		  0);

    bucketp = &__jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];
    slot->internal_tid = internal_tid;
    slot->tid = tid;

    __jhash_write_lock(&__jhash_lock);
    slot->next = *bucketp;
    *bucketp = slot;
    __jhash_write_unlock(&__jhash_lock);
}

static void __pthread_unhash (pthread_t tid,
			      unsigned long internal_tid)
{
    struct pthread_jhash **tail, *slot;
    uint32_t hash;

    hash = jhash2((uint32_t *)&tid,
		  sizeof(tid)/sizeof(uint32_t),
		  0);

    __jhash_write_lock(&__jhash_lock);

    tail = &__jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];
    slot = *tail;

    while (slot != NULL && slot->tid != tid)
	{
	tail = &slot->next;
    	slot = *tail;
	}

    if (slot)
	*tail = slot->next;

    __jhash_write_unlock(&__jhash_lock);

    if (slot)
	free(slot);
}

static unsigned long __pthread_find (pthread_t tid)

{
    struct pthread_jhash *slot;
    unsigned long internal_tid;
    uint32_t hash;

    hash = jhash2((uint32_t *)&tid,
		  sizeof(tid)/sizeof(uint32_t),
		  0);

    __jhash_read_lock(&__jhash_lock);

    slot = __jhash_buckets[hash&((1<<PTHREAD_HASHBITS)-1)];

    while (slot != NULL && slot->tid != tid)
    	slot = slot->next;

    internal_tid = slot ? slot->internal_tid : 0;

    __jhash_read_unlock(&__jhash_lock);

    return internal_tid;
}

static void __pthread_cleanup_handler (void *arg)

{
    struct pthread_jhash *slot = (struct pthread_jhash *)arg;
    __pthread_unhash(slot->tid,slot->internal_tid);
}

static void *__pthread_trampoline (void *arg)

{
    struct pthread_iargs *iargs = (struct pthread_iargs *)arg;
    unsigned long internal_tid;
    struct pthread_jhash *slot;
    struct sched_param param;
    void *status = NULL;
    long err;

    /* Broken pthread libs ignore some of the thread attribute specs
       passed to pthread_create(3), so we force the scheduling policy
       once again here. */
    param.sched_priority = iargs->prio;
    __real_pthread_setschedparam(pthread_self(),SCHED_FIFO,&param);

    /* At this early point of the thread initialization process, we
       are already running in secondary mode, so using malloc() to get
       memory for the hash slot does not add significant overhead. The
       same goes for freeing it at exit, since we will need to switch
       to secondary mode for performing the Linux task exit anyhow. */

    slot = (struct pthread_jhash *)malloc(sizeof(*slot));

    if (!slot)
	{
	iargs->ret = ENOMEM;
	__real_sem_post(&iargs->sync);
	pthread_exit((void *)ENOMEM);
	}

    err = XENOMAI_SKINCALL1(__pse51_muxid,
			    __pse51_thread_create,
			    &internal_tid);
    iargs->ret = -err;
    __real_sem_post(&iargs->sync);

    if (!err)
	{
	pthread_cleanup_push(&__pthread_cleanup_handler,slot);
	__pthread_hash(pthread_self(),internal_tid,slot);
	status = iargs->start(iargs->arg);
	pthread_cleanup_pop(1);
	}
    else
	{
	free(slot);
	status = (void *)-err;
	}

    pthread_exit(status);
}

int __wrap_pthread_create (pthread_t *tid,
			   const pthread_attr_t *attr,
			   void *(*start) (void *),
			   void *arg)
{
    struct pthread_iargs iargs;
    int inherit, policy, err;
    struct sched_param param;

    /* Run the vanilla pthread_create(3) service whenever SCHED_FIFO
       is not the new thread's policy. */

    if (!attr ||
	(!pthread_attr_getinheritsched(attr,&inherit) &&
	 ((inherit == PTHREAD_EXPLICIT_SCHED &&
	   !pthread_attr_getschedpolicy(attr,&policy) &&
	   !pthread_attr_getschedparam(attr,&param) &&
	   policy != SCHED_FIFO) ||
	  (inherit == PTHREAD_INHERIT_SCHED &&
	   !pthread_getschedparam(pthread_self(),&policy,&param) &&
	   policy != SCHED_FIFO))))
	return __real_pthread_create(tid,attr,start,arg);

    /* Ok, we are about to create a new real-time thread. First start
       a native POSIX thread, then associate a RTAI/fusion shadow to
       it. */

    iargs.start = start;
    iargs.arg = arg;
    iargs.prio = param.sched_priority;
    iargs.ret = EAGAIN;
    __real_sem_init(&iargs.sync,0,0);

    err = __real_pthread_create(tid,attr,&__pthread_trampoline,&iargs);
    __real_sem_wait(&iargs.sync);
    __real_sem_destroy(&iargs.sync);

    return err ?: iargs.ret;
}

int __wrap_pthread_detach (pthread_t thread)

{
    unsigned long internal_tid = __pthread_find(thread);

    if (!internal_tid)
	__real_pthread_detach(thread);
    
    return -XENOMAI_SKINCALL1(__pse51_muxid,
			      __pse51_thread_detach,
			      internal_tid);
}

int __wrap_pthread_setschedparam (pthread_t thread,
				  int policy,
				  const struct sched_param *param)
{
    unsigned long internal_tid = __pthread_find(thread);

    if (!internal_tid)
	return __real_pthread_setschedparam(thread,policy,param);

    return -XENOMAI_SKINCALL3(__pse51_muxid,
			      __pse51_thread_setschedparam,
			      internal_tid,
			      policy,
			      param);
}

int __wrap_sched_yield (void)

{
    return -XENOMAI_SKINCALL0(__pse51_muxid,
			      __pse51_sched_yield);
}

int __wrap_pthread_yield (void)

{
    __wrap_sched_yield();
    return 0;
}

int pthread_make_periodic_np (pthread_t thread,
			      struct timespec *starttp,
			      struct timespec *periodtp)
{
    unsigned long internal_tid = __pthread_find(thread);

    if (!internal_tid)
	return EPERM;
    
    return -XENOMAI_SKINCALL3(__pse51_muxid,
			      __pse51_thread_make_periodic,
			      internal_tid,
			      starttp,
			      periodtp);
}

int pthread_wait_np (void)

{
    return -XENOMAI_SKINCALL0(__pse51_muxid,
			      __pse51_thread_wait);
}
