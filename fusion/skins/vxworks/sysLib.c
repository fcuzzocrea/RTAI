/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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
#include "vxworks/defs.h"

static u_long nstick;
static wind_tick_handler_t tick_handler;
static int tick_handler_arg;




void tickAnnounce(void)
{
    if(tick_handler != NULL)
        tick_handler(tick_handler_arg);

    xnpod_announce_tick(&nkclock);
}


static int __tickAnnounce(xnintr_t *intr)
{
    tickAnnounce();
    return XN_ISR_HANDLED;
}


int wind_sysclk_init(u_long init_ticks)
{
    tick_handler = NULL;
    return xnpod_start_timer(nstick = init_ticks, (xnisr_t)&__tickAnnounce);
}


void wind_sysclk_cleanup(void)
{
    xnpod_stop_timer();
}


STATUS sysClkConnect (wind_tick_handler_t func, int arg )
{
    spl_t s;

    if(func == NULL)
        return ERROR;

    xnlock_get_irqsave(&nklock, s);
    tick_handler = func;
    tick_handler_arg = arg;
    xnlock_put_irqrestore(&nklock, s);

    return OK;
}


void sysClkDisable (void)
{
    xnpod_stop_timer();
}


void sysClkEnable (void)
{
    xnpod_start_timer(nstick, &__tickAnnounce);
}


int sysClkRateGet (void)
{
    return nstick;
}


STATUS sysClkRateSet (int new_rate)
{
    spl_t s;
    int err;
    
    if(new_rate <= 0)
        return ERROR;
    
    xnlock_get_irqsave(&nklock, s);
    xnpod_stop_timer();
    err = xnpod_start_timer(nstick = new_rate, &__tickAnnounce);
    xnlock_put_irqrestore(&nklock, s);

    return err == 0 ? OK : ERROR;
}
