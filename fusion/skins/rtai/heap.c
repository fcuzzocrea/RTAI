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
 * \ingroup native_heap
 */

/*!
 * \ingroup native
 * \defgroup native_heap Memory heap services.
 *
 * Memory heap services.
 *
 * Memory heaps are built over Xenomai's heap objects, which in turn
 * provide the needed support for sharing a memory area between kernel
 * and user-space using direct memory mapping.
 *
 *@{*/

#include <nucleus/pod.h>
#include <rtai/task.h>
#include <rtai/heap.h>
#include <rtai/registry.h>

int __heap_pkg_init (void)

{
    return 0;
}

void __heap_pkg_cleanup (void)

{
}

static void __heap_flush_private (xnheap_t *heap,
				  void *heapmem,
				  u_long heapsize,
				  void *cookie)
{
    xnarch_sysfree(heapmem,heapsize);
}

/*! 
 * \fn int rt_heap_create(RT_HEAP *heap,
                          const char *name,
                          size_t heapsize,
                          int mode);
 * \brief Create a real-time memory heap.
 *
 * Initializes a memory heap suitable for time-bounded allocation
 * requests of dynamic memory. Memory heaps can be local to the kernel
 * space, or shared between kernel and user-space.
 *
 * @param heap The address of a heap descriptor RTAI will use to store
 * the heap-related data.  This descriptor must always be valid while
 * the heap is active therefore it must be allocated in permanent
 * memory.
 *
 * @param name An ASCII string standing for the symbolic name of the
 * heap. When non-NULL and non-empty, this string is copied to a safe
 * place into the descriptor, and passed to the registry package if
 * enabled for indexing the created heap.
 *
 * @param heapsize The size (in bytes) of the buffer pool which is
 * going to be pre-allocated to the heap. Memory buffers will be
 * claimed and released to this pool.  The buffer pool is not
 * extensible, so this value must be compatible with the highest
 * memory pressure that could be expected.
 *
 * @param mode The heap creation mode. The following flags can be
 * OR'ed into this bitmask, each of them affecting the new heap:
 *
 * - H_FIFO makes tasks pend in FIFO order on the heap when waiting
 * for available buffers.
 *
 * - H_PRIO makes tasks pend in priority order on the heap when
 * waiting for available buffers.
 *
 * - H_SHARED causes the heap to be sharable between kernel and
 * user-space tasks. Otherwise, the new heap is only available for
 * kernel-based usage. This feature requires the real-time support in
 * user-space to be configured in (CONFIG_OPT_RTAI_FUSION).
 *
 * - H_DMA causes the buffer pool associated to the heap to be
 * allocated in physically contiguous memory, suitable for DMA
 * operations with I/O devices. A 128Kb limit exists for @a heapsize
 * when this flag is passed.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EEXIST is returned if the @a name is already in use by some
 * registered object.
 *
 * - -EINVAL is returned if @a heapsize is null.
 *
 * - -ENOMEM is returned if not enough system memory is available to
 * create the heap. Additionally, and if H_SHARED has been passed in
 * @a mode, errors while mapping the buffer pool in the caller's
 * address space might beget this return code too.
 *
 * - -ENOSYS is returned if @a mode specifies H_SHARED, but the
 * real-time support in user-space is unavailable.
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

int rt_heap_create (RT_HEAP *heap,
		    const char *name,
		    size_t heapsize,
		    int mode)
{
    int err;

    xnpod_check_context(XNPOD_ROOT_CONTEXT);

    if (heapsize == 0)
	return -EINVAL;

#ifdef __KERNEL__
    if (mode & H_SHARED)
	{
#ifdef CONFIG_RTAI_OPT_FUSION
	err = xnheap_init_shared(&heap->heap_base,
				 heapsize,
				 (mode & H_DMA) ? GFP_DMA : 0);
	if (err)
	    return err;
#else /* !CONFIG_RTAI_OPT_FUSION */
	return -ENOSYS;
#endif /* CONFIG_RTAI_OPT_FUSION */
	}
    else
#endif /* __KERNEL__ */
	{
	void *heapmem = xnarch_sysalloc(heapsize);

	if (!heapmem)
	    return -ENOMEM;

	err = xnheap_init(&heap->heap_base,
			  heapmem,
			  heapsize,
			  PAGE_SIZE); /* Use natural page size */
	if (err)
	    {
	    xnarch_sysfree(heapmem,heapsize);
	    return err;
	    }
	}

    xnsynch_init(&heap->synch_base,mode & (H_PRIO|H_FIFO));
    heap->handle = 0;  /* i.e. (still) unregistered heap. */
    heap->magic = RTAI_HEAP_MAGIC;
    heap->mode = mode;
    xnobject_copy_name(heap->name,name);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    /* <!> Since rt_register_enter() may reschedule, only register
       complete objects, so that the registry cannot return handles to
       half-baked objects... */

    if (name && *name)
        {
        err = rt_registry_enter(heap->name,heap,&heap->handle);

        if (err)
            rt_heap_delete(heap);
        }
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    return err;
}

/**
 * @fn int rt_heap_delete(RT_HEAP *heap)
 * @brief Delete a real-time heap.
 *
 * Destroy a heap and release all the tasks currently pending on it.
 * A heap exists in the system since rt_heap_create() has been called
 * to create it, so this service must be called in order to destroy it
 * afterwards.
 *
 * @param heap The descriptor address of the affected heap.
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a heap descriptor.
 *
 * - -EIDRM is returned if @a heap is a deleted heap descriptor.
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

int rt_heap_delete (RT_HEAP *heap)

{
    int err = 0, rc;
    spl_t s;

    xnpod_check_context(XNPOD_THREAD_CONTEXT);

    xnlock_get_irqsave(&nklock,s);

    heap = rtai_h2obj_validate(heap,RTAI_HEAP_MAGIC,RT_HEAP);

    if (!heap)
        {
        err = rtai_handle_error(heap,RTAI_HEAP_MAGIC,RT_HEAP);
        goto unlock_and_exit;
        }

#if defined (__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    if (heap->mode & H_SHARED)
	err = xnheap_destroy_shared(&heap->heap_base);
    else
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
	err = xnheap_destroy(&heap->heap_base,&__heap_flush_private,NULL);

    if (err)
	goto unlock_and_exit;

    rc = xnsynch_destroy(&heap->synch_base);

#if CONFIG_RTAI_OPT_NATIVE_REGISTRY
    if (heap->handle)
        rt_registry_remove(heap->handle);
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    rtai_mark_deleted(heap);

    if (rc == XNSYNCH_RESCHED)
        /* Some task has been woken up as a result of the deletion:
           reschedule now. */
        xnpod_schedule();

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_heap_alloc(RT_HEAP *heap,
                         size_t size,
                         RTIME timeout,
                         void **bufp)
 *
 * @brief Allocate a buffer.
 *
 * This service allocates a buffer from the heap's internal pool.
 *
 * @param heap The descriptor address of the heap to allocate a buffer
 * from.
 *
 * @param size The requested size in bytes of the buffer.
 *
 * @param timeout The number of clock ticks to wait for a buffer of
 * sufficient size to be available (see note). Passing
 * RT_TIME_INFINITE causes the caller to block indefinitely until some
 * buffer is eventually available. Passing RT_TIME_NONBLOCK causes the
 * service to return immediately without waiting if no buffer is
 * available on entry.
 *
 * @param bufp A pointer to a memory location which will be written
 * upon success with the address of the allocated buffer. The buffer
 * should be freed using rt_heap_free().
 *
 * @return 0 is returned upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a heap descriptor.
 *
 * - -EIDRM is returned if @a q is a deleted heap descriptor.
 *
 * - -ETIMEDOUT is returned if @a timeout is different from
 * RT_TIME_NONBLOCK and no buffer is available within the specified
 * amount of time.
 *
 * - -EWOULDBLOCK is returned if @a timeout is equal to
 * RT_TIME_NONBLOCK and no buffer is immediately available on entry.
 *
 * - -EINTR is returned if rt_task_unblock() has been called for the
 * waiting task before any buffer was available.
 *
 * Environments:
 *
 * This service can be called from:
 *
 * - Kernel module initialization/cleanup code
 * - Interrupt service routine
 *   only if @a timeout is equal to RT_TIME_NONBLOCK.
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

int rt_heap_alloc (RT_HEAP *heap,
		   size_t size,
		   RTIME timeout,
		   void **bufp)
{
    void *buf = NULL;
    RT_TASK *task;
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = rtai_h2obj_validate(heap,RTAI_HEAP_MAGIC,RT_HEAP);

    if (!heap)
	{
	err = -EINVAL;
	goto unlock_and_exit;
	}

    buf = xnheap_alloc(&heap->heap_base,size);

    if (buf)
	goto unlock_and_exit;

    if (timeout == RT_TIME_NONBLOCK)
	{
	err = -EWOULDBLOCK;
	goto unlock_and_exit;
	}

    task = rtai_current_task();
    task->wait_args.heap.size = size;
    task->wait_args.heap.buf = NULL;
    xnsynch_sleep_on(&heap->synch_base,timeout);

    if (xnthread_test_flags(&task->thread_base,XNRMID))
	err = -EIDRM; /* Heap deleted while pending. */
    else if (xnthread_test_flags(&task->thread_base,XNTIMEO))
	err = -ETIMEDOUT; /* Timeout.*/
    else if (xnthread_test_flags(&task->thread_base,XNBREAK))
	err = -EINTR; /* Unblocked.*/
    else
	buf = task->wait_args.heap.buf;

 unlock_and_exit:

    *bufp = buf;

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_heap_free(RT_HEAP *heap,
                        void *buf)
 *
 * @brief Free a buffer.
 *
 * This service releases a buffer to the heap's internal pool. If some
 * task is currently waiting for a buffer so that it's pending request
 * could be satisfied as a result of the release, it is immediately
 * resumed.
 *
 * @param heap The address of the heap descriptor to which the buffer @a buf
 * belong.
 *
 * @param buf The address of the buffer to free.
 *
 * @return 0 is returned upon success, or -EINVAL if @a buf is not a
 * valid buffer previously allocated by the rt_heap_alloc() service.
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

int rt_heap_free (RT_HEAP *heap,
		  void *buf)
{
    int err, nwake;
    spl_t s;

    if (buf == NULL)
	return -EINVAL;

    xnlock_get_irqsave(&nklock,s);

    heap = rtai_h2obj_validate(heap,RTAI_HEAP_MAGIC,RT_HEAP);

    if (!heap)
        {
        err = rtai_handle_error(heap,RTAI_HEAP_MAGIC,RT_HEAP);
        goto unlock_and_exit;
        }
    
    err = xnheap_free(&heap->heap_base,buf);

    if (!err && xnsynch_nsleepers(&heap->synch_base) > 0)
	{
	xnpholder_t *holder, *nholder;
	
	nholder = getheadpq(xnsynch_wait_queue(&heap->synch_base));
	nwake = 0;

	while ((holder = nholder) != NULL)
	    {
	    RT_TASK *sleeper = thread2rtask(link2thread(holder,plink));
	    void *buf;

	    buf = xnheap_alloc(&heap->heap_base,
			       sleeper->wait_args.heap.size);
	    if (buf)
		{
		nholder = xnsynch_wakeup_this_sleeper(&heap->synch_base,holder);
		sleeper->wait_args.heap.buf = buf;
		nwake++;
		}
	    else
		nholder = nextpq(xnsynch_wait_queue(&heap->synch_base),holder);
	    }

	if (nwake > 0)
	    xnpod_schedule();
	}

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

/**
 * @fn int rt_heap_inquire(RT_HEAP *heap, RT_HEAP_INFO *info)
 * @brief Inquire about a heap.
 *
 * Return various information about the status of a given heap.
 *
 * @param heap The descriptor address of the inquired heap.
 *
 * @param info The address of a structure the heap information will
 * be written to.

 * @return 0 is returned and status information is written to the
 * structure pointed at by @a info upon success. Otherwise:
 *
 * - -EINVAL is returned if @a heap is not a message queue descriptor.
 *
 * - -EIDRM is returned if @a heap is a deleted queue descriptor.
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

int rt_heap_inquire (RT_HEAP *heap,
		     RT_HEAP_INFO *info)
{
    int err = 0;
    spl_t s;

    xnlock_get_irqsave(&nklock,s);

    heap = rtai_h2obj_validate(heap,RTAI_HEAP_MAGIC,RT_HEAP);

    if (!heap)
        {
        err = rtai_handle_error(heap,RTAI_HEAP_MAGIC,RT_HEAP);
        goto unlock_and_exit;
        }
    
    strcpy(info->name,heap->name);
    info->nwaiters = xnsynch_nsleepers(&heap->synch_base);
    info->heapsize = heap->heap_base.extentsize;
    info->mode = heap->mode;

 unlock_and_exit:

    xnlock_put_irqrestore(&nklock,s);

    return err;
}

EXPORT_SYMBOL(rt_heap_create);
EXPORT_SYMBOL(rt_heap_delete);
EXPORT_SYMBOL(rt_heap_alloc);
EXPORT_SYMBOL(rt_heap_free);
EXPORT_SYMBOL(rt_heap_inquire);
