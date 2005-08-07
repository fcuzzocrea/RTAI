/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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


#include "posix/thread.h"
#include "posix/timer.h"

static xnqueue_t pse51_timerq,
                 timer_freeq;

static struct pse51_timer timer_pool[PSE51_TIMER_MAX];

void base_timer_handler (void *cookie)
{
    struct pse51_timer *timer = (struct pse51_timer *)cookie;
}

int timer_create(clockid_t clockid,
		 struct sigevent *evp,
		 timer_t *timerid)
{
    struct pse51_timer *timer;
    spl_t s;

    if (clockid != CLOCK_MONOTONIC)
	{
        thread_set_errno(EINVAL);
	return -1;
	}

    xnlock_get_irqsave(&nklock, s);
    
    timer = (struct pse51_timer *)getq(&timer_freeq);

    if (!timer)
	{
	xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EAGAIN);
	return -1;
	}

    appendq(&pse51_timerq,&timer->link);

    xnlock_put_irqrestore(&nklock, s);

    xntimer_init(&timer->timerbase,
		 &base_timer_handler,
		 timer);

    timer->overruns = 0;

    *timerid = timer - timer_pool;

    return 0;
}

int timer_delete(timer_t timerid)
{
    struct pse51_timer *timer;
    spl_t s;

    if (timerid < 0 || timerid >= PSE51_TIMER_MAX)
	{
        thread_set_errno(EINVAL);
	return -1;
	}

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
	{
	xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EINVAL);
	return -1;
	}

    xntimer_destroy(&timer->timerbase);
    removeq(&pse51_timerq,&timer->link);
    appendq(&timer_freeq,&timer->link);

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int timer_settime(timer_t timerid,
		  int flags,
		  const struct itimerspec *value,
		  struct itimerspec *ovalue)
{
    xnticks_t timeout, delay, interval;
    struct pse51_timer *timer;
    spl_t s;

    if (timerid < 0 || timerid >= PSE51_TIMER_MAX)
	{
        thread_set_errno(EINVAL);
	return -1;
	}

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
	{
	xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EINVAL);
	return -1;
	}

    timeout = ts2ticks_ceil(&value->it_value);

    if (flags & TIMER_ABSTIME)
	delay = timeout - xnpod_get_time();
    else
	delay = timeout;

    interval = ts2ticks_ceil(&value->it_interval);

    xntimer_start(&timer->timerbase,
		  delay <= timeout ? delay : delay - timeout,
		  interval);
    if (ovalue)
	*ovalue = timer->value;

    timer->value = *value;

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int timer_gettime(timer_t timerid, struct itimerspec *value)
{
    struct pse51_timer *timer;
    xnticks_t timeout;
    spl_t s;

    if (timerid < 0 || timerid >= PSE51_TIMER_MAX)
	{
        thread_set_errno(EINVAL);
	return -1;
	}

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
	{
	xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EINVAL);
	return -1;
	}

    timeout = xntimer_get_timeout(&timer->timerbase); /* relative only? */
    ticks2ts(&value->it_value,timeout);
    ticks2ts(&value->it_interval,xntimer_interval(&timer->timerbase));

    xnlock_put_irqrestore(&nklock, s);

    return 0;
}

int timer_getoverrun(timer_t timerid)
{
    struct pse51_timer *timer;
    int overruns;
    spl_t s;

    if (timerid < 0 || timerid >= PSE51_TIMER_MAX)
	{
        thread_set_errno(EINVAL);
	return -1;
	}

    xnlock_get_irqsave(&nklock, s);

    timer = &timer_pool[timerid];

    if (!xntimer_active_p(&timer->timerbase))
	{
	xnlock_put_irqrestore(&nklock, s);
        thread_set_errno(EINVAL);
	return -1;
	}

    overruns = timer->overruns;	/* FIXME: delay_max? */

    xnlock_put_irqrestore(&nklock, s);

    return overruns;
}

int pse51_timer_pkg_init(void)
{
    int n;

    initq(&timer_freeq);

    for (n = 0; n < PSE51_TIMER_MAX; n++)
	{
	inith(&timer_pool[n].link);
	appendq(&timer_freeq,&timer_pool[n].link);
	}

    initq(&pse51_timerq);

    return 0;
}

void pse51_timer_pkg_cleanup(void)
{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock, s);

    while ((holder = getheadq(&pse51_timerq)) != NULL)
        timer_delete(link2tm(holder) - timer_pool);

    xnlock_put_irqrestore(&nklock, s);
}

EXPORT_SYMBOL(timer_create);
EXPORT_SYMBOL(timer_delete);
EXPORT_SYMBOL(timer_settime);
EXPORT_SYMBOL(timer_gettime);
EXPORT_SYMBOL(timer_getoverrun);
