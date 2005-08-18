/**
 *
 * @note Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org> 
 * @note Copyright (C) 2005 Nextream France S.A.
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
#ifdef __KERNEL__
#include <compat/syscall.h>
#include <compat/fifo.h>
#endif /* __KERNEL__ */
#include <compat/task.h>
#include <compat/sem.h>
#include <compat/shm.h>

MODULE_DESCRIPTION("RTAI/classic API emulator");
MODULE_AUTHOR("rpm@xenomai.org");
MODULE_LICENSE("GPL");

#if !defined(__KERNEL__) || !defined(CONFIG_RTAI_OPT_FUSION)
static xnpod_t __rtai_pod;
#endif /* !__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */

static void compat_shutdown (int xtype)

{
#ifdef CONFIG_RTAI_OPT_COMPAT_SHM
    __shm_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_COMPAT_SHM */

#ifdef CONFIG_RTAI_OPT_COMPAT_FIFO
    __fifo_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_COMPAT_FIFO */

#ifdef CONFIG_RTAI_OPT_COMPAT_SEM
    __sem_pkg_cleanup();
#endif /* CONFIG_RTAI_OPT_COMPAT_SEM */

    __task_pkg_cleanup();

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    __compat_syscall_cleanup();
    xnfusion_detach();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    xnpod_shutdown(xtype);
}

int __fusion_skin_init (void)

{
    int err;

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* The RTAI/rtai emulator is stacked over the fusion
       framework. */
    err = xnfusion_attach();
#else /* !(__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */
    /* The RTAI/rtai emulator is standalone. */
    err = xnpod_init(&__rtai_pod,FUSION_MIN_PRIO,FUSION_MAX_PRIO,0);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    if (err)
	goto fail;

    err = __task_pkg_init();

    if (err)
	goto cleanup_pod;

#ifdef CONFIG_RTAI_OPT_COMPAT_SEM
    err = __sem_pkg_init();

    if (err)
	goto cleanup_task;
#endif /* CONFIG_RTAI_OPT_COMPAT_SEM */

#ifdef CONFIG_RTAI_OPT_COMPAT_FIFO
    err = __fifo_pkg_init();

    if (err)
	goto cleanup_sem;
#endif /* CONFIG_RTAI_OPT_COMPAT_FIFO */

#ifdef CONFIG_RTAI_OPT_COMPAT_SHM
    err = __shm_pkg_init();

    if (err)
	goto cleanup_fifo;
#endif /* CONFIG_RTAI_OPT_COMPAT_SHM */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    err = __compat_syscall_init();

    if (err)
	goto cleanup_shm;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */
    
    xnprintf("starting RTAI/classic emulator.\n");

    return 0;	/* SUCCESS. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
 cleanup_shm:
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

#ifdef CONFIG_RTAI_OPT_COMPAT_SHM
    __shm_pkg_cleanup();

 cleanup_fifo:
#endif /* CONFIG_RTAI_OPT_COMPAT_SHM */

#ifdef CONFIG_RTAI_OPT_COMPAT_FIFO
    __fifo_pkg_cleanup();

 cleanup_sem:
#endif /* CONFIG_RTAI_OPT_COMPAT_FIFO */

#ifdef CONFIG_RTAI_OPT_COMPAT_SEM
    __sem_pkg_cleanup();

 cleanup_task:
#endif /* CONFIG_RTAI_OPT_COMPAT_SEM */

    __task_pkg_cleanup();

 cleanup_pod:

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    __compat_syscall_cleanup();
    xnfusion_detach();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    xnpod_shutdown(XNPOD_NORMAL_EXIT);

 fail:

    return err;
}

void __fusion_skin_exit (void)

{
    xnprintf("stopping RTAI/classic emulator.\n");
    compat_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__fusion_skin_init);
module_exit(__fusion_skin_exit);
