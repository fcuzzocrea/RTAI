/*
 * Copyright (C) 1999-2003 Paolo Mantegazza <mantegazza@aero.polimi.it>
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

#ifndef _COMPAT_RTAI_SCHED_H

#include_next <rtai_sched.h>

#define RT_HIGHEST_PRIORITY RT_SCHED_HIGHEST_PRIORITY
#define RT_LOWEST_PRIORITY  RT_SCHED_LOWEST_PRIORITY
#define RT_LINUX_PRIORITY   RT_SCHED_LINUX_PRIORITY

#define READY      RT_SCHED_READY
#define SUSPENDED  RT_SCHED_SUSPENDED
#define DELAYED    RT_SCHED_DELAYED
#define SEMAPHORE  RT_SCHED_SEMAPHORE
#define SEND       RT_SCHED_SEND
#define RECEIVE    RT_SCHED_RECEIVE
#define RPC        RT_SCHED_RPC
#define RETURN     RT_SCHED_RETURN
#define MBXSUSP    RT_SCHED_MBXSUSP

#define UP_SCHED   RT_SCHED_UP
#define SMP_SCHED  RT_SCHED_SMP
#define MUP_SCHED  RT_SCHED_MUP

#endif /* !_COMPAT_RTAI_SCHED_H */
