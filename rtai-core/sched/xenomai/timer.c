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

#include "rtai_config.h"
#include "xenomai/pod.h"
#include "xenomai/mutex.h"
#include "xenomai/thread.h"
#include "xenomai/timer.h"

/*
 * This code implements a timer facility based on the timer wheel
 * algorithm described in "Redesigning the BSD Callout and Timer
 * Facilities" by Adam M. Costello and George Varghese.
 */

void xntimer_init (xntimer_t *timer,
		   void (*handler)(void *cookie),
		   void *cookie)
{
    inith(&timer->link);
    timer->status = XNTIMER_DEQUEUED;
    timer->handler = handler;
    timer->cookie = cookie;
    timer->interval = 0;

    xnarch_init_display_context(timer);
}

void xntimer_destroy (xntimer_t *timer)

{
    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	xntimer_stop(timer);

    setbits(timer->status,XNTIMER_KILLED);
}

static inline xnqueue_t *xntimer_get_time_slot (xnticks_t time) {

    return &nkpod->timerwheel[time & XNTIMER_WHEELMASK];
}

/*
 * xntimer_start() -- Arm a timer. If <interval> is != XN_INFINITE,
 * the timeout handler will be fired periodically according to the
 * given interval value. Otherwise, the timer is one-shot.
 */

void xntimer_start (xntimer_t *timer,
		    xnticks_t value,
		    xnticks_t interval)
{
    spl_t s;

    splhigh(s);

    timer->interval = interval;

    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	{
	removeq(xntimer_get_time_slot(timer->date),&timer->link);
	setbits(timer->status,XNTIMER_DEQUEUED);
	}

    if (value != XN_INFINITE)
	{
	timer->date = nkpod->jiffies + value;
	prependq(xntimer_get_time_slot(timer->date),&timer->link);
	clrbits(timer->status,XNTIMER_DEQUEUED);
	}

    splexit(s);
}

/*
 * xntimer_stop() -- Disarm a timer previously armed by
 * xntimer_start().
 */

void xntimer_stop (xntimer_t *timer)

{
    spl_t s;

    splhigh(s);

    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	{
	removeq(xntimer_get_time_slot(timer->date),&timer->link);
	setbits(timer->status,XNTIMER_DEQUEUED);
	}

    splexit(s);
}

/*
 * xntimer_do_timers() -- Inform all active periodic timers that the
 * clock has been updated. Elapsed timer handlers will be called upon
 * timeout.  Only enabled timers are inserted into the timer wheel. An
 * enabled timer is unlinked from the wheel queue while its handler is
 * run. Since the following routine runs on behalf of an interrupt
 * service thread, timeout handlers MAY reenter the nanokernel but MAY
 * NOT attempt to block the underlying service thread.
 */

void xntimer_do_timers (int incr) /* INTERNAL */

{
    xnholder_t *nextholder, *holder;
    xnticks_t jiffies;
    xnqueue_t *timerq;
    xntimer_t *timer;
    spl_t s;

    splhigh(s);

    for (jiffies = nkpod->jiffies; incr > 0; incr--, jiffies++)
	{
	/* Fetch the proper list from the timer wheel */
	timerq = xntimer_get_time_slot(jiffies);
	nextholder = getheadq(timerq);

	while ((holder = nextholder) != NULL)
	    {
	    timer = link2timer(holder);

	    if (timer->date > jiffies)
		{
		nextholder = nextq(timerq,holder);
		continue;
		}

	    nextholder = popq(timerq,holder);
	    setbits(timer->status,XNTIMER_DEQUEUED);
	    timer->handler(timer->cookie);

	    /* Restart the timer for the next period if a valid
	       interval has been given. The status is checked in order to
	       prevent restarting a timer which has been destroyed or
	       already restarted on behalf of its timeout handler. */

	    if (!testbits(timer->status,XNTIMER_KILLED) && /* !destroyed? */
		testbits(timer->status,XNTIMER_DEQUEUED) &&
		timer->interval != XN_INFINITE)
		{
		timer->date = jiffies + timer->interval;
		prependq(xntimer_get_time_slot(timer->date),&timer->link);
		clrbits(timer->status,XNTIMER_DEQUEUED);
		}
	    }
	}

    splexit(s);
}
