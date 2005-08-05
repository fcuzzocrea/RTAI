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

#include <malloc.h>
#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <rtai/syscall.h>
#include <rtai/task.h>

pthread_key_t __rtai_tskey;

int __rtai_muxid = -1;

static void __flush_tsd (void *tsd)

{
    /* Free the task descriptor allocated by rt_task_self(). */
    free(tsd);
}

static __attribute__((constructor)) void __init_rtai_interface(void)

{
    int muxid;

    muxid = XENOMAI_SYSCALL2(__xn_sys_bind,RTAI_SKIN_MAGIC,NULL); /* atomic */

    if (muxid < 0)
	{
	fprintf(stderr,"RTAI/fusion: native skin or user-space support unavailable.\n");
	fprintf(stderr,"(did you load the rtai_native.ko module?)\n");
	exit(1);
	}

    /* Allocate a TSD key for indexing self task pointers. */

    if (pthread_key_create(&__rtai_tskey,&__flush_tsd) != 0)
	{
	fprintf(stderr,"RTAI/fusion: failed to allocate new TSD key?!\n");
	exit(1);
	}

    __rtai_muxid = muxid;
}
