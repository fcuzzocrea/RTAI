/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 *
 * RTAI is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
 * or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
 * License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * This file provides the interface to the RTAI dynamic memory
 * allocator based on the algorithm described in "Design of a General
 * Purpose Memory Allocator for the 4.3BSD Unix Kernel" by Marshall
 * K. McKusick and Michael J. Karels.
 */

#ifndef _COMPAT_RTAI_MEM_MGR_H
#define _COMPAT_RTAI_MEM_MGR_H

#include <nucleus/heap.h>


#define rt_alloc(size)	xnheap_alloc(&kheap,size)
#define rt_free(ptr)	xnheap_free(&kheap,ptr)

/*
 * TODO: 
extern void display_chunk(void *addr);
extern int rt_mem_init(void);
extern void rt_mem_end(void);
extern void rt_mmgr_stats(void);
*/


#endif /* !_COMPAT_RTAI_MEM_MGR_H */
