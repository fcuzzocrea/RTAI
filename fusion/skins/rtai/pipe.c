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
 */

#include <nucleus/pod.h>
#include <nucleus/heap.h>
#include <rtai/pipe.h>

static xnheap_t *__pipe_heap = &kheap;

static unsigned __pipe_flush_virq;

static xnqueue_t __pipe_flush_q;

static inline ssize_t __pipe_flush (RT_PIPE *pipe)

{
    ssize_t nbytes;

    nbytes = xnbridge_msend(pipe->minor,pipe->buffer,pipe->fillsz + sizeof(RT_PIPE_MSG),RT_PIPE_NORMAL);
    rt_pipe_free(pipe->buffer);
    pipe->buffer = NULL;
    pipe->fillsz = 0;

    return nbytes;
}

static void __pipe_flush_handler (void)

{
    xnholder_t *holder;
    spl_t s;

    splhigh(s);

    /* Flush all pipes with pending messages. */

    while ((holder = getq(&__pipe_flush_q)) != NULL)
	{
	RT_PIPE *pipe = link2pipe(holder);
	__pipe_flush(pipe);	/* Cannot do anything upon error here. */
	clear_bit(0,&pipe->flushable);
	}

    splexit(s);
}

static void *__pipe_alloc_handler (int bminor,
				   size_t size,
				   void *cookie) {

    /* Allocate memory for the incoming message. */
    return xnheap_alloc(__pipe_heap,size,XNHEAP_NOWAIT);
}

static int __pipe_output_handler (int bminor,
				  xnbridge_mh_t *mh,
				  int onerror,
				  void *cookie)
{
    /* Free memory from output/discarded message. */
    xnheap_free(__pipe_heap,mh);
    return 0;
}

int __pipe_pkg_init (void)

{
    initq(&__pipe_flush_q);
    __pipe_flush_virq = rthal_request_srq(0,&__pipe_flush_handler);

    return __pipe_flush_virq > 0 ? 0 : -EBUSY;
}

void __pipe_pkg_cleanup (void)

{
    rthal_free_srq(__pipe_flush_virq);
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
 * @param pipe The address of a pipe descriptor RTAI will use to store
 * the pipe-related data.  This descriptor must always be valid while
 * the pipe is active therefore it must be allocated in permanent
 * memory.
 *
 * @param minor The minor number of the device associated with the pipe.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -ENODEV is returned if @a minor is not a valid minor number of
 * for pipe pseudo-devices.
 *
 * - -EBUSY is returned if @a minor is already open.
 *
 * Context: This routine can be called on behalf of a task or from the
 * initialization code.
 */

int rt_pipe_open (RT_PIPE *pipe,
		  int minor)
{
    int err;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    pipe->minor = minor;
    pipe->buffer = NULL;
    pipe->fillsz = 0;
    pipe->flushable = 0;

    err = xnbridge_mconnect(minor,
			    &__pipe_output_handler,
			    NULL,
			    &__pipe_alloc_handler,
			    pipe);
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
 * Context: This routine can be called on behalf of a task or from the
 * initialization code.
 */

int rt_pipe_close (RT_PIPE *pipe)

{
    int err;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	err = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (test_and_clear_bit(0,&pipe->flushable))
	{
	/* Purge data waiting for flush. */
	removeq(&__pipe_flush_q,&pipe->link);
	rt_pipe_free(pipe->buffer);
	}

    err = xnbridge_mdisconnect(pipe->minor);

    rtai_mark_deleted(pipe);

 unlock_and_exit:

    splexit(s);

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
 * by the message by respectively using the RT_PIPE_MSGPTR() and
 * RT_PIPE_MSGSIZE() macros.
 *
 * @param timeout The number of clock ticks to wait for some message
 * to arrive (see note). Passing RT_TIME_INFINITE causes the caller to
 * block indefinitely until some data is eventually available. Passing
 * RT_TIME_NONBLOCK causes the service to return immediately without
 * waiting if no data is available on entry.
 *
 * @return The number of read bytes available from the received
 * message is returned upon success; this value will be equal to
 * RT_PIPE_MSGSIZE(*msgp). Otherwise:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * RT_TIME_NONBLOCK and no data is available within the specified
 * amount of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * RT_TIME_NONBLOCK and no data is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any data was available.
 *
 * Side-effect: This routine calls the rescheduling procedure if no
 * data is available on entry and @a timeout is different from
 * RT_TIME_NONBLOCK.

 * Context: This routine can be called on behalf of a task.  It can
 * also be called on behalf of an interrupt context or from the
 * initialization code provided @a timeout is equal to
 * RT_TIME_NONBLOCK.
 *
 * @note This service is sensitive to the current operation mode of
 * the system timer, as defined by the rt_timer_start() service. In
 * periodic mode, clock ticks are expressed as periodic jiffies. In
 * oneshot mode, clock ticks are expressed in nanoseconds.
 */

ssize_t rt_pipe_read (RT_PIPE *pipe,
		      RT_PIPE_MSG **msgp,
		      RTIME timeout)
{
    ssize_t n;
    spl_t s;

    if (timeout != RT_TIME_NONBLOCK)
	xnpod_check_context(XNPOD_THREAD_CONTEXT);

    splhigh(s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    n = xnbridge_mrecv(pipe->minor,msgp,timeout);

 unlock_and_exit:

    splexit(s);

    return n;
}

/**
 * @fn int rt_pipe_write(RT_PIPE *pipe,
                         RT_PIPE_MSG *msg,
			    size_t size,
			    int flags)
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
 * @param size The size in bytes of the message. Zero is a valid
 * value, in which case the service returns immediately without
 * sending any message.
 *
 * @param flags A set of flags affecting the operation:
 *
 * - RT_PIPE_URGENT causes the message to be prepended to the output
 * queue, ensuring a LIFO ordering.
 *
 * - RT_PIPE_NORMAL causes the message to be appended to the output
 * queue, ensuring a FIFO ordering.
 *
 * @return Upon success, this service returns @a size if the latter is
 * non-zero, or the number of bytes flushed otherwise. Upon error, one
 * of the following error codes is returned:
 *
 * - -EINVAL is returned if @a pipe is not a pipe descriptor.
 *
 * - -EIO is returned if the user-space side of the pipe is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * Side-effect: rt_pipe_write() causes any data buffered by
 * rt_pipe_stream() to be flushed prior to sending the message. For
 * this reason, rt_pipe_write() can return a non-zero byte count to
 * the caller if some pending data has been flushed, even if @a size
 * was zero on entry.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

ssize_t rt_pipe_write (RT_PIPE *pipe,
		       RT_PIPE_MSG *msg,
		       size_t size,
		       int flags)
{
    ssize_t n = 0;
    spl_t s;

    splhigh(s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (test_and_clear_bit(0,&pipe->flushable))
	{
	removeq(&__pipe_flush_q,&pipe->link);
	n = __pipe_flush(pipe);

	if (n < 0)
	    goto unlock_and_exit;
	}

    if (size > 0)
	/* We need to add the size of the message header here. */
	n = xnbridge_msend(pipe->minor,msg,size + sizeof(RT_PIPE_MSG),flags);

 unlock_and_exit:

    splexit(s);

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
 * re-entered after the real-time kernel enters a quiescent state.
 *
 * Data buffers sent by the rt_pipe_stream() service are always
 * transmitted in FIFO order (i.e. RT_PIPE_NORMAL mode).
 *
 * @param pipe The descriptor address of the pipe to write to.
 *
 * @param buf The address of the first data byte to send. The
 * data will be copied to an internal buffer before emission.
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
 * - -EIO is returned if the user-space side of the pipe is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
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

    splhigh(s);

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

	    if (test_and_clear_bit(0,&pipe->flushable))
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

	memcpy(RT_PIPE_MSGPTR(pipe->buffer) + pipe->fillsz,(caddr_t)buf + outbytes,n);
	pipe->fillsz += n;
	outbytes += n;
	size -= n;
	}

    /* The flushable bit is not that elegant, but we must make sure
       that we won't enqueue the pipe descriptor twice in the flush
       queue, but we still have to enqueue it before the virq is made
       pending if necessary since it could preempt a Linux-based
       caller, so... */

    if (pipe->fillsz > 0 && !test_and_set_bit(0,&pipe->flushable))
	{
	appendq(&__pipe_flush_q,&pipe->link);
	rthal_pend_linux_srq(__pipe_flush_virq);
	}

 unlock_and_exit:

    splexit(s);

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
 * - -EIO is returned if the user-space side of the pipe is not yet
 * open.
 *
 * - -EIDRM is returned if @a pipe is a closed pipe descriptor.
 *
 * - -ENODEV or -EBADF are returned if @a pipe is scrambled.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

ssize_t rt_pipe_flush (RT_PIPE *pipe)

{
    ssize_t n = 0;
    spl_t s;

    splhigh(s);

    pipe = rtai_h2obj_validate(pipe,RTAI_PIPE_MAGIC,RT_PIPE);

    if (!pipe)
	{
	n = rtai_handle_error(pipe,RTAI_PIPE_MAGIC,RT_PIPE);
	goto unlock_and_exit;
	}

    if (test_and_clear_bit(0,&pipe->flushable))
	{
	removeq(&__pipe_flush_q,&pipe->link);
	n = __pipe_flush(pipe);
	}

 unlock_and_exit:

    splexit(s);

    return n <= 0 ? n : n - sizeof(RT_PIPE_MSG);
}

/**
 * @fn RT_PIPE_MSG *rt_pipe_alloc(size_t size)
 *
 * @brief Allocate a message buffer.
 *
 * This service allocates a message buffer from the system heap which
 * can be subsequently filled then passed to rt_pipe_write() for
 * sending. The beginning of the available data area of @a size
 * contiguous bytes is accessible from RT_PIPE_MSGPTR(msg).
 *
 * @param size The requested size in bytes of the buffer.
 *
 * @return The address of the allocated message buffer upon success,
 * or NULL if the allocation fails.
 *
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

RT_PIPE_MSG *rt_pipe_alloc (size_t size)

{
    RT_PIPE_MSG *msg = (RT_PIPE_MSG *)xnheap_alloc(__pipe_heap,size,XNHEAP_NOWAIT);

    if (msg)
	{
	inith(&msg->link);
	msg->size = 0;
	}

    return msg;
}

/**
 * @fn int rt_pipe_free(RT_PIPE_MSG *msg)
 *
 * @brief Free a message buffer.
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
 * Context: This routine can be called on behalf of a task, interrupt
 * context or from the initialization code.
 */

int rt_pipe_free (RT_PIPE_MSG *msg) {

    return xnheap_free(__pipe_heap,msg) == 0 ? 0 : -EINVAL;
}
