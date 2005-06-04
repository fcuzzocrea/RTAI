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

#include <sys/types.h>
#include <sys/mman.h>
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <limits.h>
#include <posix/posix.h>
#include <posix/syscall.h>

int __pse51_muxid = -1;

int __init_skin (void)

{
    int muxid;

    muxid = XENOMAI_SYSCALL2(__xn_sys_bind,PSE51_SKIN_MAGIC,NULL); /* atomic */

    if (muxid < 0)
	return -1;

    __pse51_muxid = muxid;

    return muxid;
}
