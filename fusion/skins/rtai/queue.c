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
 * \defgroup queue Message queue services.
 *
 * Queue services.
 *
 * Message queueing is a method by which real-time tasks can exchange
 * or pass data through a RTAI-managed queue of messages. Messages can
 * vary in length and be assigned different types or usages. A message
 * queue can be created by one task and used by multiple tasks that
 * read and/or write messages to the queue.
 *
 * This implementation is based on a zero-copy scheme for message
 * buffers. Message buffer pools are built over Xenomai's heap
 * objects, which in turn provide the needed support for exchanging
 * messages between kernel and user-space using direct memory mapping.
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

static void __queue_flush_private (xnheap_t *heap,
				   void *poolmem,
				   u_long poolsize,
				   void *cookie)
{
    xnarch_sysfree(poolmem,poolsize);
}

/**
 * @fn int rt_queue_create(RT_QUEUE *q,
                           const char *name,
			   size_t poolsize,
			   size_t qlimit,
			   int mode)
 * @brief Create a message queue.
 *
 * Create a message queue object that allows multiple tasks to
 * exchange data through the use of variable-sized messages. A message
 * queue is created empty.
 *
 * @param q The address of a queue descriptor RTAI will use to store
 * the queue-related data.  This descriptor must always be valid while
 * the message queue is active therefore it must be allocated in
 * permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * queue. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created queue.
 *
 * @param poolsize The size (in bytes) of the message buffer pool
 * which is going to be pre-allocated to the queue. Message buffers
 * will be claimed and released to this pool.  The buffer pool memory
 * is not extensible, so this value must be compatible with the
 * highest message pressure that could be expected.
 *
 * @param qlimit This parameter allows to limit the maximum number of
 * messages which can be queued at any point in time. Sending to a
 * full queue begets an error. The special value Q_UNLIMITED can be
 * passed to specify an unlimited amount.
 *
 * @param mode The queue creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new queue:
 *
 * - Q_FIFO makes tasks pend in FIFO order on the queue for consuming
 * messages.
 *
 * - Q_PRIO makes tasks pend in priority order on the queue.
 *
 * - Q_SHARED causes the queue to be sharable between kernel and
 * user-space tasks. Otherwise, the new queue is only available to
 * kernel-based usage.
 *
 * - Q_DMA causes the buffer pool associated to the queue to be
 * allocated in physically contiguous memory, suitable for DMA
 * operations with I/O devices. A 128Kb limit exists for @a poolsize
 * when this flag is passed.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EINVAL is returned if @a poolsize is null.
 *
 * - -ENOMEM is returned if not enough system memory is available to
 * create the queue. Additionally, and if Q_SHARED has been passed in
 * @a mode, errors while mapping the buffer pool in the caller's
 * address space might beget this return code too.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_queue_create (RT_QUEUE *q,
		     const char *name,
		     size_t poolsize,
		     size_t qlimit,
		     int mode)
{
    int err;

    xnpod_check_context(XNPOD_ROOT_CONTEXT);

    if (poolsize == 0)
	return -EINVAL;

#ifdef __KERNEL__
    if (mode & Q_SHARED)
	{
	err = xnheap_init_shared(&q->bufpool,
				 poolsize,
				 (mode & Q_DMA) ? GFP_DMA : 0);
	if (err)
	    return err;
	}
    else
#endif /* __KERNEL__ */
	{
	void *poolmem = xnarch_sysalloc(poolsize);

	if (!poolmem)
	    return -ENOMEM;

	err = xnheap_init(&q->bufpool,
			  poolmem,
			  poolsize,
			  PAGE_SIZE); /* Use natural page size */
	if (err)
	    {
	    xnarch_sysfree(poolmem,poolsize);
	    return err;
	    }
	}

    xnsynch_init(&q->synch_base,mode & (Q_PRIO|Q_FIFO));
    initq(&q->pendq);
    q->handle = 0;  /* i.e. (still) unregistered queue. */
    q->magic = RTAI_QUEUE_MAGIC;
    q->qlimit = qlimit;
    q->mode = mode;
    xnobject_copy_name(q->name,name);

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

/**
 * @fn int rt_queue_delete(RT_QUEUE *q)
 * @brief Delete a message queue.
 *
 * Destroy a message queue and release all the tasks currently pending
 * on it.  A quueue exists in the system since rt_queue_create() has
 * been called to create it, so this service must be called in order
 * to destroy it afterwards.
 *
 * @param q The descriptor address of the affected queue.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

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

#ifdef __KERNEL__
    if (q->mode & Q_SHARED)
	err = xnheap_destroy_shared(&q->bufpool);
    else
#endif /* __KERNEL__ */
	err = xnheap_destroy(&q->bufpool,&__queue_flush_private,NULL);

    if (err)
	goto unlock_and_exit;

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

/**
 * @fn void *rt_queue_alloc(RT_QUEUE *q,
                            size_t size)
 *
 * @brief Allocate a message queue buffer.
 *
 * This service allocates a message buffer from the queue's internal
 * pool which can be subsequently filled by the caller then passed to
 * rt_queue_send() for sending.
 *
 * @param size The requested size in bytes of the buffer. Zero is an
 * acceptable value, meaning that the message will not carry any
 * payload data; the receiver will thus receive a zero-sized message.
 *
 * @return The address of the allocated message buffer upon success,
 * or NULL if the allocation fails.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

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
	msg->size = size;	/* Zero is ok. */
	msg->refcount = 1;
	++msg;
	}

    xnlock_put_irqrestore(&nklock,s);

    return msg;
}

/**
 * @fn int rt_queue_free(RT_QUEUE *q,
                         void *buf)
 *
 * @brief Free a message queue buffer.
 *
 * This service releases a message buffer returned by rt_queue_recv()
 * to the queue's internal pool.
 *
 * @param buf The address of the message buffer to free. Even
 * zero-sized messages carrying no payload data must be freed, since
 * they are assigned a valid memory space to store internal
 * information.
 *
 * @return 0 is returned upon success, or -EINVAL if @a buf is not a
 * valid message buffer previously allocated by the rt_queue_alloc()
 * service.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

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

/**
 * @fn int rt_queue_send(RT_QUEUE *q,
                         void *buf,
			 size_t size,
			 int mode)
 *
 * @brief Send a message to a queue.
 *
 * This service sends a complete message to a given queue.
 *
 * @param q The descriptor address of the message queue to send to.
 *
 * @param buf The address of the message to be sent.  The message
 * space must have been allocated using the rt_queue_alloc() service.
 * Once passed to rt_queue_send(), the memory pointed to by @a buf is
 * no more under the control of the sender and thus should not be
 * referenced by it anymore; deallocation of this memory must be
 * handled on the receiving side.
 *
 * @param size The size in bytes of the message. Zero is a valid
 * value, in which case an empty message will be sent.
 *
 * @param mode A set of flags affecting the operation:
 *
 * - Q_URGENT causes the message to be prepended to the message queue,
 * ensuring a LIFO ordering.
 *
 * - Q_NORMAL causes the message to be appended to the message queue,
 * ensuring a FIFO ordering.
 *
 * - Q_BROADCAST causes the message to be sent to all tasks currently
 * waiting for messages. The message is not copied; a reference count
 * is maintained instead so that the message will remain valid until
 * the last receiver releases its own reference using rt_queue_free(),
 * after which the message space will be returned to the queue's
 * internal pool.
 *
 * @return Upon success, this service returns the number of receivers
 * which got awaken as a result of the operation. If zero is returned,
 * no task was waiting on the receiving side of the queue, and the
 * message has been enqueued. Upon error, one of the following error
 * codes is returned:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -EAGAIN is returned if queuing the message would exceed the limit
 * defined for the queue at creation.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_queue_send (RT_QUEUE *q,
		   void *buf,
		   size_t size,
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

    if (q->qlimit != Q_UNLIMITED && countq(&q->pendq) >= q->qlimit)
	{
	err = -EAGAIN;
	goto unlock_and_exit;
	}

    msg = ((rt_queue_msg_t *)buf) - 1;
    /* Message buffer ownership is being transfered from the sender to
       the receiver here; so we need to update the reference count
       appropriately. */
    msg->refcount--;
    msg->size = size;

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

/**
 * @fn int rt_queue_recv(RT_QUEUE *q,
                         void **bufp,
			 RTIME timeout)
 *
 * @brief Receive a message from a queue.
 *
 * This service retrieves the next message available from the given
 * queue. Unless otherwise specified, the caller is blocked for a
 * given amount of time if no message is immediately available on
 * entry.
 *
 * @param q The descriptor address of the message queue to receive
 * from.
 *
 * @param bufp A pointer to a memory location which will be written
 * upon success with the address of the received message. Once
 * consumed, the message space should be freed using rt_queue_free().
 *
 * @param timeout The number of clock ticks to wait for some message
 * to arrive (see note). Passing RT_TIME_INFINITE causes the caller to
 * block indefinitely until some message is eventually
 * available. Passing RT_TIME_NONBLOCK causes the service to return
 * immediately without waiting if no message is available on entry.
 *
 * @return The number of bytes available from the received message is
 * returned upon success. Zero is a possible value corresponding to a
 * zero-sized message passed to rt_queue_send(). Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a delete queue descriptor.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * RT_TIME_NONBLOCK and no message is available within the specified
 * amount of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * RT_TIME_NONBLOCK and no message is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @timeout is equal to RT_TIME_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

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

/**
 * @fn int rt_queue_inquire(RT_QUEUE *queue, RT_QUEUE_INFO *info)
 * @brief Inquire about a message queue.
 *
 * Return various information about the status of a given queue.
 *
 * @param q The descriptor address of the inquired queue.
 *
 * @param info The address of a structure the queue information will
 * be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 * - User-space task
 *
 * Rescheduling: never.
 */

int rt_queue_inquire (RT_QUEUE *q,
                      RT_QUEUE_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    q = rtai_h2obj_validate(q,RTAI_QUEUE_MAGIC,RT_QUEUE);

    if (!q)
        {
        err = rtai_handle_error(q,RTAI_QUEUE_MAGIC,RT_QUEUE);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,q->name);
    info->nwaiters = xnsynch_nsleepers(&q->synch_base);
    info->nmessages = countq(&q->pendq);
    info->qlimit = q->qlimit;
    info->mode = q->mode;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

EXPORT_SYMBOL(rt_queue_create);
EXPORT_SYMBOL(rt_queue_delete);
EXPORT_SYMBOL(rt_queue_alloc);
EXPORT_SYMBOL(rt_queue_free);
EXPORT_SYMBOL(rt_queue_send);
EXPORT_SYMBOL(rt_queue_recv);
EXPORT_SYMBOL(rt_queue_inquire);
