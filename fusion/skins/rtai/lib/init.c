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
#include <memory.h>
#include <malloc.h>
#include <unistd.h>
#include <limits.h>
#include <rtai/syscall.h>
#include <rtai/task.h>

pthread_key_t __rtai_tskey;

int __rtai_muxid = -1;

static void __flush_tsd (void *tsd)

{
    /* Free the task descriptor allocated by rt_task_self(). */
    free(tsd);
}

int __init_skin (void)

{
    __rtai_muxid = XENOMAI_SYSCALL2(__xn_sys_attach,RTAI_SKIN_MAGIC,NULL); /* atomic */

    /* Allocate a TSD key for indexing self task pointers. */

    if (__rtai_muxid >= 0 &&
	pthread_key_create(&__rtai_tskey,&__flush_tsd) != 0)
	return -1;

    return __rtai_muxid;
}
