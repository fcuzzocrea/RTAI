/*
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
 */

#ifndef _xenomai_mutex_h
#define _xenomai_mutex_h

#include <xenomai/pod.h>
#include <xenomai/synch.h>

struct xnthread;

typedef struct xnmutex {

    xnsynch_t synchbase;	/* Must be first. */

    atomic_counter_t lockcnt;

} xnmutex_t; /* Kernel mutex */

#ifdef __cplusplus
extern "C" {
#endif

void xnmutex_forget_sleeper(struct xnthread *sleeper);

void xnmutex_sleepon_inner(xnmutex_t *mutex,
			   struct xnthread *thread);

void xnmutex_wakeup_inner(xnmutex_t *mutex,
			  int flags);

static inline int xnmutex_owner_p (xnmutex_t *mutex) {

    /* This code must be entered interrupts off. */

    return (xnarch_atomic_get(&mutex->lockcnt) < 1 &&
	    mutex->synchbase.owner == nkpod->sched.runthread);
}

static inline int xnmutex_clear_lock (xnmutex_t *mutex,
				      atomic_counter_t *pcounter) {

    /* This code must be entered interrupts off. */

    int s = 0;

    if (xnmutex_owner_p(mutex)) {
        *pcounter = mutex->lockcnt;
        if (xnsynch_nsleepers(&mutex->synchbase) > 0) {
	    /* Remove the lock count which accounted for the
	       sleepers. */
	    xnarch_atomic_inc(pcounter);
	    xnarch_atomic_set(&mutex->lockcnt,0);
            /* Wake up one sleeper. */
            xnmutex_wakeup_inner(mutex,XNPOD_NOSWITCH);
	    s = -1;
	} else {
	    xnarch_atomic_set(&mutex->lockcnt,1);
	    s = 1;
	}
    }

    return s;
}

static inline void xnmutex_set_lock (xnmutex_t *mutex,
				     atomic_counter_t *pcounter) {

    xnthread_t *runthread = nkpod->sched.runthread;

    /* This code must be entered interrupts off. */

    if (xnarch_atomic_dec_and_test(&mutex->lockcnt)) {
        /* Mutex was free on entry */
        xnsynch_set_owner(&mutex->synchbase,runthread);
    } else {
           if (xnsynch_owner(&mutex->synchbase) != runthread) {
	       /* Mutex was busy on entry */
	       xnmutex_sleepon_inner(mutex,runthread);
	}
    }

    /* Account for the sleepers if any. */
    if (xnsynch_nsleepers(&mutex->synchbase) > 0)
	xnarch_atomic_dec(pcounter);

    xnarch_atomic_set(&mutex->lockcnt,xnarch_atomic_get(pcounter));
}

    /* -- Beginning of the exported interface */

void xnmutex_init(xnmutex_t *mutex);

static inline void xnmutex_lock (xnmutex_t *mutex) {

    xnthread_t *runthread = nkpod->sched.runthread;
    spl_t s;

    splhigh(s);

    if (xnarch_atomic_dec_and_test(&mutex->lockcnt)) {
        /* Mutex was free on entry */
        xnsynch_set_owner(&mutex->synchbase,runthread);
    } else {
	if (xnsynch_owner(&mutex->synchbase) != runthread) {
	    /* Mutex was busy on entry. */
	    xnmutex_sleepon_inner(mutex,runthread);
	}
    }

    splexit(s);
}

static inline void xnmutex_unlock (xnmutex_t *mutex) {

    if (xnarch_atomic_inc_and_test(&mutex->lockcnt)) {
        if (xnsynch_nsleepers(&mutex->synchbase) > 0) {
            /* Wake up one sleeper. */
            xnmutex_wakeup_inner(mutex,0);
	}
    }
}

#ifdef __cplusplus
}
#endif

#endif /* !_xenomai_mutex_h */
