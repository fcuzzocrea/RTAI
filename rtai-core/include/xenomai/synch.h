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

#ifndef _xenomai_synch_
#define _xenomai_synch_

#include "xenomai/queue.h"

/* Creation flags */
#define XNSYNCH_FIFO    0x0
#define XNSYNCH_PRIO    0x1
#define XNSYNCH_NOPIP   0x0
#define XNSYNCH_PIP     0x2

#define XNSYNCH_CLAIMED 0x10	/* Claimed by other thread(s) w/ PIP */
#define XNSYNCH_KMUTEX  0x20	/* A kernel mutex base */

/* Spare flags usable by upper interfaces */
#define XNSYNCH_SPARE0  0x01000000
#define XNSYNCH_SPARE1  0x02000000
#define XNSYNCH_SPARE2  0x04000000
#define XNSYNCH_SPARE3  0x08000000
#define XNSYNCH_SPARE4  0x10000000
#define XNSYNCH_SPARE5  0x20000000
#define XNSYNCH_SPARE6  0x40000000
#define XNSYNCH_SPARE7  0x80000000

/* Statuses */
#define XNSYNCH_DONE    0	/* Resource available / operation complete */
#define XNSYNCH_WAIT    1	/* Calling thread blocked -- start rescheduling */
#define XNSYNCH_RESCHED 2	/* Force rescheduling */

struct xnthread;
struct xnsynch;
struct xnmutex;

typedef struct xnsynch {

    xnpholder_t link;	/* Link in claim queues */

#define link2synch(laddr) \
((xnsynch_t *)(((char *)laddr) - (int)(&((xnsynch_t *)0)->link)))

    xnflags_t status;	/* Status word */

    xnpqueue_t pendq;	/* Pending threads */

    struct xnthread *owner; /* Thread which owns the resource */

    XNARCH_DECL_DISPLAY_CONTEXT();

} xnsynch_t;

#define xnsynch_name(synch)              ((synch)->name)
#define xnsynch_test_flags(synch,flags)  testbits((synch)->status,flags)
#define xnsynch_set_flags(synch,flags)   setbits((synch)->status,flags)
#define xnsynch_clear_flags(synch,flags) clrbits((synch)->status,flags)
#define xnsynch_wait_queue(synch)        (&((synch)->pendq))
#define xnsynch_nsleepers(synch)         countpq(&((synch)->pendq))
#define xnsynch_owner(synch)             ((synch)->owner)

#ifdef __cplusplus
extern "C" {
#endif

void xnsynch_init(xnsynch_t *synch,
		  xnflags_t flags);

#define xnsynch_destroy(synch) \
xnsynch_flush(synch,XNRMID)

static inline void xnsynch_set_owner (xnsynch_t *synch, struct xnthread *thread) {
    synch->owner = thread;
}

void xnsynch_sleep_on(xnsynch_t *synch,
		      xnticks_t timeout,
		      struct xnmutex *imutex);

struct xnthread *xnsynch_wakeup_one_sleeper(xnsynch_t *synch);

xnpholder_t *xnsynch_wakeup_this_sleeper(xnsynch_t *synch,
					 xnpholder_t *holder);

int xnsynch_flush(xnsynch_t *synch,
		  xnflags_t reason);

void xnsynch_release_all_ownerships(struct xnthread *thread);

void xnsynch_renice_sleeper(struct xnthread *thread);

void xnsynch_forget_sleeper(struct xnthread *thread);

#ifdef __cplusplus
}
#endif

#endif /* !_xenomai_synch_ */
