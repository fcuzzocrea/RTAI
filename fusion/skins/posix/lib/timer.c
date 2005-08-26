/*
 * Copyright (C) 2005 Philippe Gerum <rpm@xenomai.org>.
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

#include <errno.h>
#include <posix/lib/pthread.h>
#include <posix/syscall.h>

extern int __pse51_muxid;

int __wrap_timer_create (clockid_t clockid,
			 struct sigevent *evp,
			 timer_t *timerid)
{
    return -XENOMAI_SKINCALL2(__pse51_muxid,
			      __pse51_timer_create,
			      evp,
			      timerid);
}

int __wrap_timer_delete (timer_t timerid)
{
    return -1;
}

int __wrap_timer_settime(timer_t timerid,
			 int flags,
			 const struct itimerspec *value,
			 struct itimerspec *ovalue)
{
    return -1;
}

int __wrap_timer_gettime(timer_t timerid,
			 struct itimerspec *value)
{
    return -1;
}

int __wrap_timer_getoverrun(timer_t timerid)
{
    return -1;
}
