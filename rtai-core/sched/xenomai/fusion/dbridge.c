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
#include <xenomai/pod.h>
#include <xenomai/fusion.h>
#include <xenomai/dbridge.h>

static int dbridge_asyncsig = SIGIO;

dbridge_state_t dbridge_states[DBRIDGE_NDEVS];

xnqueue_t dbridge_sleepq;

xnqueue_t dbridge_asyncq;

spinlock_t dbridge_sqlock;

spinlock_t dbridge_aqlock;

unsigned dbridge_wakeup_srq;

/*
 * Attempt to wake up Linux-side sleepers upon bridge
 * availability/readability conditions.  This routine is
 * asynchronously kicked by the real-time domain and only deals with
 * Linux-related variables.
 */

static void dbridge_wakeup_proc (void)

{
    xnholder_t *holder, *nholder;
    dbridge_state_t *state;
    u_long slflags;
    spl_t s;

#ifdef DBRIDGE_DEBUG
    xnprintf("WAKEUP PROC ACTIVATED: SLEEPQ HAS %d ELEMENTS\n",
	     countq(&dbridge_sleepq));
#endif

    spin_lock_irqsave(&dbridge_sqlock,slflags);

    nholder = getheadq(&dbridge_sleepq);

    while ((holder = nholder) != NULL)
	{
	state = link2dbs(holder,slink);

	/* Wake up the sleepers whose suspension flag disappeared. */

#ifdef DBRIDGE_DEBUG
	xnprintf("MINOR #%d FOUND ON SLEEPQ\n",minor_from_state(state));
#endif

	if (!testbits(state->status,DBRIDGE_USR_WMASK))
	    {
	    nholder = popq(&dbridge_sleepq,holder);
	    clrbits(state->status,DBRIDGE_USR_ONWAIT);

	    spin_unlock_irqrestore(&dbridge_sqlock,slflags);

	    if (state->wchan)
		{
#ifdef DBRIDGE_DEBUG
		xnprintf("AWAKENING MINOR #%d ON SEM\n",minor_from_state(state));
#endif
		up(state->wchan);
		state->wchan = NULL;
		}
	    else if (waitqueue_active(&state->pollq))
		{
#ifdef DBRIDGE_DEBUG
		xnprintf("AWAKENING MINOR #%d ON POLLQ\n",minor_from_state(state));
#endif
		wake_up_interruptible(&state->pollq);
		}

	    spin_lock_irqsave(&dbridge_sqlock,slflags);

	    set_need_resched();
	    }
	else
	    nholder = nextq(&dbridge_sleepq,holder);
	}

    spin_unlock_irqrestore(&dbridge_sqlock,slflags);

    /* Scan the async queue, sending the proper signal to
       subscribers. */

    spin_lock_irqsave(&dbridge_aqlock,slflags);

    holder = getheadq(&dbridge_asyncq);

    while (holder != NULL)
	{
	state = link2dbs(holder,alink);

	splhigh(s);

	if (testbits(state->status,DBRIDGE_USR_SIGIO))
	    {
	    clrbits(state->status,DBRIDGE_USR_SIGIO);
	    splexit(s);
	    kill_fasync(&state->asyncq,dbridge_asyncsig,POLL_IN);
	    set_need_resched();
	    }
	else
	    splexit(s);

	holder = nextq(&dbridge_asyncq,holder);
	}

    spin_unlock_irqrestore(&dbridge_aqlock,slflags);
}

void dbridge_enqueue_wait (dbridge_state_t *state,
			   struct semaphore *wchan,
			   int flags)
{
    /* NOTE: Each caller _must_ ensure that this helper routine will
       be safe from preemption, including from the real-time side. */

    state->wchan = wchan;	/* may be NULL */
    setbits(state->status,flags|DBRIDGE_USR_ONWAIT);
    appendq(&dbridge_sleepq,&state->slink);
}

void dbridge_dequeue_wait (dbridge_state_t *state,
			   int flags)
{
    removeq(&dbridge_sleepq,&state->slink);
    clrbits(state->status,flags|DBRIDGE_USR_ONWAIT);
}

/*
 * Open a domain bridge.
 */

static int dbridge_open (struct inode *inode,
			 struct file *file)
{
    dbridge_state_t *state;
    int minor;

#ifdef DBRIDGE_DEBUG
    xnprintf("USR: OPENING MINOR #%d\n",MINOR(inode->i_rdev));
#endif

    minor = MINOR(inode->i_rdev);

    if (minor >= DBRIDGE_NDEVS)
	return -ENOSYS; /* TssTss... stop playing with mknod() ;o) */

    state = &dbridge_states[minor];

    file->private_data = state;
    sema_init(&state->open_sem,0);
    sema_init(&state->send_sem,0);
    sema_init(&state->event_sem,0);
    init_waitqueue_head(&state->pollq);
    state->wchan = NULL;

    return state->ops->open(state,inode,file);
}

static int dbridge_release (struct inode *inode,
			    struct file *file)
{
    dbridge_state_t *state;
    unsigned long slflags;
    spl_t s;
    int err;

#ifdef DBRIDGE_DEBUG
    xnprintf("USR: CLOSING MINOR #%d\n",MINOR(inode->i_rdev));
#endif

    state = (dbridge_state_t *)file->private_data;

    splhigh(s);

    if (testbits(state->status,DBRIDGE_USR_ONWAIT))
	removeq(&dbridge_sleepq,&state->slink);

    clrbits(state->status,DBRIDGE_USR_CONN|DBRIDGE_USR_WMASK);

    err = state->ops->close(state,inode,file);

    splexit(s);

    if (waitqueue_active(&state->pollq))
	wake_up_interruptible(&state->pollq);

    if (state->asyncq) /* Clear the async queue */
	{
	spin_lock_irqsave(&dbridge_aqlock,slflags);
	removeq(&dbridge_asyncq,&state->alink);
	spin_unlock_irqrestore(&dbridge_aqlock,slflags);
	fasync_helper(-1,file,0,&state->asyncq);
	}

    set_need_resched();

    return err;
}

static ssize_t dbridge_read (struct file *file,
			     char *buf,
			     size_t count,
			     loff_t *ppos)
{
    dbridge_state_t *state = (dbridge_state_t *)file->private_data;
    return state->ops->read(state,file,buf,count,ppos);
}

static ssize_t dbridge_write (struct file *file,
			      const char *buf, 
			      size_t count,
			      loff_t *ppos)
{
    dbridge_state_t *state = (dbridge_state_t *)file->private_data;
    return state->ops->write(state,file,buf,count,ppos);
}

static int dbridge_ioctl (struct inode *inode,
			  struct file *file,
			  unsigned int cmd,
			  unsigned long arg)
{
    dbridge_state_t *state = (dbridge_state_t *)file->private_data;
    return state->ops->ioctl(state,inode,file,cmd,arg);
}

static int dbridge_fasync (int fd,
			   struct file *file,
			   int on)
{
    dbridge_state_t *state = (dbridge_state_t *)file->private_data;
    return state->ops->fasync(state,fd,file,on);
}

static unsigned dbridge_poll (struct file *file,
			      poll_table *pt)
{
    dbridge_state_t *state = (dbridge_state_t *)file->private_data;
    return state->ops->poll(state,file,pt);
}

static struct file_operations dbridge_fops = {
	owner:		THIS_MODULE,
	read:		dbridge_read,
	write:		dbridge_write,
	poll:		dbridge_poll,
	ioctl:		dbridge_ioctl,
	open:		dbridge_open,
	release:	dbridge_release,
	fasync:		dbridge_fasync
};

extern int linux_msg_init(void);

extern void linux_msg_exit(void);

static int (*dbridge_inits[])(void) = {
    &linux_msg_init
};

static void (*dbridge_exits[])(void) = {
    &linux_msg_exit
};

int __init dbridge_init (void)

{
    dbridge_state_t *state;
    int err, n;

    for (state = &dbridge_states[0];
	 state < &dbridge_states[DBRIDGE_NDEVS]; state++)
	{
	inith(&state->slink);
	inith(&state->alink);
	state->status = 0;
	state->wchan = NULL;
	state->asyncq = NULL;
	}

    initq(&dbridge_sleepq);
    initq(&dbridge_asyncq);
    spin_lock_init(&dbridge_sqlock);
    spin_lock_init(&dbridge_aqlock);

    for (n = 0; n < sizeof(dbridge_inits) / sizeof(dbridge_inits[0]); n++)
	{
	err = dbridge_inits[n]();

	if (err)
	    {
	    while (--n > 0)	/* Oops, unwind all. */
		dbridge_exits[n]();

	    return err;
	    }
	}

    if (register_chrdev(DBRIDGE_MAJOR,"dbridge",&dbridge_fops))
	{
	printk(KERN_WARNING "RTAI/fusion: unable to get major %d for domain bridge\n",DBRIDGE_MAJOR);
	return -EIO;
	}

    dbridge_wakeup_srq = rt_request_srq(0,&dbridge_wakeup_proc,NULL);

    return XN_OK;
}

void __exit dbridge_exit (void)

{
    int n;

    for (n = 0; n < sizeof(dbridge_exits) / sizeof(dbridge_exits[0]); n++)
	dbridge_exits[n]();

    rt_free_srq(dbridge_wakeup_srq);
    unregister_chrdev(DBRIDGE_MAJOR,"dbridge");
}
