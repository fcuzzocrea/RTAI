/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA 02139,
 * USA; either version 2 of the License, or (at your option) any later
 * version.
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

#ifndef _RTAI_NUCLEUS_DBRIDGE_H
#define _RTAI_NUCLEUS_DBRIDGE_H

#include <nucleus/queue.h>

#undef XNBRIDGE_DEBUG

#define XNBRIDGE_WAIT   0x0
#define XNBRIDGE_NOWAIT 0x1

#define XNBRIDGE_NORMAL 0x0
#define XNBRIDGE_URGENT 0x1

#define XNBRIDGE_NDEVS  32
#define XNBRIDGE_MAJOR  150

#define XNBRIDGE_KERN_CONN   0x1
#define XNBRIDGE_USER_CONN   0x2
#define XNBRIDGE_USER_WOPEN  0x4
#define XNBRIDGE_USER_WSEND  0x8
#define XNBRIDGE_USER_WPOLL  0x10
#define XNBRIDGE_USER_SIGIO  0x20
#define XNBRIDGE_USER_ONWAIT 0x40

#define XNBRIDGE_USER_WMASK \
(XNBRIDGE_USER_WOPEN|XNBRIDGE_USER_WSEND|XNBRIDGE_USER_WPOLL)

typedef struct xnbridge_mh {

    xnholder_t link;
#define link2mh(laddr) \
(laddr ? ((struct xnbridge_mh *)(((char *)laddr) - (int)(&((struct xnbridge_mh *)0)->link))) : 0)
    unsigned size;
    
} xnbridge_mh_t;

#ifdef __KERNEL__

#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <linux/types.h>
#include <linux/poll.h>

typedef int xnbridge_io_handler(int minor,
				struct xnbridge_mh *mh,
				int onerror,
				void *cookie);

typedef int xnbridge_session_handler(int minor,
				     void *cookie);

typedef void *xnbridge_alloc_handler(int minor,
				     size_t size,
				     void *cookie);
typedef struct xnbridge_state {

    xnholder_t slink;	/* Link on sleep queue */
    xnholder_t alink;	/* Link on async queue */
#define link2dbs(laddr,link) \
((struct xnbridge_state *)(((char *)laddr) - (int)(&((struct xnbridge_state *)0)->link)))

    xnqueue_t inq;	/* From user-space to kernel */
    xnqueue_t outq;	/* From kernel to user-space */
    xnbridge_io_handler *output_handler;
    xnbridge_io_handler *input_handler;
    xnbridge_alloc_handler *alloc_handler;
    xnsynch_t synchbase;
    void *cookie;

    /* Linux kernel part */
    int status;
    struct semaphore open_sem;
    struct semaphore send_sem;
    struct semaphore *wchan;	/* any sem */
    struct fasync_struct *asyncq;
    wait_queue_head_t pollq;

} xnbridge_state_t;

extern xnbridge_state_t xnbridge_states[];

#define minor_from_state(s) (s - xnbridge_states)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xnbridge_init(void);

void xnbridge_exit(void);

/* Entry points of the kernel interface. */

void xnbridge_msetup(xnbridge_session_handler *open_handler,
		     xnbridge_session_handler *close_handler);

int xnbridge_mconnect(int minor,
		      xnbridge_io_handler *output_handler,
		      xnbridge_io_handler *input_handler,
		      xnbridge_alloc_handler *alloc_handler,
		      void *cookie);

int xnbridge_mdisconnect(int minor);

ssize_t xnbridge_msend(int minor,
		       struct xnbridge_mh *mh,
		       size_t size,
		       int flags);

ssize_t xnbridge_mrecv(int minor,
		       struct xnbridge_mh **pmh,
		       xnticks_t timeout);

int xnbridge_minquire(int minor);

#ifdef __cplusplus
}
#endif /* __cplusplus */

static inline xnholder_t *xnbridge_m_link(xnbridge_mh_t *mh) {
    return &mh->link;
}

#endif /* __KERNEL__ */

static inline char *xnbridge_m_data(xnbridge_mh_t *mh) {
    return (char *)(mh + 1);
}

#define xnbridge_m_size(mh) ((mh)->size)

#endif /* !_xenomai_bridge_h */
