/*
 * Copyright (C) 2001,2002,2003 Philippe Gerum.
 *
 * RAI/fusion is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published
 * by the Free Software Foundation, Inc., 675 Mass Ave, Cambridge MA
 * 02139, USA; either version 2 of the License, or (at your option)
 * any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _RTAI_NUCLEUS_PIPE_H
#define _RTAI_NUCLEUS_PIPE_H

#include <nucleus/queue.h>

#define XNPIPE_WAIT   0x0
#define XNPIPE_NOWAIT 0x1

#define XNPIPE_NORMAL 0x0
#define XNPIPE_URGENT 0x1

#define XNPIPE_NDEVS      CONFIG_RTAI_OPT_PIPE_NRDEV
#define XNPIPE_DEV_MAJOR  150

#define XNPIPE_KERN_CONN   0x1
#define XNPIPE_USER_CONN   0x2
#define XNPIPE_USER_WOPEN  0x4
#define XNPIPE_USER_WSEND  0x8
#define XNPIPE_USER_WPOLL  0x10
#define XNPIPE_USER_SIGIO  0x20
#define XNPIPE_USER_ONWAIT 0x40

#define XNPIPE_USER_WMASK \
(XNPIPE_USER_WOPEN|XNPIPE_USER_WSEND|XNPIPE_USER_WPOLL)

typedef struct xnpipe_mh {

    xnholder_t link;
    unsigned size;
    
} xnpipe_mh_t;

static inline xnpipe_mh_t *link2mh (xnholder_t *laddr)
{
    return laddr ? ((xnpipe_mh_t *)(((char *)laddr) - (int)(&((xnpipe_mh_t *)0)->link))) : 0;
}

#ifdef __KERNEL__

#include <nucleus/synch.h>
#include <nucleus/thread.h>
#include <linux/types.h>
#include <linux/poll.h>

typedef int xnpipe_io_handler(int minor,
				struct xnpipe_mh *mh,
				int onerror,
				void *cookie);

typedef int xnpipe_session_handler(int minor,
				     void *cookie);

typedef void *xnpipe_alloc_handler(int minor,
				     size_t size,
				     void *cookie);
typedef struct xnpipe_state {

    xnholder_t slink;	/* Link on sleep queue */
    xnholder_t alink;	/* Link on async queue */
#define link2xnpipe(laddr,link) \
((struct xnpipe_state *)(((char *)laddr) - (int)(&((struct xnpipe_state *)0)->link)))

    xnqueue_t inq;	/* From user-space to kernel */
    xnqueue_t outq;	/* From kernel to user-space */
    xnpipe_io_handler *output_handler;
    xnpipe_io_handler *input_handler;
    xnpipe_alloc_handler *alloc_handler;
    xnsynch_t synchbase;
    void *cookie;

    /* Linux kernel part */
    xnflags_t status;
    struct semaphore open_sem;
    struct semaphore send_sem;
    struct semaphore *wchan;	/* any sem */
    struct fasync_struct *asyncq;
    wait_queue_head_t pollq;

} xnpipe_state_t;

extern xnpipe_state_t xnpipe_states[];

#define xnminor_from_state(s) (s - xnpipe_states)

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

int xnpipe_mount(void);

void xnpipe_umount(void);

/* Entry points of the kernel interface. */

void xnpipe_setup(xnpipe_session_handler *open_handler,
		  xnpipe_session_handler *close_handler);

int xnpipe_connect(int minor,
		   xnpipe_io_handler *output_handler,
		   xnpipe_io_handler *input_handler,
		   xnpipe_alloc_handler *alloc_handler,
		   void *cookie);

int xnpipe_disconnect(int minor);

ssize_t xnpipe_send(int minor,
		    struct xnpipe_mh *mh,
		    size_t size,
		    int flags);

ssize_t xnpipe_recv(int minor,
		    struct xnpipe_mh **pmh,
		    xnticks_t timeout);

int xnpipe_inquire(int minor);

#ifdef __cplusplus
}
#endif /* __cplusplus */

static inline xnholder_t *xnpipe_m_link(xnpipe_mh_t *mh) {
    return &mh->link;
}

#endif /* __KERNEL__ */

static inline char *xnpipe_m_data(xnpipe_mh_t *mh) {
    return (char *)(mh + 1);
}

#define xnpipe_m_size(mh) ((mh)->size)

#endif /* !_RTAI_NUCLEUS_PIPE_H */
