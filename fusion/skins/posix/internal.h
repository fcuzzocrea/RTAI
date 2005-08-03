/*
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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
 */

#ifndef _POSIX_INTERNAL_H
#define _POSIX_INTERNAL_H

#include <nucleus/xenomai.h>
#include <nucleus/fusion.h>
#include <posix/posix.h>

#define PSE51_MAGIC(n) (0x8686##n##n)
#define PSE51_ANY_MAGIC         PSE51_MAGIC(00)
#define PSE51_THREAD_MAGIC      PSE51_MAGIC(01)
#define PSE51_THREAD_ATTR_MAGIC PSE51_MAGIC(02)
#define PSE51_MUTEX_MAGIC       PSE51_MAGIC(03)
#define PSE51_MUTEX_ATTR_MAGIC  PSE51_MAGIC(04)
#define PSE51_COND_MAGIC        PSE51_MAGIC(05)
#define PSE51_COND_ATTR_MAGIC   PSE51_MAGIC(05)
#define PSE51_SEM_MAGIC         PSE51_MAGIC(06)
#define PSE51_KEY_MAGIC         PSE51_MAGIC(07)
#define PSE51_ONCE_MAGIC        PSE51_MAGIC(08)
#define PSE51_MQ_MAGIC          PSE51_MAGIC(09)
#define PSE51_MQD_MAGIC         PSE51_MAGIC(0A)
#define PSE51_INTR_MAGIC        PSE51_MAGIC(0B)

#define PSE51_MIN_PRIORITY      FUSION_LOW_PRIO
#define PSE51_MAX_PRIORITY      FUSION_HIGH_PRIO

#define ONE_BILLION             1000000000

#define pse51_obj_active(h,m,t) \
((h) && ((t *)(h))->magic == (m))

#define pse51_obj_deleted(h,m,t) \
((h) && ((t *)(h))->magic == ~(m))

#define pse51_mark_deleted(t) ((t)->magic = ~(t)->magic)

static inline void ticks2ts(struct timespec *ts, xnticks_t ticks)
{
    ts->tv_sec = xnarch_uldivrem(xnpod_ticks2ns(ticks),
                                 ONE_BILLION,
                                 &ts->tv_nsec);
}

static inline xnticks_t ts2ticks_floor(const struct timespec *ts)
{
    xntime_t nsecs = ts->tv_nsec;
    if(ts->tv_sec)
        nsecs += (xntime_t) ts->tv_sec * ONE_BILLION;
    return xnpod_ns2ticks(nsecs);
}

static inline xnticks_t ts2ticks_ceil(const struct timespec *ts)
{
    xntime_t nsecs = ts->tv_nsec;
    unsigned long rem;
    xnticks_t ticks;
    if(ts->tv_sec)
        nsecs += (xntime_t) ts->tv_sec * ONE_BILLION;
    ticks = xnarch_ulldiv(nsecs, xnpod_get_tickval(), &rem);
    return rem ? ticks+1 : ticks;
}

static inline xnticks_t clock_get_ticks(clockid_t clock_id)
{
    if(clock_id == CLOCK_REALTIME)
        return xnpod_get_time();
    else
        return xnpod_ns2ticks(xnpod_get_cpu_time());
}

static inline int clock_adjust_timeout(xnticks_t *timeoutp, clockid_t clock_id)
{
    xnsticks_t delay;

    if(*timeoutp == XN_INFINITE)
        return 0;

    delay = *timeoutp - clock_get_ticks(clock_id);
    if(delay < 0)
        return ETIMEDOUT;

    *timeoutp = delay;
    return 0;
}

#endif /* !_POSIX_INTERNAL_H */
