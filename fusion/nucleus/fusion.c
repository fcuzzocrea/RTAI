/*
 * Copyright (C) 2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation; either version 2 of the License,
 * or (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <nucleus/pod.h>
#include <nucleus/fusion.h>

static xnpod_t __fusion_pod;

static int xnfusion_unload_hook (void)

{
    /* If no thread is hosted by the fusion pod, unload it. We are
       called with interrupts off, nklock locked. */

    if (nkpod == &__fusion_pod && countq(&nkpod->threadq) == 0)
	{
	xnfusion_umount();
	return 1;
	}

    return 0;
}

int xnfusion_attach (void)

{
    if (nkpod)
	{
	if (nkpod != &__fusion_pod)
	    return -ENOSYS;

	return 0;
	}

    if (xnpod_init(&__fusion_pod,FUSION_MIN_PRIO,FUSION_MAX_PRIO,0) != 0)
	return -ENOSYS;

    __fusion_pod.svctable.unload = &xnfusion_unload_hook;

    return 0;
}

int xnfusion_mount (void)

{
    return 0;
}

int xnfusion_umount (void)

{
    if (nkpod != &__fusion_pod)
	return -ENOSYS;

    xnpod_shutdown(XNPOD_NORMAL_EXIT);

    return 0;
}

EXPORT_SYMBOL(xnfusion_attach);
