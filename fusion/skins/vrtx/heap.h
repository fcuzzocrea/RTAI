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

#ifndef _vrtx_heap_h
#define _vrtx_heap_h

#include "vrtx/defs.h"

#define VRTX_HEAP_MAGIC 0x82820505

typedef struct vrtxuslt { /* Region unit slot */

    unsigned prev : 15;
    unsigned next : 15;
    unsigned busy : 1;
    unsigned heading : 1;
    unsigned units;

} vrtxuslt_t;

#define heap_align_mask   (sizeof(vrtxuslt_t)-1)

typedef struct vrtxheap {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;  /* Link in vrtxheapq */

#define link2vrtxheap(laddr) \
((vrtxheap_t *)(((char *)laddr) - (int)(&((vrtxheap_t *)0)->link)))

    xnsynch_t synchbase;

    u_long log2psize;	/* Aligned allocation unit size */

    u_long allocated;	/* count of allocated blocks */

    u_long released;	/* count of allocated then released blocks */

    xnheap_t sysheap;	/* memory heap */

} vrtxheap_t;

#ifdef __cplusplus
extern "C" {
#endif

int vrtxheap_init(u_long heap0size);

void vrtxheap_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_vrtx_heap_h */
