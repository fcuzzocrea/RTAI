/*!\file mutex.c
 * \brief Nanokernel mutex services.
 * \author Philippe Gerum
 *
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
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
 *
 * \ingroup mutex
 */

/*!
 * \ingroup xenomai
 * \defgroup mutex Nanokernel mutex services.
 *
 * Nanokernel mutex services.
 *
 ********************************************************************
 * [WARNING] Mutex services are deprecated in newer versions of the
 * Xenomai nucleus, basically due to design and performance issues. Do
 * not use this facility if you plan to port to RTAI/fusion.
 ********************************************************************
 *
 * Mutexes are internal synchronization objects that do NOT rely on
 * the standard nanokernel scheduling routines to operate.  However,
 * they share the same protocol inheritance scheme with the regular
 * xnsynch objects for the sake of clarity and efficiency. In other
 * words, code in mutex.c and synch.c cooperate on the same logic and
 * data structures for handling PIP both for kernel mutexes and
 * regular resources.
 *
 * One should keep in mind that a thread claiming a mutex is NOT
 * actually suspended, but the current mutex owner is simply
 * rescheduled before the "sleeper" instead. Priority inheritance
 * allows to boost the current mutex owner so that it can release the
 * lock as soon as possible.
 *
 * Consequently, blocking or suspending a thread that holds a mutex is
 * a BUG! However, the nanokernel provides for a special mechanism
 * that releases a given interface mutex atomically when
 * xnpod_suspend_thread() is called for the current thread,
 * reacquiring it on return of this routine.  This mechanism should be
 * understood as a lock-breaking preemption point atomically performed
 * just before the current thread is switched out.
 * @see xnpod_schedule() for more on this.
 *
 *@{*/

#include <stdarg.h>
#include "rtai_config.h"
#include "xenomai/pod.h"
#include "xenomai/mutex.h"
#include "xenomai/module.h"

/*
 * Mutexes are internal synchronization objects that do NOT rely on
 * the standard nanokernel scheduling routines to operate.  However,
 * they share the same protocol inheritance scheme with the regular
 * xnsynch objects for the sake of clarity and efficiency. In other
 * words, code in mutex.c and synch.c cooperate on the same logic and
 * data structures for handling PIP both for kernel mutexes and
 * regular resources.
 *
 * One should keep in mind that a thread claiming a mutex is NOT
 * actually suspended, but the current mutex owner is simply
 * rescheduled before the "sleeper" instead. Priority inheritance
 * allows to boost the current mutex owner so that it can release the
 * lock as soon as possible.
 *
 * Consequently, blocking or suspending a thread that holds a mutex is
 * a BUG! However, the nanokernel provides for a special mechanism
 * that releases a given interface mutex atomically when
 * xnpod_suspend_thread() is called for the current thread,
 * reacquiring it on return of this routine.  This mechanism should be
 * understood as a lock-breaking preemption point atomically performed
 * just before the current thread is switched out. See
 * xnpod_schedule() for more on this.
 */

void xnmutex_init (xnmutex_t *mutex)

{
    xnsynch_init(&mutex->synchbase,XNSYNCH_PRIO|XNSYNCH_PIP);
    xnsynch_set_flags(&mutex->synchbase,XNSYNCH_KMUTEX);
    xnarch_atomic_set(&mutex->lockcnt,1);
}

void xnmutex_sleepon_inner (xnmutex_t *mutex, xnthread_t *sleeper)

{
    xnthread_t *owner = xnsynch_owner(&mutex->synchbase);

    if (xnsynch_nsleepers(&mutex->synchbase) > 0)
	/* lockcnt was negative and decremented on entry -- Ensure
	   that all sleepers count for a single decrementation. */
	xnarch_atomic_inc(&mutex->lockcnt);

#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    if (!testbits(owner->status,XNREADY))
	xnpod_fatal("owner (name=%s, status=0x%lx) of mutex %p is not runnable -- cannot boost",
		    owner->name,
		    owner->status,
		    mutex);

    if (xnpod_priocompare(sleeper->cprio,owner->cprio) < 0)
	xnpod_fatal("badly ordered readyq?!");
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */

    insertpqf(xnsynch_wait_queue(&mutex->synchbase),
	      &sleeper->plink,
	      sleeper->cprio);

    setbits(sleeper->status,XNWMUTEX);

    /* Now we have to fiddle with the priority inheritance, taking in
       account that the mutex owner might already undergo a priority
       boost from a synchronization object it owns. In any cases, the
       main idea is: "always raise the mutex owner priority, don't
       lower it until it releases the mutex, ever." */

    if (xnpod_priocompare(sleeper->cprio,owner->cprio) > 0)
	{
	/* Raise the owner's priority to solve the current priority
	   inversion. Basically, this is a simplified version of
	   xnsynch_sleep_on(). */

	if (!testbits(owner->status,XNBOOST))
	    {
	    owner->bprio = owner->cprio;
	    setbits(owner->status,XNBOOST);
	    }

	if (!xnsynch_test_flags(&mutex->synchbase,XNSYNCH_CLAIMED))
	    {
	    insertpqf(&owner->claimq,
		      &mutex->synchbase.link,
		      getheadpq(xnsynch_wait_queue(&mutex->synchbase))->prio);
	    xnsynch_set_flags(&mutex->synchbase,XNSYNCH_CLAIMED);
	    }

	owner->cprio = sleeper->cprio;
	}

    xnpod_schedule_runnable(owner,XNPOD_SCHEDLIFO);
}

void xnmutex_wakeup_inner (xnmutex_t *mutex, int flags)

{
    xnthread_t *owner = xnsynch_owner(&mutex->synchbase), *sleeper;
    spl_t s;

    splhigh(s);

    /* Pick the highest priority sleeper. */
    sleeper = link2thread(getpq(xnsynch_wait_queue(&mutex->synchbase)),plink);
    clrbits(sleeper->status,XNWMUTEX);

    if (xnsynch_test_flags(&mutex->synchbase,XNSYNCH_CLAIMED))
	{
	removepq(&owner->claimq,&mutex->synchbase.link);
	xnsynch_clear_flags(&mutex->synchbase,XNSYNCH_CLAIMED);
	}

    if (xnsynch_nsleepers(&mutex->synchbase) > 0)
	/* lockcnt was zero on entry -- Ensure that the next unlock
	   will beget a wakeup if sleepers remain. */
	xnarch_atomic_dec(&mutex->lockcnt);

    if (testbits(owner->status,XNBOOST))
	{
	int downprio = owner->bprio;

	if (countpq(&owner->claimq) == 0)
	    clrbits(owner->status,XNBOOST);
	else
	    {
	    /* Find the highest priority needed to enforce the PIP. */
	    int rprio = getheadpq(&owner->claimq)->prio;

	    if (xnpod_priocompare(rprio,downprio) > 0)
		downprio = rprio;
	    }

	owner->cprio = downprio;

	if (owner->wchan)
	    /* Ignoring the XNDREORD flag on purpose here. */
	    xnsynch_renice_sleeper(owner);
	}

#ifdef CONFIG_RTAI_XENOMAI_DEBUG
    if (sleeper->sched->runthread != owner)
	xnpod_fatal("mutex %p not released by owner\n",mutex);
#endif /* CONFIG_RTAI_XENOMAI_DEBUG */

    xnsynch_set_owner(&mutex->synchbase,sleeper);

    xnpod_schedule_runnable(sleeper,XNPOD_SCHEDLIFO|flags);

    splexit(s);
}

/*@{*/

EXPORT_SYMBOL(xnmutex_init);
EXPORT_SYMBOL(xnmutex_sleepon_inner);
EXPORT_SYMBOL(xnmutex_wakeup_inner);
