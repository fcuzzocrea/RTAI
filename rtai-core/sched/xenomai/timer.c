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

#define XENO_TIMER_MODULE

#include "rtai_config.h"
#include <xenomai/pod.h>
#include <xenomai/mutex.h>
#include <xenomai/thread.h>
#include <xenomai/timer.h>

/*
 * This code implements a timer facility based on the timer wheel
 * algorithm described in "Redesigning the BSD Callout and Timer
 * Facilities" by Adam M. Costello and George Varghese.
 *
 * This code behaves slightly differently depending on the underlying
 * system timer mode, i.e. periodic or aperiodic. In periodic mode,
 * the hardware timer ticks periodically without any external
 * programming (aside of the initial one which sets its period). In
 * such a case, a BSD timer wheel is used to its full addressing
 * capabilities. If the underlying timer source is aperiodic, we need
 * to reprogram the next shot after each tick at hardware level, and
 * we cannot count on a strictly periodic source. In such a case, the
 * timer manager only uses a single slot from the wheel as a plain
 * linked list, which is ordered by increasing timeout values of the
 * running timers. See the discussion about xnpod_start_timer() for
 * more.
 *
 * Depending on the above mode, the timer object stores time values
 * either as count of periodic ticks, or as count of CPU ticks for
 * performance reasons. In the latter case, the upper interface must
 * express delay values as count of nanoseconds when calling the timer
 * services though.
 */

void xntimer_init (xntimer_t *timer,
		   void (*handler)(void *cookie),
		   void *cookie)
{
    /* CAUTION: Setup from xntimer_init() must not depend on the
       periodic/aperiodic timing mode. */
     
    inith(&timer->link);
    timer->status = XNTIMER_DEQUEUED;
    timer->handler = handler;
    timer->cookie = cookie;
    timer->interval = 0;
    timer->date = 0;

    xnarch_init_display_context(timer);
}

void xntimer_destroy (xntimer_t *timer)

{
    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	xntimer_stop(timer);

    setbits(timer->status,XNTIMER_KILLED);
}

static inline void xntimer_enqueue (xntimer_t *timer)

{
    if (testbits(nkpod->status,XNTMPER))
	/* Just prepend the new timer to the proper slot. */
	prependq(&nkpod->timerwheel[timer->date & XNTIMER_WHEELMASK],&timer->link);
    else
	{
	/* Insert the new timer at the proper place in the single
	   queue managed when running in aperiodic mode. O(N) here,
	   but users of the aperiodic mode need to pay a price for
	   the increased flexibility... */
	xnqueue_t *q = &nkpod->timerwheel[0];
	xnholder_t *p;

	for (p = q->head.last; p != &q->head; p = p->last)
	    if (timer->date >= link2timer(p)->date)
		break;
	
	insertq(q,p->next,&timer->link);
	}

    clrbits(timer->status,XNTIMER_DEQUEUED);
}

static inline void xntimer_dequeue (xntimer_t *timer)

{
    int slot = testbits(nkpod->status,XNTMPER) ? (timer->date & XNTIMER_WHEELMASK) : 0;
    removeq(&nkpod->timerwheel[slot],&timer->link);
    setbits(timer->status,XNTIMER_DEQUEUED);
}

#if XNARCH_APERIODIC_PREC != 0

static inline xnticks_t xntimer_next_shot (void)

{
    xnholder_t *holder = getheadq(&nkpod->timerwheel[0]);

    if (holder)
	{
	xntimer_t *timer = link2timer(holder);
	unsigned long long tsc = xnarch_get_cpu_tsc();

	if (timer->date <= tsc + nkpod->latency)
	    return XN_NONBLOCK;

	return timer->date - tsc - nkpod->latency;
	}

    return XN_INFINITE;
}

static inline int xntimer_heading_p (xntimer_t *timer)

{
    return (!testbits(nkpod->status,XNTMPER) &&
	    getheadq(&nkpod->timerwheel[0]) == &timer->link);
}
	
#endif /* XNARCH_APERIODIC_PREC != 0 */

/*
 * xntimer_set_initial_date() -- Computes the initial trigger date of
 * a timer (in internal representation) according to the current
 * timing mode, either periodic or aperiodic.
 */

static inline void xntimer_set_initial_date (xntimer_t *timer,
					     xnticks_t incr)
{
    if (testbits(nkpod->status,XNTMPER))
	timer->date = nkpod->jiffies + incr;
    else
	timer->date = xnarch_get_cpu_tsc() + incr;
}

/*
 * xntimer_reschedule() -- Computes the subsequent trigger date of a
 * just elapsed timer (in internal representation) according to the
 * current timing mode, either periodic or aperiodic, and enqueue the
 * timer at the proper place.
 */

static inline void xntimer_reschedule (xntimer_t *timer)

{
    if (testbits(nkpod->status,XNTMPER))
	timer->date = nkpod->jiffies + timer->interval;
    else
	timer->date += timer->interval;

    xntimer_enqueue(timer);
}

/*
 * xntimer_get_internal_delay() -- Converts a standard delay value into
 * its internal representation.
 */

static inline xnticks_t xntimer_get_internal_delay (xnticks_t delay)

{
    if (testbits(nkpod->status,XNTMPER))
	return delay;

    return xnarch_ns_to_tsc(delay);
}

/*
 * xntimer_start() -- Arm a timer. If <interval> is != XN_INFINITE,
 * the timeout handler will be fired periodically according to the
 * given interval value. Otherwise, the timer is one-shot. This
 * configuration must not be confused with the underlying system timer
 * periodic/aperiodic mode, which is rather used to control the
 * hardware time source.  In aperiodic mode, the current date could be
 * posterior to the timeout value; in such a case, XNERR_2LATE is
 * returned and the timer remains unarmed.
 */

int xntimer_start (xntimer_t *timer,
		   xnticks_t value,
		   xnticks_t interval)
{
    int err = XN_OK;
    spl_t s;

    splhigh(s);

    timer->interval = xntimer_get_internal_delay(interval);

    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	xntimer_dequeue(timer);

    if (value != XN_INFINITE)
	{
	xntimer_set_initial_date(timer,xntimer_get_internal_delay(value));
	xntimer_enqueue(timer);
#if XNARCH_APERIODIC_PREC != 0
	if (xntimer_heading_p(timer))
	    {
	    xnticks_t delay = xntimer_next_shot();

	    if (delay == XN_NONBLOCK)	/* Too late for this one. */
		{
		xntimer_dequeue(timer);
		err = XNERR_2LATE;
		}
	    else if (delay != XN_INFINITE)
		/* Program the hardware for the next shot. */
		xnarch_program_timer_shot(delay);
	    }
#endif /* XNARCH_APERIODIC_PREC != 0 */
	}

    splexit(s);

    return err;
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
#if XNARCH_APERIODIC_PREC != 0
	int heading = xntimer_heading_p(timer);
#endif /* XNARCH_APERIODIC_PREC */
	xntimer_dequeue(timer);
#if XNARCH_APERIODIC_PREC != 0
	/* If we removed the heading timer, reprogram the next shot,
	   unless the latter is too close or inexistent. */
	if (heading)
	    {
	    xnticks_t delay = xntimer_next_shot();

	    if (delay != XN_INFINITE)
		xnarch_program_timer_shot(delay);
	    }
#endif /* XNARCH_APERIODIC_PREC != 0 */
	}

    splexit(s);
}

/*
 * xntimer_get_date() -- Return the next trigger date of a timer
 * converted to the current time unit.
 */

xnticks_t xntimer_get_date (xntimer_t *timer)

{
    if (!xntimer_active_p(timer))
	return XN_INFINITE;

    if (testbits(nkpod->status,XNTMPER))
	return xntimer_date(timer);

    return xnarch_tsc_to_ns(xntimer_date(timer));
}

/*
 * xntimer_get_timeout() -- Return the time remaining up to the next
 * trigger date of a timer converted to the current time unit.
 */

xnticks_t xntimer_get_timeout (xntimer_t *timer)

{
    xnticks_t tsc;

    if (!xntimer_active_p(timer))
	return XN_INFINITE;

    if (testbits(nkpod->status,XNTMPER))
	return xntimer_date(timer) - nkpod->jiffies;

    tsc = xnarch_get_cpu_tsc();

    if (xntimer_date(timer) < tsc)
	return 1; /* Will elapse shortly. */

    return xnarch_tsc_to_ns(xntimer_date(timer) - tsc);
}

/*
 * xntimer_do_timers() -- Inform all active periodic timers that the
 * clock has been updated. Elapsed timer handlers will be called upon
 * timeout.  Only enabled timers are inserted into the timer wheel. An
 * enabled timer is unlinked from the wheel queue while its handler is
 * running. Since the following routine runs on behalf of an interrupt
 * service thread, timeout handlers MAY reenter the nanokernel but MAY
 * NOT attempt to block the underlying service thread.
 */

void xntimer_do_timers (int incr) /* INTERNAL */

{
    xnholder_t *nextholder, *holder;
    xnqueue_t *timerq, reschedq;
    xntimer_t *timer;
    xnticks_t now;
    spl_t s;

    initq(&reschedq);

    splhigh(s);

#if XNARCH_APERIODIC_PREC != 0
 restart:
#endif /* XNARCH_APERIODIC_PREC != 0 */

    while (incr-- > 0)
	{
	if (testbits(nkpod->status,XNTMPER))
	    {
	    /* Update the periodic clocks keeping the things strictly
	       monotonous. */
	    now = ++nkpod->jiffies;
	    timerq = &nkpod->timerwheel[now & XNTIMER_WHEELMASK];
	    nkpod->wallclock++;
	    }
	else
	    {
	    /* Only use slot #0 in aperiodic mode. */
	    timerq = &nkpod->timerwheel[0];
	    now = xnarch_get_cpu_tsc();
	    }

	nextholder = getheadq(timerq);

	while ((holder = nextholder) != NULL)
	    {
	    nextholder = nextq(timerq,holder);
	    timer = link2timer(holder);

	    if (timer->date > now)
		{
		if (!testbits(nkpod->status,XNTMPER))
		    /* No need to continue in aperiodic mode since
		       timeout dates are ordered by increasing
		       values. */
		    break;

		continue;
		}

	    timer->handler(timer->cookie);

	    /* Restart the timer for the next period if a valid
	       interval has been given. The status is checked in order
	       to prevent rescheduling a timer which has been
	       destroyed, or already rescheduled on behalf of its
	       timeout handler (the latter being a rather inefficient
	       way of managing timers btw). */

	    if (!testbits(timer->status,XNTIMER_DEQUEUED))
		{
		xntimer_dequeue(timer);

		if (timer->interval != XN_INFINITE)
		    {
		    /* Temporarily move the interval timer to the
		       rescheduling queue, so that we don't see it
		       again until the current dispatching loop is
		       over. */
		    appendq(&reschedq,&timer->link);
		    }
		}
	    }

	/* Reschedule elapsed interval timers for the next shot. */
	while ((holder = getq(&reschedq)) != NULL)
	    xntimer_reschedule(link2timer(holder));
	}

#if XNARCH_APERIODIC_PREC != 0

    if (!testbits(nkpod->status,XNTMPER))
	{
	xnticks_t delay = xntimer_next_shot();

	if (delay == XN_NONBLOCK) /* Oops, overdue timer: rescan. */
	    {
	    incr = 1;
	    goto restart;
	    }

	if (delay != XN_INFINITE) /* Program the hardware for the next shot. */
	    xnarch_program_timer_shot(delay);

	/* Otherwise, no more active timers. */
	}

#endif /* XNARCH_APERIODIC_PREC != 0 */

    splexit(s);
}

void xntimer_freeze (void)

{
    spl_t s;
    int n;

    splhigh(s);

    xnarch_stop_timer();

    for (n = 0; n < XNTIMER_WHEELSIZE; n++)
	{
	xnqueue_t *timerq = &nkpod->timerwheel[n];
	xnholder_t *holder = getheadq(timerq);

	while (holder != NULL)
	    {
	    setbits(link2timer(holder)->status,XNTIMER_DEQUEUED);
	    holder = popq(timerq,holder);
	    }
	}

    splexit(s);
}

EXPORT_SYMBOL(xntimer_init);
EXPORT_SYMBOL(xntimer_destroy);
EXPORT_SYMBOL(xntimer_start);
EXPORT_SYMBOL(xntimer_stop);
EXPORT_SYMBOL(xntimer_freeze);
EXPORT_SYMBOL(xntimer_get_date);
EXPORT_SYMBOL(xntimer_get_timeout);
