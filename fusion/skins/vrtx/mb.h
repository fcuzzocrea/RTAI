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

#ifndef _vrtx_mb_h
#define _vrtx_mb_h

#include "vrtx/defs.h"

typedef struct vrtxmsg {
    /* BEWARE, code assumes link is the first element */
    xnholder_t link;

    xnsynch_t  synchbase;
    char **mboxp;
} vrtxmsg_t;

#ifdef __cplusplus
extern "C" {
#endif

void vrtxmb_init(void);

void vrtxmb_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_vrtx_mb_h */
