/**
 * @file
 * This file is part of the RTAI project.
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
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

#ifndef _RTAI_HEAP_H
#define _RTAI_HEAP_H

#include <nucleus/synch.h>
#include <nucleus/heap.h>
#include <rtai/types.h>

struct RT_TASK;

/* Creation flags. */
#define H_PRIO   XNSYNCH_PRIO	/* Pend by task priority order. */
#define H_FIFO   XNSYNCH_FIFO	/* Pend by FIFO order. */
#define H_DMA    0x100		/* Use memory suitable for DMA. */
#define H_SHARED 0x200		/* Use mappable shared memory. */

typedef struct rt_heap_info {

    int nwaiters;		/* !< Number of pending tasks. */

    int mode;			/* !< Creation mode. */

    size_t heapsize;		/* !< Size of heap memory. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_HEAP_INFO;

typedef struct rt_heap_placeholder {

    rt_handle_t opaque;

    void *opaque2;

    caddr_t mapbase;

    size_t mapsize;

} RT_HEAP_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_HEAP_MAGIC 0x55550808

typedef struct rt_heap {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    xnheap_t heap_base;	/* !< Internal heap object. */

    int mode;		/* !< Creation mode. */

    void *shm_block;	/* !< Single shared block (H_SHARED only) */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_HEAP;

#ifdef __cplusplus
extern "C" {
#endif

int __heap_pkg_init(void);

void __heap_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_HEAP_PLACEHOLDER RT_HEAP;

int rt_heap_bind(RT_HEAP *heap,
		 const char *name);

int rt_heap_unbind(RT_HEAP *heap);

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_heap_create(RT_HEAP *heap,
		   const char *name,
		   size_t heapsize,
		   int mode);

int rt_heap_delete(RT_HEAP *heap);

int rt_heap_alloc(RT_HEAP *heap,
		  size_t size,
		  RTIME timeout,
		  void **blockp);

int rt_heap_free(RT_HEAP *heap,
		 void *block);

int rt_heap_inquire(RT_HEAP *heap,
		    RT_HEAP_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_HEAP_H */
