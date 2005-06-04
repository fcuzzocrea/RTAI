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
#include <rtai/syscall.h>
#include <rtai/task.h>

extern int __rtai_muxid;

int rt_timer_start (RTIME tickval)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_timer_start,
			     &tickval);
}

void rt_timer_stop (void)

{
    XENOMAI_SKINCALL0(__rtai_muxid,
		      __rtai_timer_stop);
}

RTIME rt_timer_read (void)

{
    RTIME now;

    XENOMAI_SKINCALL1(__rtai_muxid,
		      __rtai_timer_read,
		      &now);
    return now;
}

RTIME rt_timer_tsc (void)

{
    RTIME tsc;

#ifdef CONFIG_RTAI_HW_DIRECT_TSC
    tsc = __xn_rdtsc();
#else /* !CONFIG_RTAI_HW_DIRECT_TSC */
    XENOMAI_SKINCALL1(__rtai_muxid,
		      __rtai_timer_tsc,
		      &tsc);
#endif /* CONFIG_RTAI_HW_DIRECT_TSC */

    return tsc;
}

SRTIME rt_timer_ns2ticks (SRTIME ns)

{
    RTIME ticks;

    XENOMAI_SKINCALL2(__rtai_muxid,
		      __rtai_timer_ns2ticks,
		      &ticks,
		      &ns);
    return ticks;
}

SRTIME rt_timer_ticks2ns (SRTIME ticks)

{
    SRTIME ns;

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
