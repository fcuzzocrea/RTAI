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

#ifndef _vrtx_pt_h
#define _vrtx_pt_h

#include "vrtx/defs.h"

#define VRTX_PT_MAGIC 0x82820404

#define ptext_align_mask   (sizeof(void *)-1)

#define ptext_bitmap_pos(ptext,n) \
ptext->bitmap[((n) / (sizeof(u_long) * XN_NBBY))]

#define ptext_block_pos(n) \
(1 << ((n) % (sizeof(u_long) * XN_NBBY)))

#define ptext_bitmap_setbit(ptext,n) \
(ptext_bitmap_pos(ptext,n) |= ptext_block_pos(n))

#define ptext_bitmap_clrbit(ptext,n) \
(ptext_bitmap_pos(ptext,n) &= ~ptext_block_pos(n))

#define ptext_bitmap_tstbit(ptext,n) \
(ptext_bitmap_pos(ptext,n) & ptext_block_pos(n))

typedef struct vrtxptext {

    xnholder_t link;	/* Link in vrtxpt->extq */

#define link2vrtxptext(laddr) \
((vrtxptext_t *)(((char *)laddr) - (int)(&((vrtxptext_t *)0)->link)))

    void *freelist;	/* Free block list head */

    char *data;		/* Pointer to the user space behind the bitmap */

    u_long nblks;	/* Number of data blocks */

    u_long extsize;	/* Size of storage space */

    u_long bitmap[1];	/* Start of bitmap -- keeps alignment */

} vrtxptext_t;

typedef struct vrtxpt {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;  /* Link in vrtxptq */

    xnqueue_t extq;   /* Linked list of active extents */

#define link2vrtxpt(laddr) \
((vrtxpt_t *)(((char *)laddr) - (int)(&((vrtxpt_t *)0)->link)))

    int pid;		/* Partition identifier */

    u_long bsize;	/* (Aligned) Block size */

    u_long ublks;	/* Overall number of used blocks */
    u_long fblks;	/* Overall number of free blocks */

} vrtxpt_t;

#ifdef __cplusplus
extern "C" {
#endif

void vrtxpt_init(void);

void vrtxpt_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_vrtx_pt_h */
