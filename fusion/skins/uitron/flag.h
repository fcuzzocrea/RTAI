/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _uITRON_flag_h
#define _uITRON_flag_h

#include "uitron/defs.h"

#define uITRON_FLAG_MAGIC 0x85850303

typedef struct uiflag {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;	/* Link in uiflagq */

#define link2uiflag(laddr) \
((uiflag_t *)(((char *)laddr) - (int)(&((uiflag_t *)0)->link)))

    ID flgid;

    VP exinf;

    ATR flgatr;

    UINT flgvalue;

    xnsynch_t synchbase;

} uiflag_t;

#ifdef __cplusplus
extern "C" {
#endif

void uiflag_init(void);

void uiflag_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_uITRON_flag_h */
