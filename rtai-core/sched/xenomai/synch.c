/*!\file synch.c
 * \brief Thread synchronization services.
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
 * \ingroup synch
 */

/*!
 * \ingroup xenomai
 * \defgroup synch Thread synchronization services.
 *
 * Thread synchronization services.
 *
 *@{*/

#include <stdarg.h>
#include "rtai_config.h"
#include "xenomai/pod.h"
#include "xenomai/mutex.h"
#include "xenomai/synch.h"
#include "xenomai/thread.h"
#include "xenomai/module.h"

/*! 
 * \fn void xnsynch_init(xnsynch_t *synch, xnflags_t flags);
 * \brief Initialize a synchronization object.
 *
 * Initializes a new specialized object which can subsequently be
 * used to synchronize real-time activities. Xenomai provides a basic
 * synchronization object which can be used to build higher resource
 * objects. Xenomai threads can wait for and signal such objects in
 * order to synchronize their activities.
 *
 * This object has built-in support for priority inheritance.
 *
 * @param synch The address of a synchronization object descriptor
 * Xenomai will use to store the object-specific data.  This
 * descriptor must always be valid while the object is active
 * therefore it must be allocated in permanent memory.
 *
 * @param flags A set of creation flags affecting the operation. The
 * valid flags are:
 *
 * - XNSYNCH_PRIO causes the threads waiting for the resource to pend
 * in priority order. Otherwise, FIFO ordering is used (XNSYNCH_FIFO).
 *
 * - XNSYNCH_PIP causes the priority inheritance mechanism to be
 * automatically activated when a priority inversion is detected among
 * threads using this object. Otherwise, no priority inheritance takes
 * place upon priority inversion (XNSYNCH_NOPIP).
 *
 * Side-effect: This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

void xnsynch_init (xnsynch_t *synch,
		   xnflags_t flags)
{
    initph(&synch->link);

    if (flags & XNSYNCH_PIP)
	flags |= XNSYNCH_PRIO;	/* Obviously... */

    synch->status = flags;
    synch->owner = NULL;
    initpq(&synch->pendq,xnpod_get_qdir(nkpod));
    xnarch_init_display_context(synch);
}

/*
 * xnsynch_renice_thread() -- This service is used by the PIP code to
 * raise/lower a thread's priority. The thread's base priority value
 * is _not_ changed and if ready, the thread is always moved at the
 * end of its priority group.
 */

static inline void xnsynch_renice_thread (xnthread_t *thread, int prio)

{
    thread->cprio = prio;

    if (thread->wchan)
	/* Ignoring the XNDREORD flag on purpose here. */
	xnsynch_renice_sleeper(thread);
    else if (thread != xnpod_current_thread() &&
	     testbits(thread->status,XNREADY))
	/* xnpod_resume_thread() must be called for runnable threads
	   but the running one. */
	xnpod_resume_thread(thread,0);
    else /* In case of a shadow, we do not attempt to renice the
	    mated Linux task if we did not renice the former. */
	return;

#ifdef __KERNEL__
    if (testbits(thread->status,XNSHADOW))
	xnshadow_renice(thread);
#endif /* __KERNEL__ */
}

/*! 
 * \fn void xnsynch_sleep_on(xnsynch_t *synch,
                             xnticks_t timeout,
  			        xnmutex_t *imutex);
 * \brief Sleep on a synchronization object.
 *
 * Makes the calling thread sleep on the specified synchronization
 * object, waiting for it to be signaled.
 *
 * This service should be called by upper interfaces wanting the
 * current thread to pend on the given resource.
 *
 * @param synch The descriptor address of the synchronization object
 * to sleep on.
 *
 * @param timeout The timeout which may be used to limit the time the
 * thread pends on the resource. This value is a count of ticks.
 * Passing XN_INFINITE specifies an unbounded wait. All other values
 * are used to initialize a nanokernel watchdog timer.
 *
 * @param imutex The address of an interface mutex currently held by
 * the caller which will be subject to a lock-breaking preemption
 * before the current thread is actually switched out. The
 * corresponding kernel mutex will be automatically reacquired by the
 * nanokernel when the suspended thread is eventually resumed, before
 * xnsynch_sleep_on() returns to its caller.  Passing NULL when no
 * lock breaking is required is valid. See xnpod_schedule() for more
 * on lock-breaking preemption.
 *
 * Side-effect: This routine always calls the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread context.
 */

void xnsynch_sleep_on (xnsynch_t *synch,
		       xnticks_t timeout,
		       xnmutex_t *imutex)
{
    xnthread_t *thread = xnpod_current_thread();
    spl_t s;

    splhigh(s);

    if (testbits(synch->status,XNSYNCH_PRIO))
	{
	xnthread_t *owner = synch->owner;

	insertpqf(&synch->pendq,&thread->plink,thread->cprio);

	if (testbits(synch->status,XNSYNCH_PIP) &&
	    owner != NULL &&
	    xnpod_priocompare(thread->cprio,owner->cprio) > 0)
	    {
	    if (!testbits(owner->status,XNBOOST))
		{
		owner->bprio = owner->cprio;
		setbits(owner->status,XNBOOST);
		}

	    if (!testbits(synch->status,XNSYNCH_CLAIMED))
		{
		insertpqf(&owner->claimq,
			  &synch->link,
			  getheadpq(&synch->pendq)->prio);
		setbits(synch->status,XNSYNCH_CLAIMED);
		}

	    xnsynch_renice_thread(owner,thread->cprio);
	    }
	}
    else /* otherwise FIFO */
	appendpq(&synch->pendq,&thread->plink);

    xnpod_suspend_thread(thread,XNPEND,timeout,synch,imutex);

    splexit(s);
}

/*! 
 * \fn void xnsynch_clear_boost(xnsynch_t *synch, xnthread_t *owner);
 * \brief Clear the priority boost - INTERNAL.
 *
 * This service is called internally whenever a synchronization object
 * is not claimed anymore by sleepers to reset the object owner's
 * priority to its initial level.
 *
 * @param synch The descriptor address of the synchronization object.
 *
 * @param owner The descriptor address of the thread which
 * currently owns the synchronization object.
 */

static void xnsynch_clear_boost (xnsynch_t *synch,
				 xnthread_t *lastowner)
{
    int downprio;

    removepq(&lastowner->claimq,&synch->link);
    clrbits(synch->status,XNSYNCH_CLAIMED);
    downprio = lastowner->bprio;

    if (countpq(&lastowner->claimq) == 0)
	clrbits(lastowner->status,XNBOOST);
    else
	{
	/* Find the highest priority needed to enforce the PIP. */
	int rprio = getheadpq(&lastowner->claimq)->prio;

	if (xnpod_priocompare(rprio,downprio) > 0)
	    downprio = rprio;
	}

    if (lastowner->cprio != downprio)
	xnsynch_renice_thread(lastowner,downprio);
}

/*! 
 * \fn void xnsynch_renice_sleeper(xnthread_t *thread);
 * \brief Change a sleeper's priority - INTERNAL.
 *
 * This service is used by the PIP code to update the pending priority
 * of a sleeping thread.
 *
 * @param thread The descriptor address of the affected thread.
 */

void xnsynch_renice_sleeper (xnthread_t *thread)

{
    xnsynch_t *synch = thread->wchan;

    if (testbits(synch->status,XNSYNCH_PRIO))
	{
	xnthread_t *owner = synch->owner;

	removepq(&synch->pendq,&thread->plink);
	insertpqf(&synch->pendq,&thread->plink,thread->cprio);

	if (testbits(synch->status,XNSYNCH_CLAIMED) &&
	    xnpod_priocompare(thread->cprio,owner->cprio) > 0)
	    {
	    removepq(&owner->claimq,&synch->link);

	    insertpqf(&owner->claimq,
		      &synch->link,
		      thread->cprio);

	    xnsynch_renice_thread(owner,thread->cprio);
	    }
	}
}

/*! 
 * \fn void xnsynch_wakeup_one_sleeper(xnsynch_t *synch);
 * \brief Give the resource ownership to the next waiting thread.
 *
 * This service gives the ownership of a synchronization object to the
 * thread which is currently leading the object's pending list. The
 * sleeping thread is unblocked, but no action is taken regarding the
 * previous owner of the resource.
 *
 * This service should be called by upper interfaces wanting to signal
 * the given resource so that a single waiter is resumed.
 *
 * @param synch The descriptor address of the synchronization object
 * whose ownership is changed.
 *
 * @return The descriptor address of the unblocked thread.
 *
 * Side-effects:
 *
 * - The effective priority of the previous resource owner might be
 * lowered to its base priority value as a consequence of the priority
 * inheritance boost being cleared.
 *
 * - The synchronization object ownership is transfered to the
 * unblocked thread.
 *
 * - This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread or IST
 * context.
 */

xnthread_t *xnsynch_wakeup_one_sleeper (xnsynch_t *synch)

{
    xnthread_t *thread = NULL, *lastowner = synch->owner;
    xnpholder_t *holder;
    spl_t s;

    splhigh(s);

    holder = getpq(xnsynch_wait_queue(synch));

    if (holder)
	{
	thread = link2thread(holder,plink);
	thread->wchan = NULL;
	synch->owner = thread;
	xnpod_resume_thread(thread,XNPEND);
	}
    else
	synch->owner = NULL;

    if (testbits(synch->status,XNSYNCH_CLAIMED))
	xnsynch_clear_boost(synch,lastowner);

    splexit(s);

    xnarch_post_graph_if(synch,0,countpq(&synch->pendq) == 0);

    return thread;
}

/*! 
 * \fn void xnsynch_wakeup_this_sleeper(xnsynch_t *synch, xnpholder_t *holder);
 * \brief Give the resource ownership to a given waiting thread.
 *
 * This service gives the ownership of a given synchronization object
 * to a specific thread which is currently pending on it. The sleeping
 * thread is unblocked from its pending state. No action is taken
 * regarding the previous resource owner.
 *
 * This service should be called by upper interfaces wanting to signal
 * the given resource so that a specific waiter is resumed.
 *
 * @param synch The descriptor address of the synchronization object
 * whose ownership is changed.
 *
 * @param holder The link holder address of the thread to unblock
 * (&thread->plink) which MUST currently be linked to the
 * synchronization object's pending queue (i.e. synch->pendq).
 *
 * @return The link address of the unblocked thread in the
 * synchronization object's pending queue.
 *
 * Side-effects:
 *
 * - The effective priority of the previous resource owner might be
 * lowered to its base priority value as a consequence of the priority
 * inheritance boost being cleared.
 *
 * - The synchronization object ownership is transfered to the
 * unblocked thread.
 *
 * - This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread or IST
 * context.
 */

xnpholder_t *xnsynch_wakeup_this_sleeper (xnsynch_t *synch,
					  xnpholder_t *holder)
{
    xnthread_t *thread, *lastowner = synch->owner;
    xnpholder_t *nholder;
    spl_t s;

    splhigh(s);

    nholder = poppq(xnsynch_wait_queue(synch),holder);
    thread = link2thread(holder,plink);
    thread->wchan = NULL;
    synch->owner = thread;
    xnpod_resume_thread(thread,XNPEND);

    if (testbits(synch->status,XNSYNCH_CLAIMED))
	xnsynch_clear_boost(synch,lastowner);

    splexit(s);

    xnarch_post_graph_if(synch,0,countpq(&synch->pendq) == 0);

    return nholder;
}

/*! 
 * \fn void xnsynch_flush(xnsynch_t *synch, xnflags_t reason);
 * \brief Unblock all waiters pending on a resource.
 *
 * This service atomically releases all threads which currently sleep
 * on a given resource.
 *
 * This service should be called by upper interfaces under
 * circumstances requiring that the pending queue of a given resource
 * is cleared, such as before the resource is deleted.
 *
 * @param synch The descriptor address of the synchronization object
 * whose ownership is changed.
 *
 * @param reason Some flags to set in the status mask of every
 * unblocked thread. The following bits are pre-defined by the
 * nanokernel:
 *
 * - XNRMID should be set to indicate that the synchronization object
 * is about to be destroyed (see xnpod_resume_thread()).
 *
 * - XNBREAK should be set to indicate that the wait has been forcibly
 * interrupted (see xnpod_unblock_thread()).
 *
 * @return XNSYNCH_RESCHED is returned if at least one thread
 * is unblocked, which means the caller should invoke xnpod_schedule()
 * for applying the new scheduling state. Otherwise, XNSYNCH_DONE is
 * returned.
 *
 * Side-effects:
 *
 * - The effective priority of the previous resource owner might be
 * lowered to its base priority value as a consequence of the priority
 * inheritance boost being cleared.
 *
 * - The synchronization object is no more owned by any thread.
 *
 * - This routine does not call the rescheduling procedure.
 *
 * Context: This routine must be called on behalf of a thread or IST
 * context.
 */

int xnsynch_flush (xnsynch_t *synch, xnflags_t reason)

{
    xnpholder_t *holder;
    int status;
    spl_t s;

    splhigh(s);

    status = countpq(&synch->pendq) > 0 ? XNSYNCH_RESCHED : XNSYNCH_DONE;

    while ((holder = getpq(&synch->pendq)) != NULL)
	{
	xnthread_t *sleeper = link2thread(holder,plink);
	setbits(sleeper->status,reason);
	sleeper->wchan = NULL;
	xnpod_resume_thread(sleeper,XNPEND);
	}

    if (testbits(synch->status,XNSYNCH_CLAIMED))
	{
	xnsynch_clear_boost(synch,synch->owner);
	status = XNSYNCH_RESCHED;
	}

    synch->owner = NULL;

    splexit(s);

    xnarch_post_graph_if(synch,0,countpq(&synch->pendq) == 0);

    return status;
}

/*! 
 * \fn void xnsynch_forget_sleeper(xnthread_t *thread);
 * \brief Abort a wait for a resource - INTERNAL.
 *
 * Performs all the necessary housekeeping chores to stop a thread
 * from waiting on a given synchronization object.
 *
 * @param thread The descriptor address of the affected thread.
 *
 * When the trace support is enabled (i.e. MVM), the idle state is
 * posted to the synchronization object's state diagram (if any)
 * whenever no thread remains blocked on it. The real-time interfaces
 * must ensure that such condition (i.e. EMPTY/IDLE) is mapped to
 * state #0.
 */

void xnsynch_forget_sleeper (xnthread_t *thread) /* INTERNAL */

{
    xnsynch_t *synch = thread->wchan;

    clrbits(thread->status,XNPEND);
    thread->wchan = NULL;
    removepq(&synch->pendq,&thread->plink);

    if (testbits(synch->status,XNSYNCH_CLAIMED))
	{
	/* Find the highest priority needed to enforce the PIP. */
	xnthread_t *owner = synch->owner;
	int rprio;

	if (countpq(&synch->pendq) == 0)
	    /* No more sleepers: clear the boost. */
	    xnsynch_clear_boost(synch,owner);
	else if (getheadpq(&synch->pendq)->prio !=
		getheadpq(&owner->claimq)->prio)
		{
		/* Reorder the claim queue, and lower the priority to the
		   required minimum needed to prevent priority
		   inversion. */
		removepq(&owner->claimq,&synch->link);

		insertpqf(&owner->claimq,
			  &synch->link,
			  getheadpq(&synch->pendq)->prio);

		rprio = getheadpq(&owner->claimq)->prio;

		if (xnpod_priocompare(rprio,owner->cprio) < 0)
		    xnsynch_renice_thread(owner,rprio);
		}
	}

    xnarch_post_graph_if(synch,0,countpq(&synch->pendq) == 0);
}

/*! 
 * \fn void xnsynch_release_all_ownerships(xnthread_t *thread);
 * \brief Release all ownerships - INTERNAL.
 *
 * This call is used internally to release all the ownerships obtained
 * by a thread on synchronization objects. This routine must be
 * entered interrupts off.
 *
 * @param thread The descriptor address of the affected thread.
 */

void xnsynch_release_all_ownerships (xnthread_t *thread) /* INTERNAL */

{
    xnpholder_t *holder, *nholder;
    xnsynch_t *synch;

    holder = getheadpq(&thread->claimq);

    while ((synch = link2synch(holder)) != NULL)
	{
	/* Since xnsynch_wakeup_one_sleeper() alters the claim queue, we
	   need to be conservative while skulking it. */

	nholder = nextpq(&thread->claimq,holder);

	if (!testbits(synch->status,XNSYNCH_KMUTEX))
	    xnsynch_wakeup_one_sleeper(synch);

	holder = nholder;
	}
}

/*@}*/

EXPORT_SYMBOL(xnsynch_flush);
EXPORT_SYMBOL(xnsynch_forget_sleeper);
EXPORT_SYMBOL(xnsynch_init);
EXPORT_SYMBOL(xnsynch_release_all_ownerships);
EXPORT_SYMBOL(xnsynch_renice_sleeper);
EXPORT_SYMBOL(xnsynch_sleep_on);
EXPORT_SYMBOL(xnsynch_wakeup_one_sleeper);
EXPORT_SYMBOL(xnsynch_wakeup_this_sleeper);
