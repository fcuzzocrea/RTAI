/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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
 *
 * \ingroup native_timer
 */

/*!
 * \ingroup native
 * \defgroup native_timer Timer management services.
 *
 * Timer-related services allow to control the RTAI system timer which
 * is used in all timed operations.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/timer.h>

/**
 * @fn SRTIME rt_timer_ns2ticks(SRTIME ns)
 * @brief Convert nanoseconds to internal clock ticks.
 *
 * Convert a count of nanoseconds to internal clock ticks.
 * This routine operates on signed nanosecond values.
 *
 * @param ns The count of nanoseconds to convert.
 *
 * @return The corresponding value expressed in internal clock ticks.
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

SRTIME rt_timer_ns2ticks (SRTIME ns)

{
    return xnpod_ns2ticks(ns);
}

/**
 * @fn SRTIME rt_timer_ns2tsc(SRTIME ns)
 * @brief Convert nanoseconds to local CPU clock ticks.
 *
 * Convert a count of nanoseconds to local CPU clock ticks.
 * This routine operates on signed nanosecond values.
 *
 * @param ns The count of nanoseconds to convert.
 *
 * @return The corresponding value expressed in CPU clock ticks.
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

SRTIME rt_timer_ns2tsc (SRTIME ns)

{
    return xnarch_ns_to_tsc(ns);
}

/*!
 * @fn SRTIME rt_timer_ticks2ns(SRTIME ticks)
 * @brief Convert internal clock ticks to nanoseconds.
 *
 * Convert a count of internal clock ticks to nanoseconds.
 * This routine operates on signed tick values.
 *
 * @param ticks The count of loca CPU clock ticks to convert.
 *
 * @return The corresponding value expressed in nanoseconds.
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

SRTIME rt_timer_ticks2ns (SRTIME ticks)

{
    return xnpod_ticks2ns(ticks);
}

/*!
 * @fn SRTIME rt_timer_tsc2ns(SRTIME ticks)
 * @brief Convert local CPU clock ticks to nanoseconds.
 *
 * Convert a local CPU clock ticks to nanoseconds.
 * This routine operates on signed tick values.
 *
 * @param ticks The count of internal clock ticks to convert.
 *
 * @return The corresponding value expressed in nanoseconds.
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

SRTIME rt_timer_tsc2ns (SRTIME ticks)

{
    return xnarch_tsc_to_ns(ticks);
}

/*!
 * @fn int rt_timer_inquire(RT_TIMER_INFO *info)
 * @brief Inquire about the timer.
 *
 * Return various information about the status of the system timer.
 *
 * @param info The address of a structure the timer information will
 * be written to.
 *
 * @return This service always returns 0.
 *
 * The information block returns the period and the current system
 * date. The period can have the following values:
 *
 * - TM_UNSET is a special value indicating that the system timer is
 * inactive. A call to rt_timer_start() activates it.
 *
 * - TM_ONESHOT is a special value indicating that the timer has been
 * set up in oneshot mode.
 *
 * - Any other period value indicates that the system timer is
 * currently running in periodic mode; it is a count of nanoseconds
 * representing the period of the timer, i.e. the duration of a
 * periodic tick or "jiffy".
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

int rt_timer_inquire (RT_TIMER_INFO *info)

{
    RTIME period, tsc;

    if (!testbits(nkpod->status,XNTIMED))
	period = TM_UNSET;
    else if (!testbits(nkpod->status,XNTMPER))
	period = TM_ONESHOT;
    else
	period = xnpod_get_tickval();

    tsc = xnarch_get_cpu_tsc();
    info->period = period;
    info->tsc = tsc;

#ifdef CONFIG_RTAI_HW_PERIODIC_TIMER
    if (period != TM_ONESHOT && period != TM_UNSET)
        info->date = nkpod->jiffies + nkpod->wallclock_offset;
    else
#endif /* CONFIG_RTAI_HW_PERIODIC_TIMER */
        /* In aperiodic mode, our idea of time is the same as the
           CPU's, and a tick equals a nanosecond. */
        info->date = xnarch_tsc_to_ns(tsc) + nkpod->wallclock_offset;
    
    return 0;
}

/*!
 * @fn RTIME rt_timer_read(void)
 * @brief Return the current system time.
 *
 * Return the current time maintained by the system timer.
 *
 * @return The current time expressed in clock ticks (see note).
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
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed as nanoseconds.
 */

RTIME rt_timer_read (void)

{
    return xnpod_get_time();
}

/*!
 * @fn RTIME rt_timer_tsc(void)
 * @brief Return the current TSC value.
 *
 * Return the value of the time stamp counter (TSC) maintained by the
 * CPU of the underlying architecture.
 *
 * @return The current value of the TSC.
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

RTIME rt_timer_tsc (void)

{
    return xnarch_get_cpu_tsc();
}

/**
 * @fn void rt_timer_spin(RTIME ns)
 * @brief Busy wait burning CPU cycles.
 *
 * Enter a busy waiting loop for a count of nanoseconds. The precision
 * of this service largely depends on the availability of a time stamp
 * counter on the current CPU.
 *
 * Since this service is usually called with interrupts enabled, the
 * caller might be preempted by other real-time activities, therefore
 * the actual delay might be longer than specified.
 *
 * @param ns The time to wait expressed in nanoseconds.
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

void rt_timer_spin (RTIME ns)

{
    RTIME etime = xnarch_get_cpu_tsc() + xnarch_ns_to_tsc(ns);

    while (xnarch_get_cpu_tsc() < etime)
	cpu_relax();
}

/**
 * @fn int rt_timer_start(RTIME nstick)
 * @brief Start the system timer.
 *
 * The real-time kernel needs a time source to provide the
 * time-related services to the RTAI tasks. rt_timer_start() sets the
 * current operation mode of the system timer. On architectures that
 * provide a oneshot-programmable time source, the system timer can
 * operate either in oneshot or periodic mode. In oneshot mode, the
 * underlying hardware will be reprogrammed after each clock tick so
 * that the next one occurs after a (possibly non-constant) specified
 * interval, at the expense of a larger overhead due to hardware
 * programming duties. Periodic mode provides timing services at a
 * lower programming cost when the underlying hardware is a true PIT
 * (and not a simple decrementer), but at the expense of a lower
 * precision since all delays are rounded up to the constant interval
 * value used to program the timer.
 *
 * This service defines the time unit which will be relevant when
 * specifying time intervals to the services taking timeout or delays
 * as input parameters. In periodic mode, clock ticks will represent
 * periodic jiffies. In oneshot mode, clock ticks will represent
 * nanoseconds.
 *
 * @param nstick The timer period in nanoseconds. If this parameter is
 * equal to TM_ONESHOT, the underlying hardware timer is set to
 * operate in oneshot-programmable mode. In this mode, timing accuracy
 * is higher - since it is not rounded to a constant time slice - at
 * the expense of a lesser efficicency when many timers are
 * simultaneously active. The oneshot mode gives better results in
 * configuration involving a few tasks requesting timing services over
 * different time scales that cannot be easily expressed as multiples
 * of a single base tick, or would lead to a waste of high frequency
 * periodical ticks.
 *
 * @return 0 is returned on success. Otherwise:
 *
 * - -EBUSY is returned if the timer has already been set.
 * rt_timer_stop() must be issued before rt_timer_start() is called
 * again.
 *
 * - -ENODEV is returned if the underlying architecture does not
 * support the requested periodic timing.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_timer_start (RTIME nstick)

{
    return xnpod_start_timer(nstick,XNPOD_DEFAULT_TICKHANDLER);
}

/**
 * @fn void rt_timer_stop(void)
 * @brief Stop the system timer.
 *
 * This service stops the system timer previously started by a call to
 * rt_timer_start(). Calling rt_timer_stop() whilst the system timer
 * has not been started leads to a null-effect.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task
 *
 * Rescheduling: never.
 */

void rt_timer_stop (void)

{
    xnpod_stop_timer();
}

/*@}*/

EXPORT_SYMBOL(rt_timer_ns2ticks);
EXPORT_SYMBOL(rt_timer_ticks2ns);
EXPORT_SYMBOL(rt_timer_ns2tsc);
EXPORT_SYMBOL(rt_timer_tsc2ns);
EXPORT_SYMBOL(rt_timer_inquire);
EXPORT_SYMBOL(rt_timer_read);
EXPORT_SYMBOL(rt_timer_tsc);
EXPORT_SYMBOL(rt_timer_spin);
EXPORT_SYMBOL(rt_timer_start);
EXPORT_SYMBOL(rt_timer_stop);
