/*
 * Copyright (C) 2001,2002,2003,2004 Philippe Gerum <rpm@xenomai.org>.
 * Copyright (C) 2005 Dmitry Adamushko <dmitry.adamushko@gmail.com>
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA
 * 02111-1307, USA.
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/init.h>
#include <linux/fcntl.h>
#include <linux/poll.h>
#include <linux/termios.h>
#include <linux/proc_fs.h>
#include <linux/spinlock.h>
#include <linux/device.h>
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

xnqueue_t xnpipe_sleepq, xnpipe_asyncq;

int xnpipe_wakeup_apc;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,13)
   static struct class *xnpipe_class;
#elif LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
   static struct class_simple *xnpipe_class;
   #define class_create class_simple_create
   #define class_device_create class_simple_device_add
   #define class_device_destroy(a,b) class_simple_device_remove(b)
   #define class_destroy class_simple_destroy
#endif

/* Get nklock locked before using this macro */

#define xnpipe_read_wait(state,flags)						\
    ({										\
    int __sigpending;								\
    DEFINE_WAIT(__wait);							\
    xnpipe_enqueue_read(state);							\
    xnlock_put_irqrestore(&nklock,flags);					\
    prepare_to_wait_exclusive(&(state)->readq, &__wait, TASK_INTERRUPTIBLE);	\
    schedule();									\
    finish_wait(&(state)->readq, &__wait);					\
    __sigpending = signal_pending(current);					\
    xnlock_get_irqsave(&nklock,flags);						\
    __sigpending; })

static inline void xnpipe_enqueue_read (xnpipe_state_t *state)
{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(state->status,XNPIPE_USER_WREAD))
	{
	appendq(&xnpipe_sleepq,&state->slink);
	setbits(state->status,XNPIPE_USER_WREAD);
	}

    (state->readw)++;

    xnlock_put_irqrestore(&nklock,s);
}

static inline void xnpipe_dequeue_read (xnpipe_state_t *state)
{
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    if (!--(state->readw))
	{
	clrbits(state->status,XNPIPE_USER_WREAD);
	removeq(&xnpipe_sleepq,&state->slink);
	}
    	
    xnlock_put_irqrestore(&nklock,s);
}

/*
 * Attempt to wake up Linux-side sleepers upon pipe
 * availability/readability conditions.  This routine is
 * asynchronously kicked from kernel space and only deals with
 * Linux-related variables.
 */

static void xnpipe_wakeup_proc (void *cookie)

{
    xnholder_t *holder, *nholder;
    xnpipe_state_t *state;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    nholder = getheadq(&xnpipe_sleepq);

    while ((holder = nholder) != NULL)
	{
	nholder = nextq(&xnpipe_sleepq,holder);
	state = link2xnpipe(holder,slink);

	/* Wake up the sleepers whose ready flag is set. */

	if (testbits(state->status,XNPIPE_USER_WREAD_READY))
	    {
	    clrbits(state->status,XNPIPE_USER_WREAD_READY);

		/* PREEMPT_RT kernels could schedule us out as a
		   result of up()'ing the semaphore, so we need to do
		   the housekeeping and release the spinlock early
		   on. */
	    if (waitqueue_active(&state->readq))
		{
		xnpipe_dequeue_read(state);
		xnlock_put_irqrestore(&nklock,s);
		wake_up_interruptible(&state->readq);
		}
	    else
		/* just in case of bogus XNPIPE_USER_WREAD_READY */
		continue;

	    xnlock_get_irqsave(&nklock,s);
	    
	    /* On PREEMPT_RT kernels, __wake_up() might sleep, so we
	       need to refetch the sleep queue head just to be safe;
	       for the very same reason, livelocking inside this loop
	       cannot happen. On regular kernel variants, we just keep
	       processing the entire loop in a row. */
	
#if defined(CONFIG_PREEMPT_RT) || defined (CONFIG_SMP)
	    nholder = getheadq(&xnpipe_sleepq);
#endif /* CONFIG_PREEMPT_RT || CONFIG_SMP */	    
	    }	    
	}

    /* Scan the async queue, sending the proper signal to
       subscribers. */

    nholder = getheadq(&xnpipe_asyncq);

    while ((holder = nholder) != NULL)
	{
	nholder = nextq(&xnpipe_asyncq,holder);
	state = link2xnpipe(holder,alink);

	if (testbits(state->status,XNPIPE_USER_SIGIO))
	    {
	    clrbits(state->status,XNPIPE_USER_SIGIO);
	    xnlock_put_irqrestore(&nklock,s);
	    kill_fasync(&state->asyncq,xnpipe_asyncsig,POLL_IN);
	    xnlock_get_irqsave(&nklock,s);

	    /* The reason is the same that was pointed out 
	       for the sleep queue */
	       	       
#if defined(CONFIG_PREEMPT_RT) || defined (CONFIG_SMP)
	    nholder = getheadq(&xnpipe_sleepq);
#endif /* CONFIG_PREEMPT_RT || CONFIG_SMP */
	    }
	}

    xnlock_put_irqrestore(&nklock,s);
}

static inline void xnpipe_schedule_request (void) {
    rthal_apc_schedule(xnpipe_wakeup_apc);
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
    int need_sched = 0;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    state = &xnpipe_states[minor];

    /* the whole function should be atomic
       to prevent using a partially created object */
    xnlock_get_irqsave(&nklock,s);

    if (testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EBUSY;
	}
    
    setbits(state->status,XNPIPE_KERN_CONN);
    
    xnsynch_init(&state->synchbase,XNSYNCH_FIFO);
    state->output_handler = output_handler;
    state->input_handler = input_handler;
    state->alloc_handler = alloc_handler;
    state->cookie = cookie;
    state->ionrd = 0;

    if (testbits(state->status,XNPIPE_USER_CONN))
	{
	if (testbits(state->status,XNPIPE_USER_WREAD))
	    {
	    /* Wake up the userland thread waiting for the nucleus
	       side to connect (open). */
	    setbits(state->status,XNPIPE_USER_WREAD_READY);
	    need_sched = 1;
	    }

	if (state->asyncq) /* Schedule asynch sig. */
	    {
	    setbits(state->status,XNPIPE_USER_SIGIO);
	    need_sched = 1;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    if (need_sched)
        xnpipe_schedule_request();

    return 0;
}

int xnpipe_disconnect (int minor)

{
    xnpipe_state_t *state;
    xnholder_t *holder;
    int need_sched = 0;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    state = &xnpipe_states[minor];

    /* the whole function should be atomic */
    xnlock_get_irqsave(&nklock,s);

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EBADF;
	}

    clrbits(state->status,XNPIPE_KERN_CONN);

    if (testbits(state->status,XNPIPE_USER_CONN))
	{
	while ((holder = getq(&state->inq)) != NULL)
	    {
	    if (state->input_handler != NULL)
		state->input_handler(minor,link2mh(holder),-EPIPE,state->cookie);
	    else if (state->alloc_handler == NULL)
		xnfree(link2mh(holder));
	    }

	if (xnsynch_destroy(&state->synchbase) == XNSYNCH_RESCHED)
	    xnpod_schedule();

	if (testbits(state->status,XNPIPE_USER_WMASK))
	    {
	    setbits(state->status,XNPIPE_USER_WREADY);
	    
	    /* Wake up the userland thread waiting for some operation
	       from the kernel side (read/write or poll). */
	    need_sched = 1;
	    }

	if (state->asyncq) /* Schedule asynch sig. */
	    {
	    setbits(state->status,XNPIPE_USER_SIGIO);
	    need_sched = 1;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    if (need_sched)
        xnpipe_schedule_request();

    return 0;
}

ssize_t xnpipe_send (int minor,
		     struct xnpipe_mh *mh,
		     size_t size,
		     int flags)
{
    xnpipe_state_t *state;
    int need_sched = 0;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    if (size <= sizeof(*mh))
	return -EINVAL;

    state = &xnpipe_states[minor];

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(state->status,XNPIPE_USER_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EPIPE;
	}

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EBADF;
	}

    inith(xnpipe_m_link(mh));
    xnpipe_m_size(mh) = size - sizeof(*mh);
    state->ionrd += xnpipe_m_size(mh);

    if (flags & XNPIPE_URGENT)
	prependq(&state->outq,xnpipe_m_link(mh));
    else
	appendq(&state->outq,xnpipe_m_link(mh));

    if (testbits(state->status,XNPIPE_USER_CONN))
	{
	if (testbits(state->status,XNPIPE_USER_WREAD))
	    {
	    /* Wake up the userland thread waiting for input
	       from the kernel side. */
	    setbits(state->status,XNPIPE_USER_WREAD_READY);
	    need_sched = 1;
	    }

	if (state->asyncq) /* Schedule asynch sig. */
	    {
	    setbits(state->status,XNPIPE_USER_SIGIO);
	    need_sched = 1;
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    if (need_sched)
        xnpipe_schedule_request();

    return (ssize_t)size;
}

ssize_t xnpipe_recv (int minor,
		     struct xnpipe_mh **pmh,
		     xnticks_t timeout)
{
    xnpipe_state_t *state;
    xnthread_t *thread;
    xnholder_t *holder;
    xnticks_t stime;
    ssize_t ret;
    spl_t s;

    if (minor < 0 || minor >= XNPIPE_NDEVS)
	return -ENODEV;

    if (xnpod_asynch_p())
	return -EPERM;

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

	thread = xnpod_current_thread();

	if (xnthread_test_flags(thread,XNTIMEO))
	    {
	    ret = -ETIMEDOUT;
	    goto unlock_and_exit;
	    }

	if (xnthread_test_flags(thread,XNBREAK))
	    {
	    ret = -EINTR;
	    goto unlock_and_exit;
	    }

	if (xnthread_test_flags(thread,XNRMID))
	    {
	    ret = -EIDRM;
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
    int minor, err = 0, sigpending;
    xnpipe_state_t *state;
    spl_t s;

    minor = MINOR(inode->i_rdev);

    if (minor >= XNPIPE_NDEVS)
	return -ENXIO; /* TssTss... stop playing with mknod() ;o) */

    state = &xnpipe_states[minor];

    xnlock_get_irqsave(&nklock,s);

    /* Enforce exclusive open for the message queues. */
    if (testbits(state->status,XNPIPE_USER_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EBUSY;
	}

    setbits(state->status,XNPIPE_USER_CONN);

    file->private_data = state;
    init_waitqueue_head(&state->readq);
    state->readw = 0;

    clrbits(state->status,XNPIPE_USER_WMASK|XNPIPE_USER_WREADY|XNPIPE_USER_SIGIO);

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
            clrbits(state->status,XNPIPE_USER_CONN);
	    xnlock_put_irqrestore(&nklock,s);
            return -EWOULDBLOCK;
	    }

	sigpending = xnpipe_read_wait(state,s);

	if (sigpending && !testbits(state->status,XNPIPE_KERN_CONN))
	    {
	    clrbits(state->status,XNPIPE_USER_CONN);
    	    xnlock_put_irqrestore(&nklock,s);
	    return -ERESTARTSYS;
	    }

	xnlock_put_irqrestore(&nklock,s);
	}
    else
	{
	xnlock_put_irqrestore(&nklock,s);

	if (xnpipe_open_handler)
	    err = xnpipe_open_handler(xnminor_from_state(state),state->cookie);
	}

    if(err)
        clrbits(state->status,XNPIPE_USER_CONN);
    
    return err;
}

static int xnpipe_release (struct inode *inode,
			   struct file *file)
{
    xnpipe_state_t *state;
    xnholder_t *holder;
    int err = 0;
    spl_t s;

    state = (xnpipe_state_t *)file->private_data;

    xnlock_get_irqsave(&nklock,s);

    if (testbits(state->status,XNPIPE_USER_WMASK))
	removeq(&xnpipe_sleepq,&state->slink);

    clrbits(state->status,XNPIPE_USER_WMASK);

    if (testbits(state->status,XNPIPE_KERN_CONN))
	{
	int minor = xnminor_from_state(state);

	if (state->output_handler != NULL)
	    {
	    while ((holder = getq(&state->outq)) != NULL)
		state->output_handler(minor,link2mh(holder),-EPIPE,state->cookie);
	    }

	/* If a real-time kernel thread is waiting on this object,
	   wake it up now. */

	if (xnsynch_nsleepers(&state->synchbase) > 0)
	    {
	    xnsynch_flush(&state->synchbase,XNBREAK);
	    xnpod_schedule();
	    }

	xnlock_put_irqrestore(&nklock,s);

	if (xnpipe_close_handler != NULL)
	    err = xnpipe_close_handler(minor,state->cookie);
	}
    else
	xnlock_put_irqrestore(&nklock,s);

    if (waitqueue_active(&state->readq))
	wake_up_interruptible_all(&state->readq);

    if (state->asyncq) /* Clear the async queue */
	{
        xnlock_get_irqsave(&nklock,s);
	removeq(&xnpipe_asyncq,&state->alink);
	clrbits(state->status,XNPIPE_USER_SIGIO);
	xnlock_put_irqrestore(&nklock,s);
	fasync_helper(-1,file,0,&state->asyncq);
	}

    /* Free the state object. Since that time it can be open by someone else */
    clrbits(state->status,XNPIPE_USER_CONN);

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
    int sigpending;
    ssize_t ret;
    spl_t s;

    if (!access_ok(VERIFY_WRITE,buf,count))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);

    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EPIPE;
	}

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

	sigpending = xnpipe_read_wait(state,s);
	holder = getq(&state->outq);
	mh = link2mh(holder);

	if (sigpending && !mh)
	    {
    	    xnlock_put_irqrestore(&nklock,s);
	    return -ERESTARTSYS;
	    }
	}

    if (mh)
	{
	xnpipe_io_handler *handler = state->output_handler;
	void *cookie = state->cookie;
	
	ret = (ssize_t)xnpipe_m_size(mh); /* Cannot be zero */
	state->ionrd -= ret;
	
	xnlock_put_irqrestore(&nklock,s);

	if (ret <= count)
	    {
	    if (__copy_to_user(buf,xnpipe_m_data(mh),ret))
		ret = -EFAULT;
	    }
	else
	    /* Return buffer is too small - message is lost. */
	    ret = -ENOSPC;

	if (handler != NULL)
	    ret = handler(xnminor_from_state(state),mh,ret,cookie);
	}
    else /* Closed by peer. */
	{
	xnlock_put_irqrestore(&nklock,s);
	ret = 0;
	}

    return ret;
}

static ssize_t xnpipe_write (struct file *file,
			     const char *buf, 
			     size_t count,
			     loff_t *ppos)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    xnpipe_alloc_handler *alloc_handler;
    xnpipe_io_handler *input_handler;
    struct xnpipe_mh *mh;
    xnthread_t *sleeper;
    void *cookie;
    spl_t s;

    if (count == 0)
	return -EINVAL;

    if (!access_ok(VERIFY_READ,buf,count))
	return -EFAULT;

    xnlock_get_irqsave(&nklock,s);
    
    if (!testbits(state->status,XNPIPE_KERN_CONN))
	{
	xnlock_put_irqrestore(&nklock,s);
	return -EPIPE;
	}

    alloc_handler = state->alloc_handler;
    input_handler = state->input_handler;
    cookie = state->cookie;

    xnlock_put_irqrestore(&nklock,s);

    if (alloc_handler != NULL)
	mh = (struct xnpipe_mh *)alloc_handler(xnminor_from_state(state),
					       count + sizeof(*mh),
					       cookie);
    else
	mh = (struct xnpipe_mh *)xnmalloc(count + sizeof(*mh));

    if (!mh)
	/* Cannot sleep. */
	return -ENOMEM;

    inith(xnpipe_m_link(mh));
    xnpipe_m_size(mh) = count;
    __copy_from_user(xnpipe_m_data(mh),buf,count);

    xnlock_get_irqsave(&nklock,s);

    appendq(&state->inq,&mh->link);

    /* If a real-time kernel thread is waiting on this input queue,
       wake it up now. */

    if (xnsynch_nsleepers(&state->synchbase) > 0)
	{
	sleeper = xnsynch_wakeup_one_sleeper(&state->synchbase);
	xnpod_schedule();
	}

    xnlock_put_irqrestore(&nklock,s);

    if (input_handler != NULL)
	{
	int err = input_handler(xnminor_from_state(state),mh,0,cookie);

	if (err != 0)
	    count = (size_t)err;
	}

    return (ssize_t)count;
}

static int xnpipe_ioctl (struct inode *inode,
			 struct file *file,
			 unsigned int cmd,
			 unsigned long arg)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    xnpipe_io_handler *io_handler;
    struct xnpipe_mh *mh;
    xnholder_t *holder;
    void *cookie;
    int err = 0, n;
    spl_t s;

    switch (cmd)
	{
	case XNPIPEIOC_GET_NRDEV:

	    if (put_user(XNPIPE_NDEVS,(int *)arg))
		return -EFAULT;

	    break;

	case XNPIPEIOC_FLUSH:

	    /* Theoretically, a flush request could be prevented from
	       ending by a real-time sender perpetually feeding its
	       output queue, and doing so fast enough for the current
	       Linux task to be stuck inside the flush loop. However,
	       the way it is coded also reduces the interrupt-free
	       section to a bare minimum, so it's definitely better
	       latency-wise. Additionally, such jamming behaviour from
	       the kernel side would be the sign of some design
	       problem anyway. */

	    n = 0;
	    xnlock_get_irqsave(&nklock,s);
	    
	    io_handler = state->output_handler;
	    cookie = state->cookie;

	    while ((holder = getq(&state->outq)) != NULL)
		{
		xnlock_put_irqrestore(&nklock,s);

		mh = link2mh(holder);
		n += xnpipe_m_size(mh);

		if (io_handler != NULL)
		    io_handler(xnminor_from_state(state),mh,0,cookie);

		xnlock_get_irqsave(&nklock,s);
		}

	    state->ionrd -= n;
	    xnlock_put_irqrestore(&nklock,s);
	    err = n;

	    break;

	case FIONREAD:

	    xnlock_get_irqsave(&nklock,s);

	    n = testbits(state->status,XNPIPE_KERN_CONN) ? state->ionrd : 0;

	    xnlock_put_irqrestore(&nklock,s);

	    if (put_user(n,(int *)arg))
		return -EFAULT;

	    break;

	case TCGETS:
	    /* For isatty() probing. */
	    return -ENOTTY;

	default:

	    return -EINVAL;
	}

    return err;
}

static int xnpipe_fasync (int fd,
			  struct file *file,
			  int on)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    int ret, queued;
    spl_t s;

    queued = (state->asyncq != NULL);
    ret = fasync_helper(fd,file,on,&state->asyncq);

    if (state->asyncq)
	{
	if (!queued)
	    {
	    xnlock_get_irqsave(&nklock,s);
	    appendq(&xnpipe_asyncq,&state->alink);
	    xnlock_put_irqrestore(&nklock,s);
	    }
	}
    else if (queued)
	    {
	    xnlock_get_irqsave(&nklock,s);
	    removeq(&xnpipe_asyncq,&state->alink);
	    xnlock_put_irqrestore(&nklock,s);
	    }

    return ret;
}

static unsigned xnpipe_poll (struct file *file,
			     poll_table *pt)
{
    xnpipe_state_t *state = (xnpipe_state_t *)file->private_data;
    unsigned r_mask = 0, w_mask = 0;

    poll_wait(file,&state->readq,pt);

    if (testbits(state->status,XNPIPE_KERN_CONN))
	w_mask |= (POLLOUT|POLLWRNORM);

    if (countq(&state->outq) > 0)
	r_mask |= (POLLIN|POLLRDNORM);

    if (!r_mask)
	/* Procs which have issued a timed out poll req will remain
	   linked to the sleepers queue, and will be silently unlinked
	   the next time the real-time kernel side kicks
	   xnpipe_wakeup_proc. */
	xnpipe_enqueue_read(state);
			    
    /* a descriptor is always ready for writing with the current
       implementation, so there is no need to have/handle the writeq
       queue so far */

    return r_mask|w_mask;
}

static struct file_operations xnpipe_fops = {
	.owner =	THIS_MODULE,
	.read =		xnpipe_read,
	.write =	xnpipe_write,
	.poll =		xnpipe_poll,
	.ioctl =	xnpipe_ioctl,
	.open =		xnpipe_open,
	.release =	xnpipe_release,
	.fasync =	xnpipe_fasync
};

int xnpipe_mount (void)

{
    xnpipe_state_t *state;
    int i;

    for (state = &xnpipe_states[0];
	 state < &xnpipe_states[XNPIPE_NDEVS]; state++)
	{
	inith(&state->slink);
	inith(&state->alink);
	state->status = 0;
	state->asyncq = NULL;
	initq(&state->inq);
	initq(&state->outq);
	state->output_handler = NULL;
	state->input_handler = NULL;
	state->alloc_handler = NULL;
	}

    initq(&xnpipe_sleepq);
    initq(&xnpipe_asyncq);

    xnpipe_class = class_create(THIS_MODULE, "rtpipe");
    if(IS_ERR(xnpipe_class))
    {
       xnlogerr("Error creating rtpipe class, err=%ld.\n",PTR_ERR(xnpipe_class));
       return -EBUSY; 
    }

    for (i = 0; i < XNPIPE_NDEVS; i++)
    {
       struct class_device* cldev;
       cldev = class_device_create(xnpipe_class, MKDEV(XNPIPE_DEV_MAJOR, i),
                                   NULL, "rtp%d", i);
       if(IS_ERR(cldev))
       {
          xnlogerr("Can't add device class, major=%d, minor=%d, err=%ld\n", 
		   XNPIPE_DEV_MAJOR, i, PTR_ERR(cldev));
          class_destroy(xnpipe_class);
          return -EBUSY;
       }
    }

    if (register_chrdev(XNPIPE_DEV_MAJOR,"rtpipe",&xnpipe_fops))
	{
	xnlogerr("Unable to reserve major #%d for message pipe support.\n",
		 XNPIPE_DEV_MAJOR);
	return -EBUSY;
	}

    xnpipe_wakeup_apc = rthal_apc_alloc("pipe_wakeup",&xnpipe_wakeup_proc,NULL);

    return 0;
}

void xnpipe_umount (void)

{
    int i;

    rthal_apc_free(xnpipe_wakeup_apc);
    unregister_chrdev(XNPIPE_DEV_MAJOR,"rtpipe");

    for (i = 0; i < XNPIPE_NDEVS; i ++)
        class_device_destroy(xnpipe_class, MKDEV(XNPIPE_DEV_MAJOR, i));

    class_destroy(xnpipe_class);
}

EXPORT_SYMBOL(xnpipe_connect);
EXPORT_SYMBOL(xnpipe_disconnect);
EXPORT_SYMBOL(xnpipe_send);
EXPORT_SYMBOL(xnpipe_recv);
EXPORT_SYMBOL(xnpipe_inquire);
EXPORT_SYMBOL(xnpipe_setup);
