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
 * \ingroup pipe
 */

/*!
 * \ingroup native
 * \defgroup pipe Pipe management services.
 *
 * Pipe management services.
 *
 * Message pipes are an improved replacement for the legacy
 * RT-FIFOS. A message pipe is a two-way communication channel between
 * a kernel-based real-time thread and a user-space process. Pipes can
 * be operated in a message-oriented fashion so that message
 * boundaries are preserved, and also in byte streaming mode from
 * kernel to user-space for optimal throughput.
 *
 * Kernel-based RTAI tasks open their side of the pipe using the
 * rt_pipe_open() service; user-space processes do the same by opening
 * the /dev/rtpN special devices, where N is the minor number agreed
 * between both ends of each pipe.
 *
 *@{*/

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <rtai/pipe.h>

static xnheap_t *__pipe_heap = &kheap;

static int __pipe_flush_srq;

static DECLARE_XNQUEUE(__pipe_flush_q);

static inline ssize_t __pipe_flush (RT_PIPE *pipe)

{
    ssize_t nbytes = pipe->fillsz + sizeof(RT_PIPE_MSG);
    void *buffer = pipe->buffer;

    pipe->buffer = NULL;
    pipe->fillsz = 0;

    return xnpipe_send(pipe->minor,buffer,nbytes,P_NORMAL);
    /* The buffer will be freed by the output handler. */
}

static void __pipe_flush_handler (void *cookie)

{
    xnholder_t *holder;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    /* Flush all pipes with pending messages. */

    while ((holder = getq(&__pipe_flush_q)) != NULL)
	{
	RT_PIPE *pipe = link2rtpipe(holder);
	__clear_bit(0,&pipe->flushable);
	xnlock_put_irqrestore(&nklock,s);
	__pipe_flush(pipe);	/* Cannot do anything upon error here. */
	xnlock_get_irqsave(&nklock,s);
	}

    xnlock_put_irqrestore(&nklock,s);
}

static void *__pipe_alloc_handler (int bminor,
				   size_t size,
				   void *cookie) {

    /* Allocate memory for the incoming message. */
    return xnheap_alloc(__pipe_heap,size);
}

static int __pipe_output_handler (int bminor,
				  xnpipe_mh_t *mh,
				  int onerror,
				  void *cookie)
{
    /* Free memory from output/discarded message. */
    xnheap_free(__pipe_heap,mh);
    return 0;
}

int __pipe_pkg_init (void)

{
    __pipe_flush_srq = rthal_request_srq(&__pipe_flush_handler);

    if (__pipe_flush_srq <= 0)
	return -EBUSY;

    return 0;
}

void __pipe_pkg_cleanup (void)

{
    xnpipe_setup(NULL,NULL);
    rthal_release_srq(__pipe_flush_srq);
}

/**
 * @fn int rt_pipe_open(RT_PIPE *pipe, int minor)
 * @brief Open a pipe.
 *
 * This service opens a bi-directional communication channel allowing
 * data exchange between real-time tasks and regular user-space
 * processes. Pipes natively preserve message boundaries, but can also
 * be used in byte stream mode from kernel to user space.
 *
 * rt_pipe_open() always returns immediately, even if the user-space
 * side of the same pipe has not been opened yet. On the contrary, the
 * user-space opener might be suspended until rt_pipe_open() is issued
 * on the same pipe from kernel space, unless O_NONBLOCK has been
 * specified to open(2).
 *
 * @param pipe The address of a pipe descriptor RTAI will use to store
 * the pipe-related data.  This descriptor must always be valid while
 * the pipe is active therefore it must be allocated in permanent
 * memory.
 *
 * @param minor The minor number of the device associated with the pipe.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENODEV is returned if @a minor is not a valid minor number for
 * the pipe pseudo-device (i.e. /dev/rtp*).
 *
 * - -EBUSY is returned if @a minor is already open.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int rt_pipe_open (RT_PIPE *pipe,
		  int minor)
{
    int err;

    if (xnpod_asynch_p())
	return -EPERM;

    pipe->minor = minor;
    pipe->buffer = NULL;
    pipe->fillsz = 0;
    pipe->flushable = 0;
    pipe->magic = 0;

    err = xnpipe_connect(minor,
			   &__pipe_output_handler,
			   NULL,
			   &__pipe_alloc_handler,
			   pipe);

    if (!err)
	pipe->magic = RTAI_PIPE_MAGIC;

    return err;
}

/**
 * @fn int rt_pipe_close(RT_PIPE *pipe)
 *
 * @brief Close a pipe.
 *
 * This service closes a pipe previously opened by rt_pipe_open().
 * Data pending for transmission to user-space are lost.
 *
 * @param pipe The descriptor address of the affected pipe.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF can be returned if @a pipe is scrambled.
 *
 * - -EPERM is returned if this service was called from an
 * asynchronous context.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */

int rt_pipe_close (RT_PIPE *pipe)

{
    int err;
    spl_t s;

    if (xnpod_asynch_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	err = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (__test_and_clear_bit(0,&pipe->flushable))
	{
	/* Purge data waiting for flush. */
	removeq(&__pipe_flush_q,&pipe->link);
	rt_pipe_free(pipe->buffer);
	}

    err = xnpipe_disconnect(pipe->minor);

    rtai_mark_deleted(pipe);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_pipe_read(RT_PIPE *pipe,
                        RT_PIPE_MSG **msgp,
			RTIME timeout)
 *
 * @brief Read a message from a pipe.
 *
 * This service retrieves the next message sent from the user-space
 * side of the pipe. rt_pipe_read() always preserves message
 * boundaries, which means that all data sent through the same
 * write(2) operation on the user-space side will be gathered in a
 * single message by this service.
 *
 * Unless otherwise specified, the caller is blocked for a given
 * amount of time if no data is immediately available on entry.
 *
 * @param pipe The descriptor address of the pipe to read from.
 *
 * @param msgp A pointer to a memory location which will be written
 * upon success with the address of the received message. Once
 * consumed, the message space should be freed using rt_pipe_free().
 * The application code can retrieve the actual data and size carried
 * by the message by respectively using the P_MSGPTR() and P_MSGSIZE()
 * macros.
 *
 * @param timeout The number of clock ticks to wait for some message
 * to arrive (see note). Passing TM_INFINITE causes the caller to
 * block indefinitely until some data is eventually available. Passing
 * TM_NONBLOCK causes the service to return immediately without
 * waiting if no data is available on entry.
 *
 * @return The number of read bytes available from the received
 * message is returned upon success; this value will be equal to
 * P_MSGSIZE(*msgp). Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * TM_NONBLOCK and no data is available within the specified amount of
 * time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to TM_NONBLOCK
 * and no data is immediately available on entry.
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
 *
 * Rescheduling: always unless the request is immediately satisfied or
 * @a timeout specifies a non-blocking operation.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are interpreted as periodic jiffies. In
 * oneshot mode, clock ticks are interpreted as nanoseconds.
 */

ssize_t rt_pipe_read (RT_PIPE *pipe,
		      RT_PIPE_MSG **msgp,
		      RTIME timeout)
{
    ssize_t n;
    spl_t s;

    if (timeout != TM_NONBLOCK && xnpod_unblockable_p())
	return -EPERM;

    xnlock_get_irqsave(&nklock,s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    n = xnpipe_recv(pipe->minor,msgp,timeout);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return n;
}

/**
 * @fn int rt_pipe_write(RT_PIPE *pipe,
                         RT_PIPE_MSG *msg,
			 size_t size,
			 int mode)
 *
 * @brief Write a message to a pipe.
 *
 * This service writes a complete message to be received by the
 * user-space side of the pipe. rt_pipe_write() always preserves
 * message boundaries, which means that all data sent through a single
 * call of this service will be gathered in a single read(2) operation
 * on the user-space side.
 *
 * @param pipe The descriptor address of the pipe to write to.
 *
 * @param msg The address of the message to be sent.  The message
 * space must have been allocated using the rt_pipe_alloc() service.
 * Once passed to rt_pipe_write(), the memory pointed to by @a msg is
 * no more under the control of the application code and thus should
 * not be referenced by it anymore; deallocation of this memory will
 * be automatically handled as needed.
 *
 * @param size The size in bytes of the message (payload data
 * only). Zero is a valid value, in which case the service returns
 * immediately without sending any message.
 *
 * Additionally, rt_pipe_write() causes any data buffered by
 * rt_pipe_stream() to be flushed prior to sending the message. For
 * this reason, rt_pipe_write() can return a non-zero byte count to
 * the caller if some pending data has been flushed, even if @a size
 * was zero on entry.
 *
 * @param mode A set of flags affecting the operation:
 *
 * - P_URGENT causes the message to be prepended to the output
 * queue, ensuring a LIFO ordering.
 *
 * - P_NORMAL causes the message to be appended to the output
 * queue, ensuring a FIFO ordering.
 *
 * @return Upon success, this service returns @a size if the latter is
 * non-zero, or the number of bytes flushed otherwise. Upon error, one
 * of the following error codes is returned:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EPIPE is returned if the user-space side of the pipe is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */

ssize_t rt_pipe_write (RT_PIPE *pipe,
		       RT_PIPE_MSG *msg,
		       size_t size,
		       int mode)
{
    ssize_t n = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (__test_and_clear_bit(0,&pipe->flushable))
	{
	removeq(&__pipe_flush_q,&pipe->link);
	n = __pipe_flush(pipe);

	if (n < 0)
	    goto unlock_and_exit;
	}

    if (size > 0)
	/* We need to add the size of the message header here. */
	n = xnpipe_send(pipe->minor,msg,size + sizeof(RT_PIPE_MSG),mode);

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return n <= 0 ? n : n - sizeof(RT_PIPE_MSG);
}

/**
 * @fn int rt_pipe_stream(RT_PIPE *pipe, const void *buf, size_t size)
 *
 * @brief Stream bytes to a pipe.
 *
 * This service writes a sequence of bytes to be received by the
 * user-space side of the pipe. Unlike rt_pipe_write(), this service
 * does not preserve message boundaries. Instead, an internal buffer
 * is filled on the fly with the data. The actual sending may be
 * delayed until the internal buffer is full, or the Linux kernel is
 * re-entered after the real-time system enters a quiescent state.
 *
 * Data buffers sent by the rt_pipe_stream() service are always
 * transmitted in FIFO order (i.e. P_NORMAL mode).
 *
 * @param pipe The descriptor address of the pipe to write to.
 *
 * @param buf The address of the first data byte to send. The
 * data will be copied to an internal buffer before transmission.
 *
 * @param size The size in bytes of the buffer. Zero is a valid value,
 * in which case the service returns immediately without buffering any
 * data.
 *
 * @return The number of sent bytes upon success; this value will be
 * equal to @a size. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EPIPE is returned if the user-space side of the pipe is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * - -ENOSYS is returned if the byte streaming mode has been disabled
 * at configuration time by nullifying the size of the pipe buffer
 * (see CONFIG_RTAI_OPT_NATIVE_PIPE_BUFSZ).
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */

ssize_t rt_pipe_stream (RT_PIPE *pipe,
			const void *buf,
			size_t size)
{
    ssize_t outbytes = 0;
    size_t n;
    spl_t s;

#if CONFIG_RTAI_OPT_NATIVE_PIPE_BUFSZ <= 0
    return -ENOSYS;
#else /* CONFIG_RTAI_OPT_NATIVE_PIPE_BUFSZ > 0 */

    xnlock_get_irqsave(&nklock,s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	outbytes = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    while (size > 0)
	{
	if (size >= CONFIG_RTAI_OPT_NATIVE_PIPE_BUFSZ - pipe->fillsz)
	    n = CONFIG_RTAI_OPT_NATIVE_PIPE_BUFSZ - pipe->fillsz;
	else
	    n = size;

	if (n == 0)
	    {
	    ssize_t err = __pipe_flush(pipe);

	    if (__test_and_clear_bit(0,&pipe->flushable))
		removeq(&__pipe_flush_q,&pipe->link);

	    if (err < 0)
		{
		outbytes = err;
		goto unlock_and_exit;
		}

	    continue;
	    }

	if (pipe->buffer == NULL)
	    {
	    pipe->buffer = rt_pipe_alloc(CONFIG_RTAI_OPT_NATIVE_PIPE_BUFSZ);

	    if (pipe->buffer == NULL)
		{
		outbytes = -ENOMEM;
		goto unlock_and_exit;
		}
	    }

	memcpy(P_MSGPTR(pipe->buffer) + pipe->fillsz,(caddr_t)buf + outbytes,n);
	pipe->fillsz += n;
	outbytes += n;
	size -= n;
	}

    /* The flushable bit is not that elegant, but we must make sure
       that we won't enqueue the pipe descriptor twice in the flush
       queue, but we still have to enqueue it before the virq is made
       pending if necessary since it could preempt a Linux-based
       caller, so... */

    if (pipe->fillsz > 0 && !__test_and_set_bit(0,&pipe->flushable))
	{
	appendq(&__pipe_flush_q,&pipe->link);
	rthal_pend_srq(__pipe_flush_srq);
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return outbytes;
#endif /* CONFIG_RTAI_OPT_NATIVE_PIPE_BUFSZ <= 0 */
}

/**
 * @fn int rt_pipe_flush(RT_PIPE *pipe)
 *
 * @brief Flush the pipe.
 *
 * This service flushes any pending data buffered by rt_pipe_stream().
 * The data will be immediately sent to the user-space side of the
 * pipe.
 *
 * @param pipe The descriptor address of the pipe to flush.
 *
 * @return The number of bytes flushed upon success. Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EPIPE is returned if the user-space side of the pipe is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: possible.
 */

ssize_t rt_pipe_flush (RT_PIPE *pipe)

{
    ssize_t n = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (__test_and_clear_bit(0,&pipe->flushable))
	{
	removeq(&__pipe_flush_q,&pipe->link);
	n = __pipe_flush(pipe);
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return n <= 0 ? n : n - sizeof(RT_PIPE_MSG);
}

/**
 * @fn RT_PIPE_MSG *rt_pipe_alloc(size_t size)
 *
 * @brief Allocate a message pipe buffer.
 *
 * This service allocates a message buffer from the system heap which
 * can be subsequently filled by the caller then passed to
 * rt_pipe_write() for sending. The beginning of the available data
 * area of @a size contiguous bytes is accessible from P_MSGPTR(msg).
 *
 * @param size The requested size in bytes of the buffer. This value
 * should represent the size of the payload data.
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
 *
 * Rescheduling: never.
 */

RT_PIPE_MSG *rt_pipe_alloc (size_t size)

{
    RT_PIPE_MSG *msg = (RT_PIPE_MSG *)xnheap_alloc(__pipe_heap,size + sizeof(RT_PIPE_MSG));

    if (msg)
	{
	inith(&msg->link);
	msg->size = size;
	}

    return msg;
}

/**
 * @fn int rt_pipe_free(RT_PIPE_MSG *msg)
 *
 * @brief Free a message pipe buffer.
 *
 * This service releases a message buffer returned by rt_pipe_read()
 * to the system heap.
 *
 * @param msg The address of the message buffer to free.
 *
 * @return 0 is returned upon success, or -EINVAL if @a msg is not a
 * valid message buffer previously allocated by the rt_pipe_alloc()
 * service.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 * - Kernel-based task
 *
 * Rescheduling: never.
 */

int rt_pipe_free (RT_PIPE_MSG *msg)
{
    return xnheap_free(__pipe_heap,msg);
}

/*@}*/

EXPORT_SYMBOL(rt_pipe_open);
EXPORT_SYMBOL(rt_pipe_close);
EXPORT_SYMBOL(rt_pipe_read);
EXPORT_SYMBOL(rt_pipe_write);
EXPORT_SYMBOL(rt_pipe_stream);
EXPORT_SYMBOL(rt_pipe_alloc);
EXPORT_SYMBOL(rt_pipe_free);
