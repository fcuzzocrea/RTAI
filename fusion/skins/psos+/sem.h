/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum <rpm@xenomai.org>.
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

#ifndef _psos_sem_h
#define _psos_sem_h

#include "psos+/defs.h"
#include "psos+/psos.h"

#define PSOS_SEM_MAGIC 0x81810202

typedef struct psossem {

    unsigned magic;   /* Magic code - must be first */

    xnholder_t link;  /* Link in psossemq */

#define link2psossem(laddr) \
((psossem_t *)(((char *)laddr) - (int)(&((psossem_t *)0)->link)))

    xnsynch_t synchbase;

    char name[5];     /* Name of semaphore */

    unsigned count;   /* Available resource count */

} psossem_t;

#ifdef __cplusplus
extern "C" {
#endif

void psossem_init(void);

void psossem_cleanup(void);

#ifdef __cplusplus
}
#endif

#endif /* !_psos_sem_h */
