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

#ifndef _xenomai_dbridge_h
#define _xenomai_dbridge_h

#undef DBRIDGE_DEBUG

#define DBRIDGE_WAIT   0x0
#define DBRIDGE_NOWAIT 0x1

#define DBRIDGE_NORMAL 0x0
#define DBRIDGE_URGENT 0x1

#define DBRIDGE_RTK_CONN   0x1
#define DBRIDGE_USR_CONN   0x2
#define DBRIDGE_USR_WOPEN  0x4
#define DBRIDGE_USR_WSEND  0x8
#define DBRIDGE_USR_WPOLL  0x10
#define DBRIDGE_USR_SIGIO  0x20
#define DBRIDGE_USR_ONWAIT 0x40

#define DBRIDGE_USR_WMASK \
(DBRIDGE_USR_WOPEN|DBRIDGE_USR_WSEND|DBRIDGE_USR_WPOLL)

#define DBRIDGE_MQ_NDEVS     8	/* message channels */
#define DBRIDGE_EV_NDEVS     8	/* event flags */
#define DBRIDGE_NDEVS        (DBRIDGE_MQ_NDEVS + DBRIDGE_EV_NDEVS + 1)
#define DBRIDGE_MAJOR        150

#define XIOCURTMODE  0xfbfb

#ifdef __KERNEL__

#include <xenomai/queue.h>
#include <xenomai/synch.h>
#include <xenomai/thread.h>
#include <linux/types.h>
#include <linux/poll.h>

typedef struct dbridge_mh {

    xnholder_t link;
#define link2mh(laddr) \
(laddr ? ((dbridge_mh_t *)(((char *)laddr) - (int)(&((dbridge_mh_t *)0)->link))) : 0)
    unsigned size;
    
} dbridge_mh_t;

typedef int dbridge_io_handler(int minor,
			       struct dbridge_mh *mh,
			       int onerror,
			       void *cookie);

typedef int dbridge_session_handler(int minor,
				    void *cookie);

typedef void *dbridge_alloc_handler(int minor,
				    size_t size,
				    void *cookie);
struct _dbridge_state;

typedef struct _dbridge_ops {

    int (*open)(struct _dbridge_state *state,
		struct inode *inode,
		struct file *file);

    int (*close)(struct _dbridge_state *state,
		 struct inode *inode,
		 struct file *file);

    ssize_t (*read)(struct _dbridge_state *state,
		    struct file *file,
		    char *buf,
		    size_t count,
		    loff_t *ppos);

    ssize_t (*write)(struct _dbridge_state *state,
		     struct file *file,
		     const char *buf,
		     size_t count,
		     loff_t *ppos);

    int (*ioctl)(struct _dbridge_state *state,
		 struct inode *inode,
		 struct file *file,
		 unsigned cmd,
		 unsigned long arg);

    int (*fasync)(struct _dbridge_state *state,
		  int fd,
		  struct file *file,
		  int on);

    unsigned (*poll)(struct _dbridge_state *state,
		     struct file *file,
		     poll_table *pt);
} dbridge_ops_t;

typedef struct _dbridge_state {

    dbridge_ops_t *ops;
    xnholder_t slink;	/* Link on sleep queue */
    xnholder_t alink;	/* Link on async queue */
#define link2dbs(laddr,link) \
((struct _dbridge_state *)(((char *)laddr) - (int)(&((struct _dbridge_state *)0)->link)))

    union {
	struct {
	    xnqueue_t inq;	/* From user-space to kernel */
	    xnqueue_t outq;	/* From kernel to user-space */
	    dbridge_io_handler *o_handler;
	    dbridge_io_handler *i_handler;
	    dbridge_alloc_handler *alloc_handler;
	    xnsynch_t synchbase;
	    void *cookie;
	} mq;

	struct {
	    unsigned long events;
	    xnsynch_t synchbase;
	} ev;
    } u;

    /* Linux kernel part */
    int status;
    struct semaphore open_sem;
    struct semaphore send_sem;
    struct semaphore event_sem;
    struct semaphore *wchan;	/* any sem */
    struct fasync_struct *asyncq;
    wait_queue_head_t pollq;

} dbridge_state_t;

extern dbridge_state_t dbridge_states[];

#define minor_from_state(s) (s - dbridge_states)

static inline void dbridge_schedule_request (void) {
    extern unsigned dbridge_wakeup_srq;
    rt_pend_linux_srq(dbridge_wakeup_srq);
}

#ifdef __cplusplus
extern "C" {
#endif

void dbridge_enqueue_wait(dbridge_state_t *state,
			  struct semaphore *wchan,
			  int flags);

void dbridge_dequeue_wait(dbridge_state_t *state,
			  int flags);

/* Entry points of the kernel interface. */

void dbridge_msetup(dbridge_session_handler *open_handler,
		    dbridge_session_handler *close_handler);

int dbridge_mconnect(int minor,
		     dbridge_io_handler *o_handler,
		     dbridge_io_handler *i_handler,
		     dbridge_alloc_handler *alloc_handler,
		     void *cookie);

int dbridge_mdisconnect(int minor);

ssize_t dbridge_msend(int minor,
		      struct dbridge_mh *mh,
		      size_t size,
		      int flags);

ssize_t dbridge_mrecv(int minor,
		      struct dbridge_mh **pmh,
		      xntime_t timeout);

int dbridge_minquire(int minor);

#ifdef __cplusplus
}
#endif

static inline xnholder_t *dbridge_m_link(dbridge_mh_t *mh) {
    return &mh->link;
}

#else  /* !__KERNEL__ */

typedef struct dbridge_mh {

    char __opaque[8];
    unsigned size;

} dbridge_mh_t;

#endif /* __KERNEL__ */

static inline char *dbridge_m_data(dbridge_mh_t *mh) {
    return (char *)(mh + 1);
}

#define dbridge_m_size(mh) ((mh)->size)

#endif /* !_xenomai_dbridge_h */
