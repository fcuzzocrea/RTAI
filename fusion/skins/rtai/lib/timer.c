/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.

 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA.
 */

#include <sys/types.h>
#include <sys/mman.h>
#include <stdio.h>
#include <memory.h>
#include <errno.h>
#include <unistd.h>
#include <limits.h>
#include <rtai/syscall.h>
#include <rtai/task.h>

extern int __rtai_muxid;

static inline int __init_skin (void)

{
    __rtai_muxid = XENOMAI_SYSCALL2(__xn_sys_attach,RTAI_SKIN_MAGIC,NULL);
    return __rtai_muxid;
}

int rt_timer_start (RTIME tickval)

{
    if (__rtai_muxid < 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_timer_start,
			     &tickval);
}

void rt_timer_stop (void)

{
    if (__rtai_muxid < 0 && __init_skin() < 0)
	return;

    XENOMAI_SKINCALL0(__rtai_muxid,
		      __rtai_timer_stop);
}

RTIME rt_timer_read (void)

{
    RTIME now;

    if (__rtai_muxid < 0 && __init_skin() < 0)
	return 0;

    XENOMAI_SKINCALL1(__rtai_muxid,
		      __rtai_timer_read,
		      &now);
    return now;
}

RTIME rt_timer_tsc (void)

{
    RTIME tsc;

    if (__rtai_muxid < 0 && __init_skin() < 0)
	return 0;

    XENOMAI_SKINCALL1(__rtai_muxid,
		      __rtai_timer_tsc,
		      &tsc);
    return tsc;
}

RTIME rt_timer_ns2ticks (RTIME ns)

{
    RTIME ticks;

    if (__rtai_muxid < 0 && __init_skin() < 0)
	return 0;

    XENOMAI_SKINCALL2(__rtai_muxid,
		      __rtai_timer_ns2ticks,
		      &ticks,
		      &ns);
    return ticks;
}

RTIME rt_timer_ticks2ns (RTIME ticks)

{
    RTIME ns;

    if (__rtai_muxid < 0 && __init_skin() < 0)
	return 0;

    XENOMAI_SKINCALL2(__rtai_muxid,
		      __rtai_timer_ticks2ns,
		      &ns,
		      &ticks);
    return ns;
}

int rt_timer_inquire (RT_TIMER_INFO *info)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_timer_inquire,
			     info);
}
