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
#include <rtai/event.h>

extern int __rtai_muxid;

int __init_skin(void);

int rt_event_create (RT_EVENT *event,
		     const char *name,
		     unsigned long ivalue,
		     int mode)
{
    if (__rtai_muxid < 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL4(__rtai_muxid,
			     __rtai_event_create,
			     event,
			     name,
			     ivalue,
			     mode);
}

int rt_event_bind (RT_EVENT *event,
		   const char *name)
{
    if (__rtai_muxid < 0 && __init_skin() < 0)
	return -ENOSYS;

    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_event_bind,
			     event,
			     name);
}

int rt_event_delete (RT_EVENT *event)

{
    return XENOMAI_SKINCALL1(__rtai_muxid,
			     __rtai_event_delete,
			     event);
}

int rt_event_wait (RT_EVENT *event,
                   unsigned long mask,
                   unsigned long *mask_r,
                   int mode,
		   RTIME timeout)
{
    return XENOMAI_SKINCALL5(__rtai_muxid,
			     __rtai_event_wait,
			     event,
			     mask,
			     mask_r,
			     mode,
			     &timeout);
}

int rt_event_signal (RT_EVENT *event,
		     unsigned long mask)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_event_signal,
			     event,
			     mask);
}

int rt_event_inquire (RT_EVENT *event,
		      RT_EVENT_INFO *info)
{
    return XENOMAI_SKINCALL2(__rtai_muxid,
			     __rtai_event_inquire,
			     event,
			     info);
}
