/*
 * Copyright (C) 2001,2002 IDEALX (http://www.idealx.com/).
 * Written by Julien Pinon <jpinon@idealx.com>.
 * Copyright (C) 2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _vrtx_task_h
#define _vrtx_task_h

#include "vrtx/defs.h"

#define VRTX_TASK_MAGIC 0x82820101

typedef struct vrtxtask {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;	/* Link in vrtxtaskq */

#define link2vrtxtask(laddr) \
((vrtxtask_t *)(((char *)laddr) - (int)(&((vrtxtask_t *)0)->link)))

    xnthread_t threadbase;

#define thread2vrtxtask(taddr) \
((taddr) ? ((vrtxtask_t *)(((char *)taddr) - (int)(&((vrtxtask_t *)0)->threadbase))) : NULL)

    int tid;

    void (*entry)(void *cookie);

    char *param;

    u_long paramsz;

    TCB vrtxtcb; /* Fake VRTX task control block for sc_tinquiry() */

    union { /* Saved args for current synch. wait operation */

	struct {
	    int opt;
	    int mask;
	} evgroup;

	char *qmsg;

	struct {
	    u_long size;
	    void *chunk;
	} heap;

    } waitargs;

} vrtxtask_t;

#define vrtx_current_task() thread2vrtxtask(xnpod_current_thread())

#ifdef __cplusplus
extern "C" {
#endif

void vrtxtask_init(u_long stacksize);

void vrtxtask_cleanup(void);

void vrtxtask_delete_internal(vrtxtask_t *task);

#ifdef __cplusplus
}
#endif

#endif /* !_vrtx_task_h */
