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
 * \ingroup heap
 */

/*!
 * \ingroup native
 * \defgroup heap Memory heap services.
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
	err = xnheap_init_shared(&heap->heap_base,
				 heapsize,
				 (mode & H_DMA) ? GFP_DMA : 0);
	if (err)
	    return err;
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

#ifdef __KERNEL__
    if (heap->mode & H_SHARED)
	err = xnheap_destroy_shared(&heap->heap_base);
    else
#endif /* __KERNEL__ */
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
