/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
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
#include "vrtx/task.h"

#define TEN_POW_9 1000000000ULL

void ui_timer (void) {
    xnpod_announce_tick(&nkclock,1);
}

void sc_gclock (struct timespec *timep, unsigned long *nsp, int *errp)
{
    unsigned long remain;
    xntime_t now;

    xnmutex_lock(&__imutex);

    *nsp = xnpod_get_tickval();

    now = xnpod_get_time ();

    timep->seconds = xnarch_ulldiv(now, 1000000000, &remain);
    timep->nanoseconds = remain;

    xnmutex_unlock(&__imutex);

    *errp = RET_OK;
}

void sc_sclock (struct timespec time, unsigned long ns, int *errp)
{
    if ( (ns > 1000000000) ||
	 ( (time.nanoseconds < 0) || (time.nanoseconds > 999999999) ) )
	{
	*errp = ER_IIP;
	return;
	}

    xnmutex_lock(&__imutex);

    if ( (ns != xnpod_get_tickval()) )
	{
	xnpod_stop_timer();
	if (ns != 0)
	    {
	    xnpod_start_timer(ns, &xnpod_announce_tick);
	    }
	}

    xnpod_set_time(time.seconds * TEN_POW_9 + time.nanoseconds);

    xnmutex_unlock(&__imutex);

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
    xntime_t now, etime;

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
