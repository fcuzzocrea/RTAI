/**
 * @file
 * @note Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#include <nucleus/pod.h>
#include <nucleus/thread.h>
#include <nucleus/timer.h>

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
    timer->date = XN_INFINITE;
    timer->shot = XN_INFINITE;

    xnarch_init_display_context(timer);
}

void xntimer_destroy (xntimer_t *timer)

{
    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	xntimer_stop(timer);

    __setbits(timer->status,XNTIMER_KILLED);
}

static inline void xntimer_enqueue_periodic (xntimer_t *timer)

{
    /* Just prepend the new timer to the proper slot. */
    prependq(&nkpod->timerwheel[timer->date & XNTIMER_WHEELMASK],&timer->link);
    timer->shot = timer->date;
    __clrbits(timer->status,XNTIMER_DEQUEUED);
}

static inline void xntimer_dequeue_periodic (xntimer_t *timer)

{
    unsigned slot = (timer->date & XNTIMER_WHEELMASK);
    removeq(&nkpod->timerwheel[slot],&timer->link);
    __setbits(timer->status,XNTIMER_DEQUEUED);
}

#if CONFIG_RTAI_HW_APERIODIC_TIMER

static inline void xntimer_enqueue_aperiodic (xntimer_t *timer)

{
    xnqueue_t *q = &nkpod->timerwheel[0];
    xnholder_t *p;

    /* Insert the new timer at the proper place in the single
       queue managed when running in aperiodic mode. O(N) here,
       but users of the aperiodic mode need to pay a price for
       the increased flexibility... */

    for (p = q->head.last; p != &q->head; p = p->last)
	if (timer->date >= link2timer(p)->date)
	    break;
	
    insertq(q,p->next,&timer->link);
    timer->shot = timer->date;
    __clrbits(timer->status,XNTIMER_DEQUEUED);
}

static inline void xntimer_dequeue_aperiodic (xntimer_t *timer)

{
    removeq(&nkpod->timerwheel[0],&timer->link);
    __setbits(timer->status,XNTIMER_DEQUEUED);
}

static inline void xntimer_next_shot (void)

{
    xnholder_t *holder = getheadq(&nkpod->timerwheel[0]);
    xnticks_t now, delay, xdate;
    xntimer_t *timer;

    if (!holder)
	return; /* No pending timer. */

    timer = link2timer(holder);
    now = xnarch_get_cpu_tsc();
    xdate = now + nkschedlat;

    if (xdate + nktimerlat >= timer->date)
	delay = nktimerlat;
    else
	delay = timer->date - xdate;

    timer->shot = now + delay;

    xnarch_program_timer_shot(delay);
}

static inline int xntimer_heading_p (xntimer_t *timer) {

    return getheadq(&nkpod->timerwheel[0]) == &timer->link;
}
	
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */

/*! 
 * \fn int xntimer_start(xntimer_t *timer,
 		   xnticks_t value,
		   xnticks_t interval)
 * \brief Arm a timer.
 *
 * Starts a timer. The associated timeout handler will be fired after
 * each elapse time. A timer can be either periodic or single-shot,
 * depending on the reload value passed to this routine. The given
 * timer must have been previously initialized by a call to
 * xntimer_init().
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param value The relative date of the initial timer shot, expressed
 * in clock ticks (see note).
 *
 * @param interval The reload value of the timer. It is a periodic
 * interval value to be used for reprogramming the next timer shot,
 * expressed in clock ticks (see note). If @a interval is equal to
 * XN_INFINITE, the timer will not be reloaded when it elapses.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EAGAIN is returned if the underlying time source is operating in
 * one-shot mode and @a value is anterior to the current date.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the xnpod_start_timer() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

int xntimer_start (xntimer_t *timer,
		   xnticks_t value,
		   xnticks_t interval)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	{
#if CONFIG_RTAI_HW_APERIODIC_TIMER
	if (!testbits(nkpod->status,XNTMPER))
	    xntimer_dequeue_aperiodic(timer);
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    xntimer_dequeue_periodic(timer);
	}

    if (value != XN_INFINITE)
	{
#if CONFIG_RTAI_HW_APERIODIC_TIMER
	if (!testbits(nkpod->status,XNTMPER))
	    {
	    timer->date = xnarch_get_cpu_tsc() + xnarch_ns_to_tsc(value);
	    timer->interval = xnarch_ns_to_tsc(interval);
	    xntimer_enqueue_aperiodic(timer);

	    if (xntimer_heading_p(timer))
		{
		if (timer->date <= xnarch_get_cpu_tsc())
		    { /* Too late for this one. */
		    xntimer_dequeue_aperiodic(timer);
		    err = -EAGAIN;
		    }
		else
		    xntimer_next_shot();
		}
	    }
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    {
	    timer->date = nkpod->jiffies + value;
	    timer->interval = interval;
	    xntimer_enqueue_periodic(timer);
	    }
	}
    else
	{
	timer->date = XN_INFINITE;
	timer->interval = XN_INFINITE;
	}

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/*!
 * \fn int xntimer_stop(xntimer_t *timer)
 *
 * \brief Disarm a timer.
 *
 * This service deactivates a timer previously armed using
 * xntimer_start(). Once disarmed, the timer can be subsequently
 * re-armed using the latter service.
 *
 * @param timer The address of a valid timer descriptor.
 *
 */

void xntimer_stop (xntimer_t *timer)

{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	{
#if CONFIG_RTAI_HW_APERIODIC_TIMER
	if (!testbits(nkpod->status,XNTMPER))
	    {
	    int heading = xntimer_heading_p(timer);
	    xntimer_dequeue_aperiodic(timer);
	    /* If we removed the heading timer, reprogram the next
	       shot if any. */
	    if (heading) xntimer_next_shot();
	    }
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    xntimer_dequeue_periodic(timer);
	}

    xnlock_put_irqrestore(&nklock,s);
}

/*
 * xntimer_get_date() -- Return the next trigger date of a timer
 * converted to the current time unit.
 */

xnticks_t xntimer_get_date (xntimer_t *timer)

{
    if (!xntimer_active_p(timer))
	return XN_INFINITE;

#if CONFIG_RTAI_HW_APERIODIC_TIMER
    if (!testbits(nkpod->status,XNTMPER))
	return xnarch_tsc_to_ns(xntimer_date(timer));
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */

    return xntimer_date(timer);
}

/*
 * xntimer_get_timeout() -- Return the time remaining up to the next
 * trigger date of a timer converted to the current time unit.
 */

xnticks_t xntimer_get_timeout (xntimer_t *timer)

{
    if (!xntimer_active_p(timer))
	return XN_INFINITE;

#if CONFIG_RTAI_HW_APERIODIC_TIMER
    if (!testbits(nkpod->status,XNTMPER))
	{
        xnticks_t tsc = xnarch_get_cpu_tsc();

	if (xntimer_date(timer) < tsc)
	    return 1; /* Will elapse shortly. */

	return xnarch_tsc_to_ns(xntimer_date(timer) - tsc);
	}
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */

    return xntimer_date(timer) - nkpod->jiffies;
}

/**
 * @internal
 * xntimer_do_timers() -- Inform all active timers that the clock has
 * been updated. Elapsed timer handlers will be called upon timeout.
 * Only enabled timers are inserted into the timer wheel. Called with
 * nklock locked, interrupts off.
 */

void xntimer_do_timers (void)

{
    xnsched_t *sched = xnpod_current_sched();
    xnholder_t *nextholder, *holder;
    xnqueue_t *timerq, reschedq;
    xnticks_t now = 0; /* Silence preposterous warning. */
    xntimer_t *timer;
#if CONFIG_RTAI_HW_APERIODIC_TIMER
    int aperiodic = !testbits(nkpod->status,XNTMPER);

    if (aperiodic)
	/* Only use slot #0 in aperiodic mode. */
	timerq = &nkpod->timerwheel[0];
    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	{
	/* Update the periodic clocks keeping the things strictly
	   monotonous. */
	now = ++nkpod->jiffies;
	timerq = &nkpod->timerwheel[now & XNTIMER_WHEELMASK];
	++nkpod->wallclock;
	}

#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
    nkpod->timestamps.timer_top = xnarch_get_cpu_tsc();
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */

    initq(&reschedq);

    nextholder = getheadq(timerq);

    while ((holder = nextholder) != NULL)
	{
	nextholder = nextq(timerq,holder);
	timer = link2timer(holder);

#if CONFIG_RTAI_HW_APERIODIC_TIMER
	if (aperiodic)
	    {
	    now = xnarch_get_cpu_tsc();

	    if (timer->shot > now)
		/* No need to continue in aperiodic mode since
		   timeout dates are ordered by increasing
		   values. */
		break;
	    }
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    if (timer->shot > now)
		continue;

	if (timer == &nkpod->htimer)
	    /* By postponing the propagation of the low-priority host
	       tick to the interrupt epilogue (see
	       xnintr_irq_handler()), we save some I-cache, which
	       translates into precious microsecs. */
	    __setbits(sched->status,XNHTICK);
	else
	    {
#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
	    nkpod->timestamps.timer_entry = nkpod->timestamps.timer_top;
	    nkpod->timestamps.timer_drift = (xnsticks_t)now - (xnsticks_t)timer->date;
	    nkpod->timestamps.timer_drift2 = (xnsticks_t)now - (xnsticks_t)timer->shot;
	    nkpod->timestamps.timer_handler = xnarch_get_cpu_tsc();
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
	    /* Otherwise, we'd better have a valid handler... */
	    timer->handler(timer->cookie);
#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
	    nkpod->timestamps.timer_handled = xnarch_get_cpu_tsc();
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
	    }

	/* Restart the timer for the next period if a valid interval
	   has been given. The status is checked in order to prevent
	   rescheduling a timer which has been destroyed, or already
	   rescheduled on behalf of its timeout handler (the latter
	   being a rather inefficient way of managing timers btw). */

	if (!testbits(timer->status,XNTIMER_DEQUEUED))
	    {
#if CONFIG_RTAI_HW_APERIODIC_TIMER
	    if (aperiodic)
		xntimer_dequeue_aperiodic(timer);
	    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
		xntimer_dequeue_periodic(timer);

	    if (timer->interval != XN_INFINITE)
		/* Temporarily move the interval timer to the
		   rescheduling queue, so that we don't see it again
		   until the current dispatching loop is over. */
		appendq(&reschedq,&timer->link);
	    }
	}

    /* Reschedule elapsed interval timers for the next shot. */

    while ((holder = getq(&reschedq)) != NULL)
	{
	timer = link2timer(holder);

#if CONFIG_RTAI_HW_APERIODIC_TIMER
	if (aperiodic)
	    {
	    timer->date += timer->interval;
	    xntimer_enqueue_aperiodic(timer);
	    }
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    {
	    timer->date = nkpod->jiffies + timer->interval;
	    xntimer_enqueue_periodic(timer);
	    }
	}

#if CONFIG_RTAI_HW_APERIODIC_TIMER
    if (aperiodic)
	xntimer_next_shot();
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */

#ifdef CONFIG_RTAI_OPT_TIMESTAMPS
    nkpod->timestamps.timer_exit = xnarch_get_cpu_tsc();
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
}

void xntimer_freeze (void)

{
    spl_t s;
    int n;

    xnarch_stop_timer();

    xnlock_get_irqsave(&nklock,s);

    if (!nkpod || testbits(nkpod->status,XNPIDLE))
	goto unlock_and_exit;

    for (n = 0; n < XNTIMER_WHEELSIZE; n++)
	{
	xnqueue_t *timerq = &nkpod->timerwheel[n];
	xnholder_t *holder = getheadq(timerq);

	while (holder != NULL)
	    {
	    __setbits(link2timer(holder)->status,XNTIMER_DEQUEUED);
	    holder = popq(timerq,holder);
	    }
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);
}

EXPORT_SYMBOL(xntimer_init);
EXPORT_SYMBOL(xntimer_destroy);
EXPORT_SYMBOL(xntimer_start);
EXPORT_SYMBOL(xntimer_stop);
EXPORT_SYMBOL(xntimer_freeze);
EXPORT_SYMBOL(xntimer_get_date);
EXPORT_SYMBOL(xntimer_get_timeout);
