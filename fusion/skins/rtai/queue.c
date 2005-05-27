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
 * \ingroup native_queue
 */

/*!
 * \ingroup native
 * \defgroup native_queue Message queue services.
 *
 * Queue services.
 *
 * Message queueing is a method by which real-time tasks can exchange
 * or pass data through a RTAI-managed queue of messages. Messages can
 * vary in length and be assigned different types or usages. A message
 * queue can be created by one task and used by multiple tasks that
 * send and/or receive messages to the queue.
 *
 * This implementation is based on a zero-copy scheme for message
 * buffers. Message buffer pools are built over the nucleus's heap
 * objects, which in turn provide the needed support for exchanging
 * messages between kernel and user-space using direct memory mapping.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/queue.h>
#include <rtai/registry.h>

#ifdef CONFIG_RTAI_NATIVE_EXPORT_REGISTRY

static int __queue_read_proc (char *page,
			      char **start,
			      off_t off,
			      int count,
			      int *eof,
			      void *data)
{
    RT_QUEUE *q = (RT_QUEUE *)data;
    char *p = page;
    int len;
    spl_t s;

    p += sprintf(p,"type=%s:poolsz=%lu:limit=%d:mcount=%d\n",
		 q->mode & Q_SHARED ? "shared" : "local",
		 xnheap_size(&q->bufpool),
		 q->qlimit,
		 countq(&q->pendq));

    xnlock_get_irqsave(&nklock,s);

    if (xnsynch_nsleepers(&q->synch_base) > 0)
	{
	xnpholder_t *holder;
	
	/* Pended queue -- dump waiters. */

	holder = getheadpq(xnsynch_wait_queue(&q->synch_base));

	while (holder)
	    {
	    xnthread_t *sleeper = link2thread(holder,plink);
	    p += sprintf(p,"+%s\n",xnthread_name(sleeper));
	    holder = nextpq(xnsynch_wait_queue(&q->synch_base),holder);
	    }
	}

    xnlock_put_irqrestore(&nklock,s);

    len = (p - page) - off;
    if (len <= off + count) *eof = 1;
    *start = page + off;
    if(len > count) len = count;
    if(len < 0) len = 0;

    return len;
}

static RT_OBJECT_PROCNODE __queue_pnode = {

    .dir = NULL,
    .type = "queues",
    .entries = 0,
    .read_proc = &__queue_read_proc,
    .write_proc = NULL
};

#elif CONFIG_RTAI_OPT_NATIVE_REGISTRY

static RT_OBJECT_PROCNODE __queue_pnode = {

    .type = "queues"
};

#endif /* CONFIG_RTAI_NATIVE_EXPORT_REGISTRY */

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
 * queue is created empty. Message queues can be local to the kernel
 * space, or shared between kernel and user-space.
 *
 * This service needs the special character device /dev/rtheap
 * (10,254) when called from user-space tasks.
 *
 * @param q The address of a queue descriptor RTAI will use to store
 * the queue-related data.  This descriptor must always be valid while
 * the message queue is active therefore it must be allocated in
 * permanent memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * queue. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created queue. Shared queues must be given
 * a valid name.
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
 * user-space tasks. Otherwise, the new queue is only available for
 * kernel-based usage. This flag is implicitely set when the caller is
 * running in user-space. This feature requires the real-time support
 * in user-space to be configured in (CONFIG_OPT_RTAI_FUSION).
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
 * - -EINVAL is returned if @a poolsize is null, greater than the
 * system limit, or @a name is null or empty for a shared queue.
 *
 * - -ENOMEM is returned if not enough system memory is available to
 * create or register the queue. Additionally, and if Q_SHARED has
 * been passed in @a mode, errors while mapping the buffer pool in the
 * caller's address space might beget this return code too.
 *
 * - -EPERM is returned if this service was called from an invalid
 * context.
 *
 * - -ENOSYS is returned if @a mode specifies Q_SHARED, but the
 * real-time support in user-space is unavailable.
 *
 * - -ENOENT is returned if /dev/rtheap can't be opened.
 *   
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task (switches to secondary mode)
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

    if (!xnpod_root_p())
	return -EPERM;

    if (poolsize == 0)
	return -EINVAL;

    /* Make sure we won't hit trivial argument errors when calling
       xnheap_init(). */

    if (poolsize < 2 * PAGE_SIZE)
	poolsize = 2 * PAGE_SIZE;

    /* Account for the overhead so that the actual free space is large
       enough to match the requested size. */

    poolsize += xnheap_overhead(poolsize,PAGE_SIZE);
    poolsize = PAGE_ALIGN(poolsize);

#ifdef __KERNEL__
    if (mode & Q_SHARED)
	{
	if (!name || !*name)
	    return -EINVAL;

#ifdef CONFIG_RTAI_OPT_FUSION
	err = xnheap_init_shared(&q->bufpool,
				 poolsize,
				 (mode & Q_DMA) ? GFP_DMA : 0);
	if (err)
	    return err;

	q->cpid = 0;
#else /* !CONFIG_RTAI_OPT_FUSION */
	return -ENOSYS;
#endif /* CONFIG_RTAI_OPT_FUSION */
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

#ifdef CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(q->name,q,&q->handle,&__queue_pnode);

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
 * on it.  A queue exists in the system since rt_queue_create() has
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
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - User-space task
 *
 * Rescheduling: possible.
 */

int rt_queue_delete (RT_QUEUE *q)

{
    int err = 0, rc;
    spl_t s;

    if (xnpod_asynch_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

    q = rtai_h2obj_validate(q,RTAI_QUEUE_MAGIC,RT_QUEUE);

    if (!q)
        {
        err = rtai_handle_error(q,RTAI_QUEUE_MAGIC,RT_QUEUE);
        goto unlock_and_exit;
        }

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    if (q->mode & Q_SHARED)
	err = xnheap_destroy_shared(&q->bufpool);
    else
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
	err = xnheap_destroy(&q->bufpool,&__queue_flush_private,NULL);

    if (err)
	goto unlock_and_exit;

    rc = xnsynch_destroy(&q->synch_base);

#ifdef CONFIG_RTAI_OPT_NATIVE_REGISTRY
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
 * @param q The descriptor address of the affected queue.
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

static int __queue_check_msg (void *p)

{
    rt_queue_msg_t *msg = (rt_queue_msg_t *)p;

    if (msg->refcount == 0)
	return -EINVAL;

    if (--msg->refcount > 0)
	return -EBUSY;

    return 0;
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
 * @param q The descriptor address of the affected queue.
 *
 * @param buf The address of the message buffer to free. Even
 * zero-sized messages carrying no payload data must be freed, since
 * they are assigned a valid memory space to store internal
 * information.
 *
 * @return 0 is returned upon success, or -EINVAL if @a buf is not a
 * valid message buffer previously allocated by the rt_queue_alloc()
 * service, or the caller did not get ownership of the message through
 * a successful return from rt_queue_recv().
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
    int err;
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
    
    err = xnheap_test_and_free(&q->bufpool,
			       ((rt_queue_msg_t *)buf) - 1,
			       &__queue_check_msg);
    if (err == -EBUSY)
	/* Release failed due to non-zero refcount; this is not an
	 * error from the interface POV. */
	err = 0;

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
 * - -ENOMEM is returned if queuing the message would exceed the limit
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
	err = -ENOMEM;
	goto unlock_and_exit;
	}

    msg = ((rt_queue_msg_t *)buf) - 1;

    if (msg->refcount == 0)
	{
	/* Cheap test: the sender cannot own the message. Bail out. */
	err = -EINVAL;
	goto unlock_and_exit;
	}

    /* Message buffer ownership is being transferred from the sender to
       the receiver(s) here; so we need to update the reference count
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
 * @fn ssize_t rt_queue_recv(RT_QUEUE *q,
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
 * to arrive (see note). Passing TM_INFINITE causes the caller to
 * block indefinitely until some message is eventually
 * available. Passing TM_NONBLOCK causes the service to return
 * immediately without waiting if no message is available on entry.
 *
 * @return The number of bytes available from the received message is
 * returned upon success. Zero is a possible value corresponding to a
 * zero-sized message passed to rt_queue_send(). Otherwise:
 *
 * - -EINVAL is returned if @a q is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted queue descriptor.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no message is available within the specified amount
 * of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no message is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * - -EPERM is returned if this service should block, but was called
 * from a context which cannot sleep (e.g. interrupt, non-realtime or
 * scheduler locked).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to TM_NONBLOCK.
 *
 * - Kernel-based task
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
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
        {
	msg = link2rtmsg(holder);
	msg->refcount++;
	}
    else
	{
	if (timeout == TM_NONBLOCK)
	    {
	    err = -EWOULDBLOCK;;
	    goto unlock_and_exit;
	    }

	if (xnpod_unblockable_p())
	    {
	    err = -EPERM;
	    goto unlock_and_exit;
	    }

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
 * @fn int rt_queue_inquire(RT_QUEUE *q, RT_QUEUE_INFO *info)
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
    info->poolsize = xnheap_size(&q->bufpool);
    info->mode = q->mode;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_queue_bind(RT_QUEUE *q,
			const char *name)
 * @brief Bind to a shared message queue.
 *
 * This user-space only service retrieves the uniform descriptor of a
 * given shared RTAI message queue identified by its symbolic name. If
 * the queue does not exist on entry, this service blocks the caller
 * until a queue of the given name is created.
 *
 * @param name A valid NULL-terminated name which identifies the
 * queue to bind to.
 *
 * @param q The address of a queue descriptor retrieved by the
 * operation. Contents of this memory is undefined upon failure.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EFAULT is returned if @a q or @a name is referencing invalid
 * memory.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before the retrieval has completed.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - User-space task (switches to primary mode)
 *
 * Rescheduling: always unless the request is immediately satisfied.
 */

/**
 * @fn int rt_queue_unbind(RT_QUEUE *q)
 *
 * @brief Unbind from a shared message queue.
 *
 * This user-space only service unbinds the calling task from the
 * message queue object previously retrieved by a call to
 * rt_queue_bind().
 *
 * Unbinding from a message queue when it is no more needed is
 * especially important in order to properly release the mapping
 * resources used to attach the shared queue memory to the caller's
 * address space.
 *
 * @param q The address of a queue descriptor to unbind from.
 *
 * @return 0 is always returned.
 *
 * This service can be called from:
 *
 * - User-space task.
 *
 * Rescheduling: never.
 */

int __queue_pkg_init (void)

{
    return 0;
}

void __queue_pkg_cleanup (void)

{
}

/*@}*/

EXPORT_SYMBOL(rt_queue_create);
EXPORT_SYMBOL(rt_queue_delete);
EXPORT_SYMBOL(rt_queue_alloc);
EXPORT_SYMBOL(rt_queue_free);
EXPORT_SYMBOL(rt_queue_send);
EXPORT_SYMBOL(rt_queue_recv);
EXPORT_SYMBOL(rt_queue_inquire);
