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

#ifndef _RTAI_TIMER_H
#define _RTAI_TIMER_H

#include <nucleus/timer.h>
#include <rtai/types.h>

#define TM_UNSET   XNPOD_NO_TICK
#define TM_ONESHOT XNPOD_APERIODIC_TICK

typedef struct rt_timer_info {

    RTIME period;	/* <! Current status (unset, aperiodic, period). */
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
