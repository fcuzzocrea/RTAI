/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Gilles Chanteperdrix <gilles.chanteperdrix@laposte.net>.
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
