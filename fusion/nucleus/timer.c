/**
 * @file
 * @note Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 *
 * \ingroup timer
 */

/*!
 * \ingroup nucleus
 * \defgroup timer Timer services.
 *
 * The RTAI/fusion timer facility behaves slightly differently
 * depending on the underlying system timer mode, i.e. periodic or
 * aperiodic. In periodic mode, the hardware timer ticks periodically
 * without any external programming (aside of the initial one which
 * sets its period). In such a case, a BSD timer wheel (see
 * "Redesigning the BSD Callout and Timer Facilities" by Adam
 * M. Costello and George Varghese) is used to its full addressing
 * capabilities.
 * 
 * If the underlying timer source is aperiodic, we need to reprogram
 * the next shot after each tick at hardware level, and we do not need
 * any periodic source. In such a case, the timer manager only uses a
 * single slot (#0) from the wheel as a plain linked list, which is
 * ordered by increasing timeout values of the running timers.
 *
 * Depending on the above mode, the timer object stores time values
 * either as count of periodic ticks, or as count of CPU ticks.
 *
 *@{*/

#define XENO_TIMER_MODULE

#include <nucleus/pod.h>
#include <nucleus/thread.h>
#include <nucleus/timer.h>

/*! 
 * \fn void xntimer_init(xntimer_t *timer,
                         void (*handler)(void *cookie),
			 void *cookie)
 * \brief Initialize a timer object.
 *
 * Creates a timer. When created, a timer is left disarmed; it must be
 * started using xntimer_start() in order to be activated.
 *
 * @param timer The address of a timer descriptor the nucleus will use
 * to store the object-specific data.  This descriptor must always be
 * valid while the object is active therefore it must be allocated in
 * permanent memory.
 *
 * @param handler The routine to call upon expiration of the timer.
 *
 * @param cookie A user-defined opaque cookie the nucleus will pass
 * unmodified to the handler as its unique argument.
 *
 * There is no limitation on the number of timers which can be
 * created/active concurrently.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
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
    timer->prio = XNTIMER_STDPRIO;
#if CONFIG_RTAI_OPT_PERCPU_TIMER
    timer->sched = xnpod_current_sched();
#else /* !CONFIG_RTAI_OPT_PERCPU_TIMER */
    timer->sched = xnpod_sched_slot(XNTIMER_KEEPER_ID);
#endif  /* CONFIG_RTAI_OPT_PERCPU_TIMER */
    
    xnarch_init_display_context(timer);
}

/*! 
 * \fn void xntimer_destroy(xntimer_t *timer)
 *
 * \brief Release a timer object.
 *
 * Destroys a timer. After it has been destroyed, all resources
 * associated with the timer have been released. The timer is
 * automatically deactivated before deletion if active on entry.
 *
 * @param timer The address of a valid timer descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

void xntimer_destroy (xntimer_t *timer)

{
    if (!testbits(timer->status,XNTIMER_DEQUEUED))
	xntimer_stop(timer);

    __setbits(timer->status,XNTIMER_KILLED);
}

static inline void xntimer_enqueue_periodic (xntimer_t *timer)

{
    xnsched_t *sched = timer->sched;
    /* Just prepend the new timer to the proper slot. */
    prependq(&sched->timerwheel[timer->date & XNTIMER_WHEELMASK],&timer->link);
    __clrbits(timer->status,XNTIMER_DEQUEUED);
}

static inline void xntimer_dequeue_periodic (xntimer_t *timer)

{
    unsigned slot = (timer->date & XNTIMER_WHEELMASK);
    removeq(&timer->sched->timerwheel[slot],&timer->link);
    __setbits(timer->status,XNTIMER_DEQUEUED);
}

#if CONFIG_RTAI_HW_APERIODIC_TIMER

static inline void xntimer_enqueue_aperiodic (xntimer_t *timer)

{
    xnqueue_t *q = &timer->sched->timerwheel[0];
    xnholder_t *p;

    /* Insert the new timer at the proper place in the single
       queue managed when running in aperiodic mode. O(N) here,
       but users of the aperiodic mode need to pay a price for
       the increased flexibility... */

    for (p = q->head.last; p != &q->head; p = p->last)
	if (timer->date > link2timer(p)->date ||
	    (timer->date == link2timer(p)->date &&
	     timer->prio <= link2timer(p)->prio))
	    break;
	
    insertq(q,p->next,&timer->link);
    __clrbits(timer->status,XNTIMER_DEQUEUED);
}

static inline void xntimer_dequeue_aperiodic (xntimer_t *timer)

{
    removeq(&timer->sched->timerwheel[0],&timer->link);
    __setbits(timer->status,XNTIMER_DEQUEUED);
}

static inline void xntimer_next_local_shot (xnsched_t *this_sched)

{
    xnholder_t *holder = getheadq(&this_sched->timerwheel[0]);
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

    xnarch_program_timer_shot(delay <= ULONG_MAX ? delay : ULONG_MAX);
}

static inline int xntimer_heading_p (xntimer_t *timer) {

    return getheadq(&timer->sched->timerwheel[0]) == &timer->link;
}
	
static inline void xntimer_next_remote_shot (xnsched_t *sched)
{
    xnarch_send_timer_ipi(xnarch_cpumask_of_cpu(xnsched_cpu(sched)));
}

#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */

/*! 
 * \fn int xntimer_start(xntimer_t *timer,
 		   xnticks_t value,
		   xnticks_t interval)
 * \brief Arm a timer.
 *
 * Activates a timer so that the associated timeout handler will be
 * fired after each expiration time. A timer can be either periodic or
 * single-shot, depending on the reload value passed to this
 * routine. The given timer must have been previously initialized by a
 * call to xntimer_init().
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @param value The relative date of the initial timer shot, expressed
 * in clock ticks (see note).
 *
 * @param interval The reload value of the timer. It is a periodic
 * interval value to be used for reprogramming the next timer shot,
 * expressed in clock ticks (see note). If @a interval is equal to
 * XN_INFINITE, the timer will not be reloaded after it has expired.
 *
 * @return 0 is always returned.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the xnpod_start_timer() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
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
                if(xntimer_sched(timer) != xnpod_current_sched())
                    xntimer_next_remote_shot(xntimer_sched(timer));
                else
                    xntimer_next_local_shot(xntimer_sched(timer));
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
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
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
	       shot if any. If the timer was running on another CPU, let it
	       tick. */
	    if (heading && xntimer_sched(timer) == xnpod_current_sched())
                xntimer_next_local_shot(xntimer_sched(timer));
	    }
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    xntimer_dequeue_periodic(timer);
	}

    xnlock_put_irqrestore(&nklock,s);
}

#if CONFIG_SMP
/**
 * Migrate a timer.
 *
 * This call migrates a timer to another cpu. In order to avoid pathological
 * cases, it must be called from the CPU to which @a timer is currently
 * attached.
 *
 * @param timer The address of the timer object to be migrated.
 *
 * @param sched The address of the destination CPU xnsched_t structure.
 *
 * @retval -EINVAL if @a timer is queued on another CPU than current ;
 * @retval 0 otherwise.
 *
 */
int xntimer_set_sched(xntimer_t *timer, xnsched_t *sched)
{
    int err = 0;

#if CONFIG_RTAI_OPT_PERCPU_TIMER
    int queued;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    if (sched == timer->sched)
        goto unlock_and_exit;

    queued = !testbits(timer->status,XNTIMER_DEQUEUED);

    /* Avoid the pathological case where the timer interrupt did not occur yet
       for the current date on the timer source CPU, whereas we are trying to
       migrate it to a CPU where the timer interrupt already occured. This would
       not be a problem in aperiodic mode. */
    if (queued && timer->sched != xnpod_current_sched())
        {
        err = -EINVAL;
        goto unlock_and_exit;
        }

    if (queued)
        xntimer_stop(timer);
    timer->sched = sched;

    if (queued)
        {
#if CONFIG_RTAI_HW_APERIODIC_TIMER
	if (!testbits(nkpod->status,XNTMPER))
            {
            xntimer_enqueue_aperiodic(timer);

            if (xntimer_heading_p(timer))
                {
                if (sched != xnpod_current_sched())
                    xntimer_next_remote_shot(sched);
                else
                    xntimer_next_local_shot(sched);
                }
            }
        else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
            xntimer_enqueue_periodic(timer);
        }

 unlock_and_exit:
    xnlock_put_irqrestore(&nklock, s);

#endif  /* CONFIG_RTAI_OPT_PERCPU_TIMER */

    return err;
}
#endif /* CONFIG_SMP */

/*!
 * \fn xnticks_t xntimer_get_date(xntimer_t *timer)
 *
 * \brief Return the absolute expiration date.
 *
 * Return the next expiration date of a timer in absolute clock ticks
 * (see note).
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The expiration date converted to the current time unit. The
 * special value XN_INFINITE is returned if @a timer is currently
 * inactive.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the xnpod_start_timer() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed as nanoseconds.
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

/*!
 * \fn xnticks_t xntimer_get_timeout(xntimer_t *timer)
 *
 * \brief Return the relative expiration date.
 *
 * Return the next expiration date of a timer in relative clock ticks
 * (see note).
 *
 * @param timer The address of a valid timer descriptor.
 *
 * @return The expiration date converted to the current time unit. The
 * special value XN_INFINITE is returned if @a timer is currently
 * inactive. In oneshot mode, it might happen that the timer has
 * already expired when this service is run (even if the associated
 * handler has not been fired yet); in such a case, 1 is returned.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the xnpod_start_timer() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed as nanoseconds.
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

/*!
 * @internal
 * \fn void xntimer_do_timers(void)
 *
 * \brief Process a timer tick.
 *
 * This routine informs all active timers that the clock has been
 * updated by processing the timer wheel. Elapsed timer actions will
 * be fired.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Interrupt service routine, nklock locked, interrupts off
 *
 * Rescheduling: never.
 *
 * @note Only active timers are inserted into the timer wheel.
 */

void xntimer_do_timers (void)

{
    xnsched_t *sched = xnpod_current_sched();
    xnholder_t *nextholder, *holder;
    xnqueue_t *timerq, reschedq;
    xntimer_t *timer;
    xnticks_t now;
#if CONFIG_RTAI_HW_APERIODIC_TIMER
    int aperiodic = !testbits(nkpod->status,XNTMPER);

    if (aperiodic)
	{
	now = xnarch_get_cpu_tsc();
	/* Only use slot #0 in aperiodic mode. */
        timerq = &sched->timerwheel[0];
        }
    else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	{
	/* Update the periodic clocks keeping the things strictly
	   monotonous (only CPU XNTIMER_KEEPER_ID does this). */
#if CONFIG_RTAI_OPT_PERCPU_TIMER
        if (sched == xnpod_sched_slot(XNTIMER_KEEPER_ID))
#endif /* CONFIG_RTAI_OPT_PERCPU_TIMER */
            {
            ++nkpod->jiffies;
            ++nkpod->wallclock;
            }
        now = nkpod->jiffies;
        timerq = &sched->timerwheel[now & XNTIMER_WHEELMASK];
	}

#if CONFIG_RTAI_OPT_TIMESTAMPS
    nkpod->timestamps.timer_entry = xnarch_get_cpu_tsc();
    xnarch_read_timings(&nkpod->timestamps.tick_shot,
			&nkpod->timestamps.tick_delivery,
			nkpod->timestamps.timer_entry);
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
	    if (timer->date - nkschedlat > now)
		/* No need to continue in aperiodic mode since
		   timeout dates are ordered by increasing
		   values. */
		break;
	    }
	else
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */
	    if (timer->date > now)
		continue;

	if (timer == &nkpod->htimer)
	    /* By postponing the propagation of the low-priority host
	       tick to the interrupt epilogue (see
	       xnintr_irq_handler()), we save some I-cache, which
	       translates into precious microsecs on low-end hw. */
	    __setbits(sched->status,XNHTICK);
	else
	    {
#if CONFIG_RTAI_OPT_TIMESTAMPS
	    nkpod->timestamps.timer_drift = (xnsticks_t)now - (xnsticks_t)timer->date;
	    nkpod->timestamps.timer_handler = xnarch_get_cpu_tsc();
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
	    /* Otherwise, we'd better have a valid handler... */
	    timer->handler(timer->cookie);
#if CONFIG_RTAI_OPT_TIMESTAMPS
	    nkpod->timestamps.timer_handled = xnarch_get_cpu_tsc();
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
	    }

	/* Restart the timer for the next period if a valid interval
	   has been given. The status is checked in order to prevent
	   rescheduling a timer which has been destroyed, or already
	   rescheduled on behalf of its timeout handler. A killed
	   timer has already been dequeued, so there is no need to
	   specifically check for the XNTIMER_KILLED status here. */

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
	xntimer_next_local_shot(sched);
#endif /* CONFIG_RTAI_HW_APERIODIC_TIMER */

#if CONFIG_RTAI_OPT_TIMESTAMPS
    nkpod->timestamps.timer_exit = xnarch_get_cpu_tsc();
#endif /* CONFIG_RTAI_OPT_TIMESTAMPS */
}

/*!
 * @internal
 * \fn void xntimer_freeze(void)
 *
 * \brief Freeze all timers.
 *
 * This routine deactivates all active timers atomically.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Interrupt service routine, nklock unlocked
 *
 * Rescheduling: never.
 *
 * @note Always make sure the nklock is free when stopping the
 * underlying timing source by calling xnarch_stop_timer(), otherwise,
 * deadlock situations would arise on some architectures.
 */

void xntimer_freeze (void)

{
#if CONFIG_RTAI_OPT_PERCPU_TIMER
    int nr_cpus;
#endif /* CONFIG_RTAI_OPT_PERCPU_TIMER */
    int n, cpu;
    spl_t s;

    xnarch_stop_timer();

    xnlock_get_irqsave(&nklock,s);

    if (!nkpod || testbits(nkpod->status,XNPIDLE))
	goto unlock_and_exit;

#if CONFIG_RTAI_OPT_PERCPU_TIMER
    nr_cpus = xnarch_num_online_cpus();
    for (cpu = 0; cpu < nr_cpus; cpu++)
#else /* !CONFIG_RTAI_OPT_PERCPU_TIMER */
	cpu = XNTIMER_KEEPER_ID;
#endif /* CONFIG_RTAI_OPT_PERCPU_TIMER */
        for (n = 0; n < XNTIMER_WHEELSIZE; n++)
	    {
            xnqueue_t *timerq = &xnpod_sched_slot(cpu)->timerwheel[n];
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

/*@}*/

EXPORT_SYMBOL(xntimer_init);
EXPORT_SYMBOL(xntimer_destroy);
EXPORT_SYMBOL(xntimer_start);
EXPORT_SYMBOL(xntimer_stop);
EXPORT_SYMBOL(xntimer_freeze);
EXPORT_SYMBOL(xntimer_get_date);
EXPORT_SYMBOL(xntimer_get_timeout);
