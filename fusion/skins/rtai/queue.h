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

#ifndef _RTAI_QUEUE_H
#define _RTAI_QUEUE_H

#include <nucleus/synch.h>
#include <nucleus/heap.h>
#include <rtai/types.h>

struct RT_TASK;

/* Creation flags. */
#define Q_PRIO  XNSYNCH_PRIO	/* Pend by task priority order. */
#define Q_FIFO  XNSYNCH_FIFO	/* Pend by FIFO order. */
#define Q_DMA   0x100		/* Use memory suitable for DMA. */

#define Q_UNLIMITED 0		/* No size limit. */

/* Operation flags. */
#define Q_URGENT     0x1
#define Q_BROADCAST  0x2

typedef struct rt_queue_info {

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_QUEUE_INFO;

typedef struct rt_queue_placeholder {
    rt_handle_t opaque;
    caddr_t mapbase;
} RT_QUEUE_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_QUEUE_MAGIC 0x55550707

typedef struct rt_queue {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    xnqueue_t pendq;	/* !< Pending message queue. */

    xnheap_t bufpool;		/* !< Message buffer pool. */

    int mode;			/* !< Creation mode. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    int qmax;		/* !< Maximum queued elements. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_QUEUE;

typedef struct rt_queue_msg {

    size_t size;

    volatile unsigned refcount;

    xnholder_t link;

#define link2rtmsg(laddr) \
((rt_queue_msg_t *)(((char *)laddr) - (int)(&((rt_queue_msg_t *)0)->link)))

} rt_queue_msg_t;

#ifdef __cplusplus
extern "C" {
#endif

int __queue_pkg_init(void);

void __queue_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_QUEUE_PLACEHOLDER RT_QUEUE;

int rt_queue_bind(RT_QUEUE *q,
		  const char *name);

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_queue_create(RT_QUEUE *q,
		    const char *name,
		    size_t poolsize,
		    unsigned qmax,
		    int mode);

int rt_queue_delete(RT_QUEUE *q);

int rt_queue_inquire(RT_QUEUE *q,
		     RT_QUEUE_INFO *info);

int rt_queue_send(RT_QUEUE *q,
		  void *buf,
		  int mode);

ssize_t rt_queue_recv(RT_QUEUE *q,
		      void **bufp,
		      RTIME timeout);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_QUEUE_H */
