/*
 * This file is part of the XENOMAI project.
 *
 * Copyright (C) 2001,2002 Philippe Gerum.
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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/fcntl.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <xenomai/pod.h>
#include <xenomai/heap.h>
#include <xenomai/fusion.h>
#include <xenomai/dbridge.h>

static dbridge_session_handler *dbridge_open_handler,
                               *dbridge_close_handler;

extern xnqueue_t dbridge_sleepq;

extern xnqueue_t dbridge_asyncq;

extern spinlock_t dbridge_sqlock;

extern spinlock_t dbridge_aqlock;

/* Real-time entry points. Remember that we _must_ enforce critical
   sections using splhigh/splexit since we might be competing with the
   kernel-based Xenomai domain threads for data access. */

void dbridge_msetup (dbridge_session_handler *open_handler,
		     dbridge_session_handler *close_handler)
{
    dbridge_open_handler = open_handler;
    dbridge_close_handler = close_handler;
}

int dbridge_mconnect (int minor,
		      dbridge_io_handler *o_handler,
		      dbridge_io_handler *i_handler,
		      dbridge_alloc_handler *alloc_handler,
		      void *cookie)
{
    dbridge_state_t *state;
    spl_t s;

    if (minor < 0 || minor >= DBRIDGE_MQ_NDEVS)
	return -ENODEV;

    state = &dbridge_states[minor];

    splhigh(s);

    if (testbits(state->status,DBRIDGE_RTK_CONN))
	{
	splexit(s);
	return -EBUSY;
	}

    setbits(state->status,DBRIDGE_RTK_CONN);

    splexit(s);

    xnsynch_init(&state->u.mq.synchbase,XNSYNCH_FIFO);
    state->u.mq.o_handler = o_handler;
    state->u.mq.i_handler = i_handler;
    state->u.mq.alloc_handler = alloc_handler;
    state->u.mq.cookie = cookie;

    if (testbits(state->status,DBRIDGE_USR_CONN))
	{
#ifdef DBRIDGE_DEBUG
	xnprintf("RTK: USR IS CONNECTED ON MINOR #%d\n",minor);
#endif
	if (testbits(state->status,DBRIDGE_USR_WOPEN|DBRIDGE_USR_WPOLL))
	    {
	    /* Wake up the userland thread waiting for the
	       Xenomai side to connect (open or poll). */
	    clrbits(state->status,DBRIDGE_USR_WOPEN|DBRIDGE_USR_WPOLL);
#ifdef DBRIDGE_DEBUG
	    xnprintf("RTK: WOPEN|WPOLL, KICKING MINOR #%d\n",minor);
#endif
	    dbridge_schedule_request();
	    }

	if (state->asyncq) /* Schedule asynch sig. */
	    {
#ifdef DBRIDGE_DEBUG
	    xnprintf("RTK: SIGIO, KICKING MINOR #%d\n",minor);
#endif
	    setbits(state->status,DBRIDGE_USR_SIGIO);
	    dbridge_schedule_request();
	    }
	}

    return 0;
}

int dbridge_mdisconnect (int minor)

{
    dbridge_state_t *state;
    xnholder_t *holder;
    spl_t s;

    if (minor < 0 || minor >= DBRIDGE_MQ_NDEVS)
	return -ENODEV;

    state = &dbridge_states[minor];

    splhigh(s);

    if (!testbits(state->status,DBRIDGE_RTK_CONN))
	{
	splexit(s);
	return -EBADF;
	}

    clrbits(state->status,DBRIDGE_RTK_CONN);

    splexit(s);

    if (testbits(state->status,DBRIDGE_USR_CONN))
	{
	while ((holder = getq(&state->u.mq.inq)) != NULL)
	    {
	    if (state->u.mq.i_handler != NULL)
		state->u.mq.i_handler(minor,link2mh(holder),1,state->u.mq.cookie);
	    else if (state->u.mq.alloc_handler == NULL)
		xnfree(link2mh(holder));
	    }

	splhigh(s);

	if (xnsynch_destroy(&state->u.mq.synchbase) == XNSYNCH_RESCHED)
	    xnpod_schedule(NULL);

	splexit(s);

	if (testbits(state->status,DBRIDGE_USR_WMASK))
	    {
	    /* Wake up the userland thread waiting for some operation
	       from the real-time kernel side (read/write or poll). */
	    clrbits(state->status,DBRIDGE_USR_WMASK);
	    dbridge_schedule_request();
	    }

	if (state->asyncq) /* Schedule asynch sig. */
	    {
	    setbits(state->status,DBRIDGE_USR_SIGIO);
	    dbridge_schedule_request();
	    }
	}

    return 0;
}

ssize_t dbridge_msend (int minor,
		       struct dbridge_mh *mh,
		       size_t size,
		       int flags)
{
    dbridge_state_t *state;
    spl_t s;

    if (minor < 0 || minor >= DBRIDGE_MQ_NDEVS)
	return -ENODEV;

    if (size <= sizeof(*mh))
	return -EINVAL;

    state = &dbridge_states[minor];

    if (!testbits(state->status,DBRIDGE_USR_CONN))
	return -EIO;

    splhigh(s);

    if (!testbits(state->status,DBRIDGE_RTK_CONN))
	{
	splexit(s);
	return -EBADF;
	}

    inith(dbridge_m_link(mh));
    dbridge_m_size(mh) = size - sizeof(*mh);

    if (flags & DBRIDGE_URGENT)
	prependq(&state->u.mq.outq,dbridge_m_link(mh));
    else
	appendq(&state->u.mq.outq,dbridge_m_link(mh));

    if (testbits(state->status,DBRIDGE_USR_CONN))
	{
	if (testbits(state->status,DBRIDGE_USR_WSEND))
	    {
	    /* Wake up the userland thread waiting for input
	       from the real-time kernel side. */
	    clrbits(state->status,DBRIDGE_USR_WSEND);
#ifdef DBRIDGE_DEBUG
	    xnprintf("RTK: WSEND, KICKING MINOR #%d\n",minor);
#endif
	    dbridge_schedule_request();
	    }
#ifdef DBRIDGE_DEBUG
	else
	    xnprintf("RTK: NO WSEND FOR MINOR #%d\n",minor);
#endif

	if (state->asyncq) /* Schedule asynch sig. */
	    {
	    setbits(state->status,DBRIDGE_USR_SIGIO);
	    dbridge_schedule_request();
	    }
	}
#ifdef DBRIDGE_DEBUG
    else
	xnprintf("RTK: USR NOT CONNECTED ON MINOR #%d\n",minor);
#endif

    splexit(s);

    return (ssize_t)size;
}

ssize_t dbridge_mreceive (int minor,
			  struct dbridge_mh **pmh,
			  xntime_t timeout)
{
    dbridge_state_t *state;
    xnholder_t *holder;
    xntime_t stime;
    spl_t s;

    if (minor < 0 || minor >= DBRIDGE_MQ_NDEVS)
	return -ENODEV;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    state = &dbridge_states[minor];

    splhigh(s);

    if (!testbits(state->status,DBRIDGE_RTK_CONN))
	{
	splexit(s);
	return -EBADF;
	}

    stime = xnpod_get_time();

    while ((holder = getq(&state->u.mq.inq)) == NULL)
	{
	if (timeout == XN_NONBLOCK)
	    {
	    splexit(s);
	    return -EWOULDBLOCK;
	    }

	if (timeout != XN_INFINITE)
	    {
	    xntime_t now = xnpod_get_time();

	    /* Compute the remaining time until the timeout
	       expires, bailing out if it has just elapsed. */

	    if (stime + timeout >= now)
		{
		splexit(s);
		return -ETIMEDOUT;
		}

	    timeout -= (now - stime);
	    stime = now;
	    }

	xnsynch_sleep_on(&state->u.mq.synchbase,timeout,NULL);

	if (xnthread_test_flags(xnpod_current_thread(),XNTIMEO))
	    {
	    splexit(s);
	    return -ETIMEDOUT;
	    }

	if (xnthread_test_flags(xnpod_current_thread(),XNBREAK))
	    {
	    splexit(s);
	    return -EAGAIN;
	    }

	if (xnthread_test_flags(xnpod_current_thread(),XNRMID))
	    {
	    splexit(s);
	    return -ESTALE;
	    }
	}

    splexit(s);

    *pmh = link2mh(holder);

    return (ssize_t)dbridge_m_size(*pmh);
}

int dbridge_minquire (int minor)

{
    if (minor < 0 || minor >= DBRIDGE_MQ_NDEVS)
	return -ENODEV;

    return dbridge_states[minor].status;
}

static int linux_msg_open (dbridge_state_t *state,
			   struct inode *inode,
			   struct file *file)
{
    int err = 0;
    spl_t s;

    /* Enforce exclusive open for the message queues. */
    if (testbits(state->status,DBRIDGE_USR_CONN))
	return -EBUSY;

    splhigh(s);

    clrbits(state->status,DBRIDGE_USR_WMASK|DBRIDGE_USR_SIGIO|DBRIDGE_USR_ONWAIT);
    setbits(state->status,DBRIDGE_USR_CONN);

    if (!testbits(state->status,DBRIDGE_RTK_CONN))
	{
	if (dbridge_open_handler)
	    {
	    splexit(s);

	    err = dbridge_open_handler(minor_from_state(state),NULL);

	    if (err != 0)
		{
		clrbits(state->status,DBRIDGE_USR_CONN);
		return err;
		}

	    if (testbits(state->status,DBRIDGE_RTK_CONN))
		return 0;

	    splhigh(s);
	    }

	if (testbits(file->f_flags,O_NONBLOCK))
	    {
	    splexit(s);
	    return -EWOULDBLOCK;
	    }

	dbridge_enqueue_wait(state,&state->open_sem,DBRIDGE_USR_WOPEN);

	splexit(s);

#ifdef DBRIDGE_DEBUG
	xnprintf("USR: WAITING FOR RT OPEN ON MINOR #%d\n",MINOR(inode->i_rdev));
#endif

	if (down_interruptible(&state->open_sem))
	    {
	    splhigh(s);
	    dbridge_dequeue_wait(state,DBRIDGE_USR_WOPEN);
	    splexit(s);
	    return -ERESTARTSYS;
	    }

#ifdef DBRIDGE_DEBUG
	xnprintf("USR: GOT OPEN ON MINOR #%d\n",MINOR(inode->i_rdev));
#endif
	}
    else
	{
	splexit(s);

	if (dbridge_open_handler)
	    err = dbridge_open_handler(minor_from_state(state),state->u.mq.cookie);
	}

    return err;
}

static int linux_msg_release (dbridge_state_t *state,
			      struct inode *inode,
			      struct file *file)
{
    xnholder_t *holder;
    int err = 0;
    spl_t s;

    splhigh(s);

    if (testbits(state->status,DBRIDGE_RTK_CONN))
	{
	int minor = minor_from_state(state);

	if (state->u.mq.o_handler != NULL)
	    {
	    while ((holder = getq(&state->u.mq.outq)) != NULL)
		state->u.mq.o_handler(minor,link2mh(holder),1,state->u.mq.cookie);
	    }

	splexit(s);

	if (dbridge_close_handler != NULL)
	    err = dbridge_close_handler(minor,state->u.mq.cookie);
	}
    else
	splexit(s);

    return err;
}

static ssize_t linux_msg_read (dbridge_state_t *state,
			       struct file *file,
			       char *buf,
			       size_t count,
			       loff_t *ppos)
{
    struct dbridge_mh *mh;
    xnholder_t *holder;
    ssize_t ret;
    spl_t s;

    if (!access_ok(VERIFY_WRITE,buf,count))
	return -EFAULT;

    if (!testbits(state->status,DBRIDGE_RTK_CONN))
	return -EIO;

    splhigh(s);

    /* Queue probe and proc enqueuing must be seen atomically,
       including from the real-time kernel side. */

    holder = getq(&state->u.mq.outq);
    mh = link2mh(holder);

    if (!mh)
	{
	if (file->f_flags & O_NONBLOCK)
	    {
	    splexit(s);
	    return -EAGAIN;
	    }

	dbridge_enqueue_wait(state,&state->send_sem,DBRIDGE_USR_WSEND);

	splexit(s);

#ifdef DBRIDGE_DEBUG
	xnprintf("USR: WAITING FOR RT SEND ON MINOR #%d\n",minor_from_state(state));
#endif

	if (down_interruptible(&state->send_sem))
	    {
	    splhigh(s);
	    dbridge_dequeue_wait(state,DBRIDGE_USR_WSEND);
	    splexit(s);
	    return -ERESTARTSYS;
	    }

	splhigh(s);

#ifdef DBRIDGE_DEBUG
	xnprintf("USR: GOT INPUT DATA ON MINOR #%d\n",minor_from_state(state));
#endif

	holder = getq(&state->u.mq.outq);
	mh = link2mh(holder);
	}

    splexit(s);

#ifdef DBRIDGE_DEBUG
    xnprintf("USR: RT-INPUT #%d QUEUE HAS %d ELEMENTS, MH %p\n",
	     minor_from_state(state),
	     countq(&state->u.mq.outq),mh);
#endif

    if (mh)
	{
	ret = (ssize_t)dbridge_m_size(mh); /* Cannot be zero */

	if (ret <= count)
	    __copy_to_user(buf,dbridge_m_data(mh),ret);
	else
	    /* Return buffer is too small - message is lost. */
	    ret = -ENOSPC;

	if (state->u.mq.o_handler != NULL)
	    state->u.mq.o_handler(minor_from_state(state),mh,ret < 0,state->u.mq.cookie);
	}
    else /* Closed by peer. */
	ret = 0;

    return ret;
}

static ssize_t linux_msg_write (dbridge_state_t *state,
				struct file *file,
				const char *buf, 
				size_t count,
				loff_t *ppos)
{
    struct dbridge_mh *mh;
    xnthread_t *sleeper;
    int err;
    spl_t s;

    if (!testbits(state->status,DBRIDGE_RTK_CONN))
	return -EIO;

    if (count == 0)
	return -EINVAL;

    if (!access_ok(VERIFY_READ,buf,count))
	return -EFAULT;

    if (state->u.mq.alloc_handler != NULL)
	mh = (struct dbridge_mh *)state->u.mq.alloc_handler(minor_from_state(state),
							    count + sizeof(*mh),
							    state->u.mq.cookie);
    else
	mh = (struct dbridge_mh *)xnmalloc(count + sizeof(*mh));

    if (!mh)
	/* Cannot sleep. */
	return -ENOMEM;

    inith(dbridge_m_link(mh));
    dbridge_m_size(mh) = count;
    __copy_from_user(dbridge_m_data(mh),buf,count);

    if (state->u.mq.i_handler != NULL)
	{
	err = state->u.mq.i_handler(minor_from_state(state),mh,0,state->u.mq.cookie);

	if (err != 0)
	    count = (size_t)err;
	}
    else
	{
	splhigh(s);

	appendq(&state->u.mq.inq,&mh->link);

	/* If a real-time kernel thread is waiting on this input
	   queue, wake it up now. */

	if (xnsynch_nsleepers(&state->u.mq.synchbase) > 0)
	    {
	    sleeper = xnsynch_wakeup_one_sleeper(&state->u.mq.synchbase);
#ifdef DBRIDGE_DEBUG
	    xnprintf("USR: WAKING UP RT ON MINOR #%d (thread %s)\n",
		     minor_from_state(state),
		     xnthread_name(sleeper));
#endif
	    xnpod_schedule(NULL);
	    }

	splexit(s);
	}

    return (ssize_t)count;
}

static int linux_msg_ioctl (dbridge_state_t *state,
			    struct inode *inode,
			    struct file *file,
			    unsigned int cmd,
			    unsigned long arg)
{
    return -ENOSYS;
}

static int linux_msg_fasync (dbridge_state_t *state,
			     int fd,
			     struct file *file,
			     int on)
{
    int ret, queued;
    u_long slflags;

    queued = (state->asyncq != NULL);
    ret = fasync_helper(fd,file,on,&state->asyncq);

    if (state->asyncq)
	{
	if (!queued)
	    {
	    spin_lock_irqsave(&dbridge_aqlock,slflags);
	    appendq(&dbridge_asyncq,&state->alink);
	    spin_unlock_irqrestore(&dbridge_aqlock,slflags);
	    }
	}
    else if (queued)
	    {
	    spin_lock_irqsave(&dbridge_aqlock,slflags);
	    removeq(&dbridge_asyncq,&state->alink);
	    spin_unlock_irqrestore(&dbridge_aqlock,slflags);
	    }

    return ret;
}

static unsigned linux_msg_poll (dbridge_state_t *state,
			       struct file *file,
			       poll_table *pt)
{
    unsigned mask = 0;
    spl_t s;

    poll_wait(file,&state->pollq,pt);

    if (testbits(state->status,DBRIDGE_RTK_CONN))
	mask |= (POLLOUT|POLLWRNORM);

    if (countq(&state->u.mq.outq) > 0)
	mask |= (POLLIN|POLLRDNORM);

    if (!mask && !testbits(state->status,DBRIDGE_USR_WPOLL))
	{
	/* Procs which have issued a timed out poll req will remain
	   linked to the sleepers queue, and will be silently unlinked
	   the next time the real-time kernel side kicks
	   dbridge_wakeup_proc. */
	splhigh(s);
	dbridge_enqueue_wait(state,NULL,DBRIDGE_USR_WPOLL);
	splexit(s);
	}

    return mask;
}

dbridge_ops_t dbridge_msg_ops = {
    open:	&linux_msg_open,
    close:	&linux_msg_release,
    read:	&linux_msg_read,
    write:	&linux_msg_write,
    ioctl:	&linux_msg_ioctl,
    fasync:	&linux_msg_fasync,
    poll:	&linux_msg_poll
};

int linux_msg_init (void)

{
    dbridge_state_t *state;

    for (state = &dbridge_states[0];
	 state < &dbridge_states[DBRIDGE_MQ_NDEVS]; state++)
	{
	initq(&state->u.mq.inq);
	initq(&state->u.mq.outq);
	state->u.mq.o_handler = NULL;
	state->u.mq.i_handler = NULL;
	state->u.mq.alloc_handler = NULL;
	state->ops = &dbridge_msg_ops;
	}

    return 0;
}

void linux_msg_exit (void) {
}

EXPORT_SYMBOL(dbridge_mconnect);
EXPORT_SYMBOL(dbridge_mdisconnect);
EXPORT_SYMBOL(dbridge_msend);
EXPORT_SYMBOL(dbridge_mreceive);
EXPORT_SYMBOL(dbridge_minquire);
EXPORT_SYMBOL(dbridge_msetup);
