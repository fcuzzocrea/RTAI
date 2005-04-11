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
 */

#ifndef _RTAI_TIMER_H
#define _RTAI_TIMER_H

#include <nucleus/timer.h>
#include <rtai/types.h>

#define TM_UNSET   XN_NO_TICK
#define TM_ONESHOT XN_APERIODIC_TICK

typedef struct rt_timer_info {

    RTIME period;	/* !< Current status (unset, aperiodic, period). */
    RTIME date;		/* !< Current date. */

} RT_TIMER_INFO;

#ifdef __cplusplus
extern "C" {
#endif

SRTIME rt_timer_ns2ticks(SRTIME ns);

SRTIME rt_timer_ticks2ns(SRTIME ticks);

int rt_timer_inquire(RT_TIMER_INFO *info);

RTIME rt_timer_read(void);

RTIME rt_timer_tsc(void);

void rt_timer_spin(RTIME ns);

int rt_timer_start(RTIME nstick);

void rt_timer_stop(void);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_TIMER_H */
