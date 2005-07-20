/*
 * Copyright (C) 2005 Jan Kiszka <jan.kiszka@web.de>.
 * Copyright (C) 2005 Joerg Langenberg <joergel75@gmx.net>.
 *
 * RTAI/fusion is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * RTAI/fusion is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with RTAI/fusion; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <nucleus/pod.h>
#ifdef __KERNEL__
#include <nucleus/fusion.h>
#include <rtdm/syscall.h>
#endif /* __KERNEL__ */
#include <rtdm/core.h>
#include <rtdm/device.h>
#include <rtdm/proc.h>

MODULE_DESCRIPTION("Real-Time Driver Model");
MODULE_AUTHOR("jan.kiszka@web.de");
MODULE_LICENSE("GPL");

#if !defined(__KERNEL__) || !defined(CONFIG_RTAI_OPT_FUSION)
static xnpod_t __rtai_pod;
#endif /* !__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */


static void rtdm_shutdown (int xtype)
{
    rtdm_core_cleanup();
    rtdm_dev_cleanup();

#ifdef CONFIG_PROC_FS
    rtdm_proc_cleanup();
#endif /* CONFIG_PROC_FS */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    rtdm_syscall_cleanup();
    xnfusion_detach();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    xnpod_shutdown(xtype);
}


int __init __fusion_skin_init (void)
{
    int err;

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    /* The RTDM skin is stacked over the fusion framework. */
    err = xnfusion_attach();
#else /* !(__KERNEL__ && CONFIG_RTAI_OPT_FUSION) */
    /* The RTDM skin is standalone. */
    err = xnpod_init(&pod, FUSION_LOW_PRIO, FUSION_HIGH_PRIO, 0);
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    if (err)
        goto fail;

    err = rtdm_dev_init();
    if (err)
        goto cleanup_pod;

    err = rtdm_core_init();
    if (err)
        goto cleanup_dev;

#ifdef CONFIG_PROC_FS
    err = rtdm_proc_init();
    if (err)
        goto cleanup_core;
#endif /* CONFIG_PROC_FS */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    err = rtdm_syscall_init();
    if (err)
        goto cleanup_proc;
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

    xnprintf("starting RTDM services.\n");

    return 0;

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
 cleanup_proc:
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

#ifdef CONFIG_PROC_FS
    rtdm_proc_cleanup();

  cleanup_core:
#endif /* CONFIG_PROC_FS */

    rtdm_core_cleanup();

  cleanup_dev:
    rtdm_dev_cleanup();

  cleanup_pod:
#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    xnfusion_detach();
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

  fail:
    return err;
}

void __fusion_skin_exit (void)
{
    xnprintf("stopping RTDM services.\n");
    rtdm_shutdown(XNPOD_NORMAL_EXIT);
}

module_init(__fusion_skin_init);
module_exit(__fusion_skin_exit);
