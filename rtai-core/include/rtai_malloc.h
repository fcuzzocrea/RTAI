/*
 * Dynamic Memory Management for Real Time Linux.
 *
 * Copyright (©) 2000 Pierre Cloutier (Poseidon Controls Inc.),
 *                    Steve Papacharalambous (Zentropic Computing Inc.),
 *                    All rights reserved
 *
 * Authors:           Pierre Cloutier (pcloutier@poseidoncontrols.com)
 *                    Steve Papacharalambous (stevep@zentropix.com)
 *
 * Original date:     Mon 14 Feb 2000
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _RTAI_MEM_H
#define _RTAI_MEM_H

#include <rtai_types.h>

#ifdef __KERNEL__

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

void *rt_malloc(unsigned int size);

void rt_free(void *addr);

int rt_mem_init(void);

void rt_mem_end(void);

void rt_mmgr_stats(void);

void display_chunk(void *addr);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#undef DBG
#ifdef ZDEBUG
#define DBG(fmt, args...)  rtai_print_to_screen("<%s %d> " fmt, __FILE__, __LINE__ ,##args)
#else
#define DBG(fmt, args...)
#endif

#endif /* __KERNEL__ */

#endif  /* !_RTAI_MEM_H */
