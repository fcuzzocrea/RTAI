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

/*!
 * \defgroup native Native RTAI API.
 *
 * The native RTAI programming interface available to real-time
 * applications.
 *
 */

#include <nucleus/pod.h>
#ifdef __KERNEL__
#include <rtai/syscall.h>
#endif /* __KERNEL__ */
#include <rtai/task.h>
#include <rtai/timer.h>
#include <rtai/registry.h>
#include <rtai/sem.h>
#include <rtai/event.h>
#include <rtai/mutex.h>
#include <rtai/cond.h>
#include <rtai/pipe.h>
#include <rtai/queue.h>
#include <rtai/heap.h>
#include <rtai/alarm.h>
#include <rtai/intr.h>

MODULE_DESCRIPTION("Native RTAI skin");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

#if !defined(__KERNEL__) || !defined(CONFIG_RTAI_OPT_FUSION)
static xnpod_t __rtai_pod;
#endif /* !__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */

static void rtai_shutdown (int xtype)

{
#ifdef CONFIG_RTAI_OPT_NATIVE_INTR
    __intr_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_INTR */

#ifdef CONFIG_RTAI_OPT_NATIVE_ALARM
    __alarm_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_ALARM */

#ifdef CONFIG_RTAI_OPT_NATIVE_HEAP
    __heap_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_HEAP */

#ifdef CONFIG_RTAI_OPT_NATIVE_QUEUE
    __queue_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_QUEUE */

#ifdef CONFIG_RTAI_OPT_NATIVE_PIPE
    __pipe_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_PIPE */

#ifdef CONFIG_RTAI_OPT_NATIVE_COND
    __cond_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_COND */

#ifdef CONFIG_RTAI_OPT_NATIVE_MUTEX
    __mutex_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_MUTEX */

#ifdef CONFIG_RTAI_OPT_NATIVE_EVENT
    __event_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_EVENT */

#ifdef CONFIG_RTAI_OPT_NATIVE_SEM
    __sem_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_SEM */

    __task_pkg_cleanup();

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    __rtai_syscall_cleanup();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

#ifdef CONFIG_RTAI_OPT_NATIVE_REGISTRY
    __registry_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    xnpod_shutdown(xtype);
}

int __fusion_skin_init (void)

{
    int err;

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* The RTAI skin is stacked over the fusion framework. */
    err = xnfusion_attach();
#else /* !(__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */
    /* The RTAI skin is standalone, there is no priority level to
       reserve for interrupt servers in user-space, since there is no
       user-space support in the first place. */
    err = xnpod_init(&__rtai_pod,T_LOPRIO,T_HIPRIO,0);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    if (err)
	goto fail;

    nkpod->svctable.shutdown = &rtai_shutdown;

#ifdef CONFIG_RTAI_OPT_NATIVE_REGISTRY
    err = __registry_pkg_init();

    if (err)
	goto fail;
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

    err = __task_pkg_init();

    if (err)
	goto cleanup_registry;

#ifdef CONFIG_RTAI_OPT_NATIVE_SEM
    err = __sem_pkg_init();

    if (err)
	goto cleanup_task;
#endif /* CONFIG_RTAI_OPT_NATIVE_SEM */

#ifdef CONFIG_RTAI_OPT_NATIVE_EVENT
    err = __event_pkg_init();

    if (err)
	goto cleanup_sem;
#endif /* CONFIG_RTAI_OPT_NATIVE_EVENT */

#ifdef CONFIG_RTAI_OPT_NATIVE_MUTEX
    err = __mutex_pkg_init();

    if (err)
	goto cleanup_event;
#endif /* CONFIG_RTAI_OPT_NATIVE_MUTEX */

#ifdef CONFIG_RTAI_OPT_NATIVE_COND
    err = __cond_pkg_init();

    if (err)
	goto cleanup_mutex;
#endif /* CONFIG_RTAI_OPT_NATIVE_MUTEX */

#ifdef CONFIG_RTAI_OPT_NATIVE_PIPE
    err = __pipe_pkg_init();

    if (err)
	goto cleanup_cond;
#endif /* CONFIG_RTAI_OPT_NATIVE_PIPE */

#ifdef CONFIG_RTAI_OPT_NATIVE_QUEUE
    err = __queue_pkg_init();

    if (err)
	goto cleanup_pipe;
#endif /* CONFIG_RTAI_OPT_NATIVE_QUEUE */

#ifdef CONFIG_RTAI_OPT_NATIVE_HEAP
    err = __heap_pkg_init();

    if (err)
	goto cleanup_queue;
#endif /* CONFIG_RTAI_OPT_NATIVE_HEAP */

#ifdef CONFIG_RTAI_OPT_NATIVE_ALARM
    err = __alarm_pkg_init();

    if (err)
	goto cleanup_heap;
#endif /* CONFIG_RTAI_OPT_NATIVE_HEAP */

#ifdef CONFIG_RTAI_OPT_NATIVE_INTR
    err = __intr_pkg_init();

    if (err)
	goto cleanup_alarm;
#endif /* CONFIG_RTAI_OPT_NATIVE_INTR */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    err = __rtai_syscall_init();

    if (err)
	goto cleanup_intr;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
    
    return 0;	/* SUCCESS. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
 cleanup_intr:
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

#ifdef CONFIG_RTAI_OPT_NATIVE_INTR
    __intr_pkg_cleanup();

 cleanup_alarm:
#endif /* CONFIG_RTAI_OPT_NATIVE_INTR */

#ifdef CONFIG_RTAI_OPT_NATIVE_ALARM
    __alarm_pkg_cleanup();

 cleanup_heap:
#endif /* CONFIG_RTAI_OPT_NATIVE_ALARM */

#ifdef CONFIG_RTAI_OPT_NATIVE_HEAP
    __heap_pkg_cleanup();

 cleanup_queue:
#endif /* CONFIG_RTAI_OPT_NATIVE_HEAP */

#ifdef CONFIG_RTAI_OPT_NATIVE_QUEUE
    __queue_pkg_cleanup();

 cleanup_pipe:
#endif /* CONFIG_RTAI_OPT_NATIVE_QUEUE */

#ifdef CONFIG_RTAI_OPT_NATIVE_PIPE
    __pipe_pkg_cleanup();

 cleanup_cond:
#endif /* CONFIG_RTAI_OPT_NATIVE_PIPE */

#ifdef CONFIG_RTAI_OPT_NATIVE_COND
    __cond_pkg_cleanup();

 cleanup_mutex:
#endif /* CONFIG_RTAI_OPT_NATIVE_COND */

#ifdef CONFIG_RTAI_OPT_NATIVE_MUTEX
    __mutex_pkg_cleanup();

 cleanup_event:
#endif /* CONFIG_RTAI_OPT_NATIVE_MUTEX */

#ifdef CONFIG_RTAI_OPT_NATIVE_EVENT
    __event_pkg_cleanup();

 cleanup_sem:
#endif /* CONFIG_RTAI_OPT_NATIVE_EVENT */

#ifdef CONFIG_RTAI_OPT_NATIVE_SEM
    __sem_pkg_cleanup();

 cleanup_task:
#endif /* CONFIG_RTAI_OPT_NATIVE_SEM */

    __task_pkg_cleanup();

 cleanup_registry:

#ifdef CONFIG_RTAI_OPT_NATIVE_REGISTRY
    __registry_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_NATIVE_REGISTRY */

 fail:

    return err;
}

void __fusion_skin_exit (void)

{
    rtai_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__fusion_skin_init);
module_exit(__fusion_skin_exit);
