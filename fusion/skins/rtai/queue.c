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
 * \ingroup queue
 */

/*!
 * \ingroup native
 * \defgroup queue Queue services.
 *
 * Queue services.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/queue.h>
#include <rtai/registry.h>

int __queue_pkg_init (void)

{
    return 0;
}

void __queue_pkg_cleanup (void)

{
}

int rt_queue_create (RT_QUEUE *q,
		     const char *name,
		     size_t poolsize,
		     unsigned qmax,
		     int mode)
{
    void *poolmem;
    int err;

    xnpod_check_context(XNPOD_ROOT_CONTEXT);

#ifdef __KERNEL__
    if (mode & Q_DMA)
	poolmem = kmalloc(poolsize,GFP_KERNEL|GFP_DMA);
    else
	poolmem = vmalloc(poolsize);
#else /* !__KERNEL__ */
	poolmem = xnarch_sysalloc(poolsize);
#endif /* __KERNEL__ */

    if (poolmem == NULL)
	return -ENOMEM;

    xnsynch_init(&q->synch_base,mode & (Q_PRIO|Q_FIFO));
    initq(&q->pendq);
    q->handle = 0;  /* i.e. (still) unregistered queue. */
    q->magic = RTAI_QUEUE_MAGIC;
    q->qmax = qmax;
    q->mode = mode;
    xnobject_copy_name(q->name,name);

    err = xnheap_init(&q->bufpool,
		      poolmem,
		      poolsize,
		      PAGE_SIZE); /* Use natural page size */
    if (err)
	{
#ifdef __KERNEL__
	if (mode & Q_DMA)
	    kfree(poolmem);
	else
	    vfree(poolmem);
#else /* !__KERNEL__ */
	xnarch_sysfree(poolmem);
#endif /* __KERNEL__ */
	return err;
	}

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(q->name,q,&q->handle);

        if (err)
            rt_queue_delete(q);
        }
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

static void __queue_flush_pool (void *poolmem,
				u_long poolsize,
				void *cookie)
{
#ifdef __KERNEL__
    RT_QUEUE *q = (RT_QUEUE *)cookie;

    if (q->mode & Q_DMA)
	kfree(poolmem);
    else
	vfree(poolmem);
#else /* !__KERNEL__ */
    xnarch_sysfree(poolmem);
#endif /* __KERNEL__ */
}

int rt_queue_delete (RT_QUEUE *q)

{
    int err = 0, rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    q = rtai_h2obj_validate(q,RTAI_QUEUE_MAGIC,RT_QUEUE);

    if (!q)
        {
        err = rtai_handle_error(q,RTAI_QUEUE_MAGIC,RT_QUEUE);
        goto unlock_and_exit;
        }
    
    xnheap_destroy(&q->bufpool,&__queue_flush_pool,q);

    rc = xnsynch_destroy(&q->synch_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (q->handle)
        rt_registry_remove(q->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    rtai_mark_deleted(q);

    if (rc == XNSYNCH_RESCHED)
        /* Some task has been woken up as a result of the deletion:
           reschedule now. */
        xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

void *rt_queue_alloc (RT_QUEUE *q,
		      size_t size)
{
    rt_queue_msg_t *msg;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    q = rtai_h2obj_validate(q,RTAI_QUEUE_MAGIC,RT_QUEUE);

    if (!q)
        {
	xnlock_put_irqrestore(&nklock,s);
	return NULL;
        }
    
    msg = (rt_queue_msg_t *)xnheap_alloc(&q->bufpool,size + sizeof(rt_queue_msg_t));

    if (msg)
	{
	inith(&msg->link);
	msg->size = size;
	msg->refcount = 1;
	}

    xnlock_put_irqrestore(&nklock,s);

    return msg + 1;
}

int rt_queue_free (RT_QUEUE *q,
		   void *buf)
{
    rt_queue_msg_t *msg;
    int err;
    spl_t s;

    if (buf == NULL)
	return -EINVAL;

    msg = ((rt_queue_msg_t *)buf) - 1;

    xnlock_get_irqsave(&nklock,s);

    if (msg->refcount == 0)
	{
	err = -EINVAL;
	goto unlock_and_exit;
	}

    if (--msg->refcount > 0)
	{
	err = 0;
	goto unlock_and_exit;
	}

    q = rtai_h2obj_validate(q,RTAI_QUEUE_MAGIC,RT_QUEUE);

    if (!q)
        {
        err = rtai_handle_error(q,RTAI_QUEUE_MAGIC,RT_QUEUE);
        goto unlock_and_exit;
        }
    
    err = xnheap_free(&q->bufpool,msg);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

int rt_queue_send (RT_QUEUE *q,
		   void *buf,
		   int mode)
{
    xnthread_t *sleeper;
    rt_queue_msg_t *msg;
    int err, nrecv = 0;
    spl_t s;

    if (buf == NULL)
	return -EINVAL;

    xnlock_get_irqsave(&nklock,s);

    q = rtai_h2obj_validate(q,RTAI_QUEUE_MAGIC,RT_QUEUE);

    if (!q)
        {
        err = rtai_handle_error(q,RTAI_QUEUE_MAGIC,RT_QUEUE);
        goto unlock_and_exit;
        }

    if (q->qmax != Q_UNLIMITED && countq(&q->pendq) >= q->qmax)
	{
	err = -EBUSY;
	goto unlock_and_exit;
	}

    msg = ((rt_queue_msg_t *)buf) - 1;
    /* Message buffer ownership is being transfered from the sender to
       the receiver here; so we need to update the reference count
       appropriately. */
    msg->refcount--;

    do
	{
	sleeper = xnsynch_wakeup_one_sleeper(&q->synch_base);
    
	if (!sleeper)
	    break;

	thread2rtask(sleeper)->wait_args.qmsg = msg;
	msg->refcount++;
	nrecv++;
	}
    while (mode & Q_BROADCAST);

    if (nrecv > 0)
	xnpod_schedule();
    else if (!(mode & Q_BROADCAST))
	{
	/* Messages are never queued in broadcast mode. Otherwise we
	   need to queue the message if no task is waiting for it. */

	if (mode & Q_URGENT)
	    prependq(&q->pendq,&msg->link);
	else
	    appendq(&q->pendq,&msg->link);
	}

    err = nrecv;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

ssize_t rt_queue_recv (RT_QUEUE *q,
		       void **bufp,
		       RTIME timeout)
{
    rt_queue_msg_t *msg = NULL;
    xnholder_t *holder;
    ssize_t err = 0;
    RT_TASK *task;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    q = rtai_h2obj_validate(q,RTAI_QUEUE_MAGIC,RT_QUEUE);

    if (!q)
        {
        err = rtai_handle_error(q,RTAI_QUEUE_MAGIC,RT_QUEUE);
        goto unlock_and_exit;
        }

    holder = getq(&q->pendq);

    if (holder)
	msg = link2rtmsg(holder);
    else
	{
	if (timeout == RT_TIME_NONBLOCK)
	    {
	    err = -EWOULDBLOCK;;
	    goto unlock_and_exit;
	    }

	xnpod_check_context(XNPOD_THREAD_CONTEXT);

	xnsynch_sleep_on(&q->synch_base,timeout);

	task = rtai_current_task();

	if (xnthread_test_flags(&task->thread_base,XNRMID))
	    err = -EIDRM; /* Queue deleted while pending. */
	else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	    err = -ETIMEDOUT; /* Timeout.*/
	else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	    err = -EINTR; /* Unblocked.*/
	else
	    {
	    msg = task->wait_args.qmsg;
	    task->wait_args.qmsg = NULL;
	    }
	}

    if (msg)
	{
	*bufp = msg + 1;
	err = (ssize_t)msg->size;
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}
