/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum.
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
#include <linux/poll.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>
#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <nucleus/pipe.h>

static int xnpipe_asyncsig = SIGIO;

static xnpipe_session_handler *xnpipe_open_handler,
                              *xnpipe_close_handler;

xnpipe_state_t xnpipe_states[XNPIPE_NDEVS];

xnqueue_t xnpipe_sleepq;

xnqueue_t xnpipe_asyncq;

spinlock_t xnpipe_sqlock;

spinlock_t xnpipe_aqlock;

unsigned xnpipe_wakeup_srq;

/*
 * Attempt to wake up Linux-side sleepers upon pipe
 * availability/readability conditions.  This routine is
 * asynchronously kicked from kernel space and only deals with
 * Linux-related variables.
 */

static void xnpipe_wakeup_proc (void)

{
    xnholder_t *holder, *nholder;
    xnpipe_state_t *state;
    unsigned long slflags;
    spl_t s;

#ifdef XNPIPE_DEBUG
    xnprintf("WAKEUP PROC ACTIVATED: SLEEPQ HAS %d ELEMENTS\n",
	     countq(&xnpipe_sleepq));
#endif

    spin_lock_irqsave(&xnpipe_sqlock,slflags);

    nholder = getheadq(&xnpipe_sleepq);

    while ((holder = nholder) != NULL)
	{
	state = link2xnpipe(holder,slink);

	/* Wake up the sleepers whose suspension flag disappeared. */

#ifdef XNPIPE_DEBUG
	xnprintf("MINOR #%d FOUND ON SLEEPQ\n",xnminor_from_state(state));
#endif

	if (!testbits(state->status,XNPIPE_USER_WMASK))
	    {
	    nholder = popq(&xnpipe_sleepq,holder);
	    clrbits(state->status,XNPIPE_USER_ONWAIT);

	    spin_unlock_irqrestore(&xnpipe_sqlock,slflags);

	    if (state->wchan)
		{
#ifdef XNPIPE_DEBUG
		xnprintf("AWAKENING MINOR #%d ON SEM\n",xnminor_from_state(state));
#endif
		up(state->wchan);
		state->wchan = NULL;
		}
	    else if (waitqueue_active(&state->pollq))
		{
#ifdef XNPIPE_DEBUG
		xnprintf("AWAKENING MINOR #%d ON POLLQ\n",xnminor_from_state(state));
#endif
		wake_up_interruptible(&state->pollq);
		}

	    spin_lock_irqsave(&xnpipe_sqlock,slflags);

	    set_need_resched();
	    }
	else
	    nholder = nextq(&xnpipe_sleepq,holder);
	}

    spin_unlock_irqrestore(&xnpipe_sqlock,slflags);

    /* Scan the async queue, sending the proper signal to
       subscribers. */

    spin_lock_irqsave(&xnpipe_aqlock,slflags);

    holder = getheadq(&xnpipe_asyncq);

    while (holder != NULL)
	{
	state = link2xnpipe(holder,alink);

	xnlock_get_irqsave(&nklock,s);

	if (testbits(state->status,XNPIPE_USER_SIGIO))
	    {
	    clrbits(state->status,XNPIPE_USER_SIGIO);
	    xnlock_put_irqrestore(&nklock,s);
	    kill_fasync(&state->asyncq,xnpipe_asyncsig,POLL_IN);
	    set_need_resched();
	    }
	else
	    xnlock_put_irqrestore(&nklock,s);

	holder = nextq(&xnpipe_asyncq,holder);
	}

    spin_unlock_irqrestore(&xnpipe_aqlock,slflags);
}

static inline void xnpipe_enqueue_wait (xnpipe_state_t *state,
					struct semaphore *wchan,
					int flags)
{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);
    state->wchan = wchan;	/* may be NULL */
    setbits(state->status,flags|XNPIPE_USER_ONWAIT);
    appendq(&xnpipe_sleepq,&state->slink);
    xnlock_put_irqrestore(&nklock,s);
}

static inline void xnpipe_dequeue_wait (xnpipe_state_t *state,
					int flags)
{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);
    removeq(&xnpipe_sleepq,&state->slink);
    clrbits(state->status,flags|XNPIPE_USER_ONWAIT);
    xnlock_put_irqrestore(&nklock,s);
}

static inline void xnpipe_schedule_request (void) {
    rthal_pend_linux_srq(xnpipe_wakeup_srq);
}

/* Real-time entry points. Remember that we _must_ enforce critical
   sections since we might be competing with the real-time threads for
   data access. */

void xnpipe_setup (xnpipe_session_handler *open_handler,
		   xnpipe_session_handler *close_handler)
{
    xnpipe_open_handler = open_handler;
    xnpipe_close_handler = close_handler;
}

int xnpipe_connect (int minor,
		    xnpipe_io_handler *output_handler,
		    xnpipe_io_handler *input_handler,
		    xnpipe_alloc_handler *alloc_handler,
		    void *cookie)
{
    xnpipe_state_t *state;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    state = &xnpipe_states[minor];

    xnlock_get_irqsave(&nklock,s);

    if (testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EBUSY;
	}

    setbits(state->status,XNPIPE_KERN_CONN);

    xnlock_put_irqrestore(&nklock,s);

    xnsynch_init(&state->synchbase,XNSYNCH_FIFO);
    state->output_handler = output_handler;
    state->input_handler = input_handler;
    state->alloc_handler = alloc_handler;
    state->cookie = cookie;

    if (testbits(state->status,XNPIPE_USER_CONN))
	{
#ifdef XNPIPE_DEBUG
	xnprintf("RTK: USR IS CONNECTED ON MINOR #%d\n",minor);
#endif
	if (testbits(state->status,XNPIPE_USER_WOPEN|XNPIPE_USER_WPOLL))
	    {
	    /* Wake up the userland thread waiting for the
	       Xenomai side to connect (open or poll). */
	    clrbits(state->status,XNPIPE_USER_WOPEN|XNPIPE_USER_WPOLL);
#ifdef XNPIPE_DEBUG
	    xnprintf("RTK: WOPEN|WPOLL, KICKING MINOR #%d\n",minor);
#endif
	    xnpipe_schedule_request();
	    }

	if (state->asyncq) /* Schedule asynch sig. */
	    {
#ifdef XNPIPE_DEBUG
	    xnprintf("RTK: SIGIO, KICKING MINOR #%d\n",minor);
#endif
	    setbits(state->status,XNPIPE_USER_SIGIO);
	    xnpipe_schedule_request();
	    }
	}

    return 0;
}

int xnpipe_disconnect (int minor)

{
    xnpipe_state_t *state;
    xnholder_t *holder;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    state = &xnpipe_states[minor];

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EBADF;
	}

    clrbits(state->status,XNPIPE_KERN_CONN);

    xnlock_put_irqrestore(&nklock,s);

    if (testbits(state->status,XNPIPE_USER_CONN))
	{
	while ((holder = getq(&state->inq)) != NULL)
	    {
	    if (state->input_handler != NULL)
		state->input_handler(minor,link2mh(holder),1,state->cookie);
	    else if (state->alloc_handler == NULL)
		xnfree(link2mh(holder));
	    }

	xnlock_get_irqsave(&nklock,s);

	if (xnsynch_destroy(&state->synchbase) == XNSYNCH_RESCHED)
	    xnpod_schedule();

	xnlock_put_irqrestore(&nklock,s);

	if (testbits(state->status,XNPIPE_USER_WMASK))
	    {
	    /* Wake up the userland thread waiting for some operation
	       from the kernel side (read/write or poll). */
	    clrbits(state->status,XNPIPE_USER_WMASK);
	    xnpipe_schedule_request();
	    }

	if (state->asyncq) /* Schedule asynch sig. */
	    {
	    setbits(state->status,XNPIPE_USER_SIGIO);
	    xnpipe_schedule_request();
	    }
	}

    return 0;
}

ssize_t xnpipe_send (int minor,
		     struct xnpipe_mh *mh,
		     size_t size,
		     int flags)
{
    xnpipe_state_t *state;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    if (size <= sizeof(*mh))
	return -EINVAL;

    state = &xnpipe_states[minor];

    if (!testbits(state->status,XNPIPE_USER_CONN))
	return -EPIPE;

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EBADF;
	}

    inith(xnpipe_m_link(mh));
    xnpipe_m_size(mh) = size - sizeof(*mh);

    if (flags & XNPIPE_URGENT)
	prependq(&state->outq,xnpipe_m_link(mh));
    else
	appendq(&state->outq,xnpipe_m_link(mh));

    if (testbits(state->status,XNPIPE_USER_CONN))
	{
	if (testbits(state->status,XNPIPE_USER_WSEND))
	    {
	    /* Wake up the userland thread waiting for input
	       from the kernel side. */
	    clrbits(state->status,XNPIPE_USER_WSEND);
#ifdef XNPIPE_DEBUG
	    xnprintf("RTK: WSEND, KICKING MINOR #%d\n",minor);
#endif
	    xnpipe_schedule_request();
	    }
#ifdef XNPIPE_DEBUG
	else
	    xnprintf("RTK: NO WSEND FOR MINOR #%d\n",minor);
#endif

	if (state->asyncq) /* Schedule asynch sig. */
	    {
	    setbits(state->status,XNPIPE_USER_SIGIO);
	    xnpipe_schedule_request();
	    }
	}
#ifdef XNPIPE_DEBUG
    else
	xnprintf("RTK: USR NOT CONNECTED ON MINOR #%d\n",minor);
#endif

    xnlock_put_irqrestore(&nklock,s);

    return (ssize_t)size;
}

ssize_t xnpipe_recv (int minor,
		     struct xnpipe_mh **pmh,
		     xnticks_t timeout)
{
    xnpipe_state_t *state;
    xnholder_t *holder;
    xnticks_t stime;
    ssize_t ret;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    state = &xnpipe_states[minor];

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	ret = -EBADF;
	goto unlock_and_exit;
	}

    stime = xnpod_get_time();

    while ((holder = getq(&state->inq)) == NULL)
	{
	if (timeout == XN_NONBLOCK)
	    {
	    ret = -EWOULDBLOCK;
	    goto unlock_and_exit;
	    }

	if (timeout != XN_INFINITE)
	    {
	    xnticks_t now = xnpod_get_time();

	    /* Compute the remaining time until the timeout
	       expires, bailing out if it has just elapsed. */

	    if (stime + timeout >= now)
		{
		ret = -ETIMEDOUT;
		goto unlock_and_exit;
		}

	    timeout -= (now - stime);
	    stime = now;
	    }

	xnsynch_sleep_on(&state->synchbase,timeout);

	if (xnthread_test_flags(xnpod_current_thread(),XNTIMEO))
	    {
	    ret = -ETIMEDOUT;
	    goto unlock_and_exit;
	    }

	if (xnthread_test_flags(xnpod_current_thread(),XNBREAK))
	    {
	    ret = -EINTR;
	    goto unlock_and_exit;
	    }

	if (xnthread_test_flags(xnpod_current_thread(),XNRMID))
	    {
	    ret = -ESTALE;
	    goto unlock_and_exit;
	    }
	}

    *pmh = link2mh(holder);

    ret = (ssize_t)xnpipe_m_size(*pmh);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return ret;
}

int xnpipe_inquire (int minor)

{
    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    return xnpipe_states[minor].status;
}

/*
 * Open the pipe from user-space.
 */

static int xnpipe_open (struct inode *inode,
			struct file *file)
{
    xnpipe_state_t *state;
    int minor, err = 0;
    spl_t s;

#ifdef XNPIPE_DEBUG
    xnprintf("USR: OPENING MINOR #%d\n",MINOR(inode->i_rdev));
#endif

    minor = MINOR(inode->i_rdev);

    if (minor >= XNPIPE_NDEVS)
	return -ENXIO; /* TssTss... stop playing with mknod() ;o) */

    state = &xnpipe_states[minor];

    file->private_data = state;
    sema_init(&state->open_sem,0);
    sema_init(&state->send_sem,0);
    init_waitqueue_head(&state->pollq);
    state->wchan = NULL;

    /* Enforce exclusive open for the message queues. */
    if (testbits(state->status,XNPIPE_USER_CONN))
	return -EBUSY;

    xnlock_get_irqsave(&nklock,s);

    clrbits(state->status,XNPIPE_USER_WMASK|XNPIPE_USER_SIGIO|XNPIPE_USER_ONWAIT);
    setbits(state->status,XNPIPE_USER_CONN);

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	if (xnpipe_open_handler)
	    {
	    xnlock_put_irqrestore(&nklock,s);

	    err = xnpipe_open_handler(xnminor_from_state(state),NULL);

	    if (err != 0)
		{
		clrbits(state->status,XNPIPE_USER_CONN);
		return err;
		}

	    if (testbits(state->status,XNPIPE_KERN_CONN))
		return 0;

	    xnlock_get_irqsave(&nklock,s);
	    }

	if (testbits(file->f_flags,O_NONBLOCK))
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    return -EWOULDBLOCK;
	    }

	xnpipe_enqueue_wait(state,&state->open_sem,XNPIPE_USER_WOPEN);

	xnlock_put_irqrestore(&nklock,s);

#ifdef XNPIPE_DEBUG
	xnprintf("USR: WAITING FOR RT OPEN ON MINOR #%d\n",MINOR(inode->i_rdev));
#endif

	if (down_interruptible(&state->open_sem))
	    {
	    xnpipe_dequeue_wait(state,XNPIPE_USER_WOPEN);
	    return -ERESTARTSYS;
	    }

#ifdef XNPIPE_DEBUG
	xnprintf("USR: GOT OPEN ON MINOR #%d\n",MINOR(inode->i_rdev));
#endif
	}
    else
	{
	xnlock_put_irqrestore(&nklock,s);

	if (xnpipe_open_handler)
	    err = xnpipe_open_handler(xnminor_from_state(state),state->cookie);
	}

    return err;
}

static int xnpipe_release (struct inode *inode,
			   struct file *file)
{
    xnpipe_state_t *state;
    unsigned long slflags;
    xnholder_t *holder;
    int err = 0;
    spl_t s;

#ifdef XNPIPE_DEBUG
    xnprintf("USR: CLOSING MINOR #%d\n",MINOR(inode->i_rdev));
#endif

    state = (xnpipe_state_t *)file->private_data;

    xnlock_get_irqsave(&nklock,s);

    if (testbits(state->status,XNPIPE_USER_ONWAIT))
	removeq(&xnpipe_sleepq,&state->slink);

    clrbits(state->status,XNPIPE_USER_CONN|XNPIPE_USER_WMASK);

    if (testbits(state->status,XNPIPE_KERN_CONN))
	{
	int minor = xnminor_from_state(state);

	if (state->output_handler != NULL)
	    {
	    while ((holder = getq(&state->outq)) != NULL)
		state->output_handler(minor,link2mh(holder),1,state->cookie);
	    }

	xnlock_put_irqrestore(&nklock,s);

	if (xnpipe_close_handler != NULL)
	    err = xnpipe_close_handler(minor,state->cookie);
	}
    else
	xnlock_put_irqrestore(&nklock,s);

    if (waitqueue_active(&state->pollq))
	wake_up_interruptible(&state->pollq);

    if (state->asyncq) /* Clear the async queue */
	{
	spin_lock_irqsave(&xnpipe_aqlock,slflags);
	removeq(&xnpipe_asyncq,&state->alink);
	spin_unlock_irqrestore(&xnpipe_aqlock,slflags);
	fasync_helper(-1,file,0,&state->asyncq);
	}

    set_need_resched();

    return err;
}

static ssize_t xnpipe_read (struct file *file,
			    char *buf,
			    size_t count,
			    loff_t *ppos)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    struct xnpipe_mh *mh;
    xnholder_t *holder;
    ssize_t ret;
    spl_t s;

    if (!access_ok(VERIFY_WRITE,buf,count))
	return -EFAULT;

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	return -EPIPE;

    xnlock_get_irqsave(&nklock,s);

    /* Queue probe and proc enqueuing must be seen atomically,
       including from the real-time kernel side. */

    holder = getq(&state->outq);
    mh = link2mh(holder);

    if (!mh)
	{
	if (file->f_flags & O_NONBLOCK)
	    {
	    xnlock_put_irqrestore(&nklock,s);
	    return -EAGAIN;
	    }

	xnpipe_enqueue_wait(state,&state->send_sem,XNPIPE_USER_WSEND);

	xnlock_put_irqrestore(&nklock,s);

#ifdef XNPIPE_DEBUG
	xnprintf("USR: WAITING FOR RT SEND ON MINOR #%d\n",xnminor_from_state(state));
#endif

	if (down_interruptible(&state->send_sem))
	    {
	    xnpipe_dequeue_wait(state,XNPIPE_USER_WSEND);
	    return -ERESTARTSYS;
	    }

	xnlock_get_irqsave(&nklock,s);

#ifdef XNPIPE_DEBUG
	xnprintf("USR: GOT INPUT DATA ON MINOR #%d\n",xnminor_from_state(state));
#endif

	holder = getq(&state->outq);
	mh = link2mh(holder);
	}

    xnlock_put_irqrestore(&nklock,s);

#ifdef XNPIPE_DEBUG
    xnprintf("USR: RT-INPUT #%d QUEUE HAS %d ELEMENTS, MH %p\n",
	     xnminor_from_state(state),
	     countq(&state->outq),mh);
#endif

    if (mh)
	{
	ret = (ssize_t)xnpipe_m_size(mh); /* Cannot be zero */

	if (ret <= count)
	    __copy_to_user(buf,xnpipe_m_data(mh),ret);
	else
	    /* Return buffer is too small - message is lost. */
	    ret = -ENOSPC;

	if (state->output_handler != NULL)
	    state->output_handler(xnminor_from_state(state),mh,ret < 0,state->cookie);
	}
    else /* Closed by peer. */
	ret = 0;

    return ret;
}

static ssize_t xnpipe_write (struct file *file,
			     const char *buf, 
			     size_t count,
			     loff_t *ppos)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    struct xnpipe_mh *mh;
    xnthread_t *sleeper;
    int err;
    spl_t s;

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	return -EPIPE;

    if (count == 0)
	return -EINVAL;

    if (!access_ok(VERIFY_READ,buf,count))
	return -EFAULT;

    if (state->alloc_handler != NULL)
	mh = (struct xnpipe_mh *)state->alloc_handler(xnminor_from_state(state),
							count + sizeof(*mh),
							state->cookie);
    else
	mh = (struct xnpipe_mh *)xnmalloc(count + sizeof(*mh));

    if (!mh)
	/* Cannot sleep. */
	return -ENOMEM;

    inith(xnpipe_m_link(mh));
    xnpipe_m_size(mh) = count;
    __copy_from_user(xnpipe_m_data(mh),buf,count);

    if (state->input_handler != NULL)
	{
	err = state->input_handler(xnminor_from_state(state),mh,0,state->cookie);

	if (err != 0)
	    count = (size_t)err;
	}
    else
	{
	xnlock_get_irqsave(&nklock,s);

	appendq(&state->inq,&mh->link);

	/* If a real-time kernel thread is waiting on this input
	   queue, wake it up now. */

	if (xnsynch_nsleepers(&state->synchbase) > 0)
	    {
	    sleeper = xnsynch_wakeup_one_sleeper(&state->synchbase);
#ifdef XNPIPE_DEBUG
	    xnprintf("USR: WAKING UP RT ON MINOR #%d (thread %s)\n",
		     xnminor_from_state(state),
		     xnthread_name(sleeper));
#endif
	    xnpod_schedule();
	    }

	xnlock_put_irqrestore(&nklock,s);
	}

    return (ssize_t)count;
}

static int xnpipe_ioctl (struct inode *inode,
			 struct file *file,
			 unsigned int cmd,
			 unsigned long arg)
{
    return -ENOSYS;
}

static int xnpipe_fasync (int fd,
			  struct file *file,
			  int on)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    unsigned long slflags;
    int ret, queued;

    queued = (state->asyncq != NULL);
    ret = fasync_helper(fd,file,on,&state->asyncq);

    if (state->asyncq)
	{
	if (!queued)
	    {
	    spin_lock_irqsave(&xnpipe_aqlock,slflags);
	    appendq(&xnpipe_asyncq,&state->alink);
	    spin_unlock_irqrestore(&xnpipe_aqlock,slflags);
	    }
	}
    else if (queued)
	    {
	    spin_lock_irqsave(&xnpipe_aqlock,slflags);
	    removeq(&xnpipe_asyncq,&state->alink);
	    spin_unlock_irqrestore(&xnpipe_aqlock,slflags);
	    }

    return ret;
}

static unsigned xnpipe_poll (struct file *file,
			     poll_table *pt)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    unsigned mask = 0;

    poll_wait(file,&state->pollq,pt);

    if (testbits(state->status,XNPIPE_KERN_CONN))
	mask |= (POLLOUT|POLLWRNORM);

    if (countq(&state->outq) > 0)
	mask |= (POLLIN|POLLRDNORM);

    if (!mask && !testbits(state->status,XNPIPE_USER_WPOLL))
	/* Procs which have issued a timed out poll req will remain
	   linked to the sleepers queue, and will be silently unlinked
	   the next time the real-time kernel side kicks
	   xnpipe_wakeup_proc. */
	xnpipe_enqueue_wait(state,NULL,XNPIPE_USER_WPOLL);

    return mask;
}

static struct file_operations xnpipe_fops = {
	owner:		THIS_MODULE,
	read:		xnpipe_read,
	write:		xnpipe_write,
	poll:		xnpipe_poll,
	ioctl:		xnpipe_ioctl,
	open:		xnpipe_open,
	release:	xnpipe_release,
	fasync:		xnpipe_fasync
};

int xnpipe_mount (void)

{
    xnpipe_state_t *state;

    for (state = &xnpipe_states[0];
	 state < &xnpipe_states[XNPIPE_NDEVS]; state++)
	{
	inith(&state->slink);
	inith(&state->alink);
	state->status = 0;
	state->wchan = NULL;
	state->asyncq = NULL;
	initq(&state->inq);
	initq(&state->outq);
	state->output_handler = NULL;
	state->input_handler = NULL;
	state->alloc_handler = NULL;
	}

    initq(&xnpipe_sleepq);
    initq(&xnpipe_asyncq);
    spin_lock_init(&xnpipe_sqlock);
    spin_lock_init(&xnpipe_aqlock);

    if (register_chrdev(XNPIPE_DEV_MAJOR,"rtpipe",&xnpipe_fops))
	{
	xnlogerr("Unable to reserve major #%d for real-time pipe service.\n",
		 XNPIPE_DEV_MAJOR);
	return -EPIPE;
	}

    xnpipe_wakeup_srq = rthal_request_srq(0,&xnpipe_wakeup_proc);

    return 0;
}

void xnpipe_umount (void)

{
    rthal_release_srq(xnpipe_wakeup_srq);
    unregister_chrdev(XNPIPE_DEV_MAJOR,"rtpipe");
}

EXPORT_SYMBOL(xnpipe_connect);
EXPORT_SYMBOL(xnpipe_disconnect);
EXPORT_SYMBOL(xnpipe_send);
EXPORT_SYMBOL(xnpipe_recv);
EXPORT_SYMBOL(xnpipe_inquire);
EXPORT_SYMBOL(xnpipe_setup);
