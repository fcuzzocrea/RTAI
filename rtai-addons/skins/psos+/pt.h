/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
 *
 * Xenomai is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * Xenomai is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Xenomai; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _psos_pt_h
#define _psos_pt_h

#include "psos+/defs.h"
#include "psos+/rtai_psos.h"

#define PSOS_PT_MAGIC 0x81810404

#define pt_align_mask   (sizeof(void *)-1)

#define pt_bitmap_pos(pt,n) \
pt->bitmap[((n) / (sizeof(u_long) * XN_NBBY))]

#define pt_block_pos(n) \
(1 << ((n) % (sizeof(u_long) * XN_NBBY)))

#define pt_bitmap_setbit(pt,n) \
(pt_bitmap_pos(pt,n) |= pt_block_pos(n))

#define pt_bitmap_clrbit(pt,n) \
(pt_bitmap_pos(pt,n) &= ~pt_block_pos(n))

#define pt_bitmap_tstbit(pt,n) \
(pt_bitmap_pos(pt,n) & pt_block_pos(n))

typedef struct psospt {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;  /* Link in psosptq */

#define link2psospt(laddr) \
((psospt_t *)(((char *)laddr) - (int)(&((psospt_t *)0)->link)))

    char name[XNOBJECT_NAME_LEN]; /* Symbolic name of partition */

    u_long flags;

    u_long bsize;	/* (Aligned) Block size */

    u_long psize;	/* Size of storage space */

    u_long nblks;	/* Number of data blocks */

    u_long ublks;	/* Number of used blocks */

    void *freelist;	/* Free block list head */

    char *data;		/* Pointer to the user space behind the bitmap */

    u_long bitmap[1];	/* Start of bitmap -- keeps alignment */

} psospt_t;

#ifdef __cplusplus
extern "C" {
#endif

void psospt_init(void);

void psospt_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_psos_pt_h */
