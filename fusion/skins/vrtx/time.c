/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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
 * along with RTAI/fusion; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "rtai_config.h"
#include "vrtx/task.h"

#define TEN_POW_9 1000000000ULL

void ui_timer (void) {
    xnpod_announce_tick(&nkclock);
}

void sc_gclock (struct timespec *timep, unsigned long *nsp, int *errp)
{
    unsigned long remain;
    xnticks_t now;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    *nsp = xnpod_get_tickval();

    now = xnpod_get_time ();

    timep->seconds = xnarch_ulldiv(now, 1000000000, &remain);
    timep->nanoseconds = remain;

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;
}

void sc_sclock (struct timespec time, unsigned long ns, int *errp)
{
    spl_t s;

    if ( (ns > 1000000000) ||
	 ( (time.nanoseconds < 0) || (time.nanoseconds > 999999999) ) )
	{
	*errp = ER_IIP;
	return;
	}

    xnlock_get_irqsave(&nklock,s);

    if ( (ns != xnpod_get_tickval()) )
	{
	xnpod_stop_timer();
	if (ns != 0)
	    {
	    xnpod_start_timer(ns, &xnpod_announce_tick);
	    }
	}

    xnpod_set_time(time.seconds * TEN_POW_9 + time.nanoseconds);

    xnlock_put_irqrestore(&nklock,s);

    *errp = RET_OK;
}

unsigned long sc_gtime(void)
{
    return (unsigned long)xnpod_get_time ();
}

void sc_stime(long time)
{
    xnpod_set_time (time);
}

void sc_delay (long ticks)
{
    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if (ticks > 0)
	{
	vrtx_current_task()->vrtxtcb.TCBSTAT = TBSDELAY;	
	xnpod_delay(ticks);
	}
    else
	xnpod_yield(); /* Perform manual round-robin */
}

void sc_adelay (struct timespec time, int *errp)
{
    xnticks_t now, etime;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    if ( (time.nanoseconds < 0) || (time.nanoseconds > 999999999) )
	{
	*errp = ER_IIP;
	return;
	}

    etime = time.seconds * TEN_POW_9 + time.nanoseconds;
    now = xnpod_get_time();
    *errp = RET_OK;

    if (etime > now)
	{
	vrtx_current_task()->vrtxtcb.TCBSTAT = TBSADELAY;
	xnpod_delay(etime - now);
	}
    else
	xnpod_yield(); /* Perform manual round-robin */
}
