/*
 * Copyright (C) 2004 Philippe Gerum <rpm@xenomai.org>
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

#ifndef _RTAI_WRAPPERS_H
#define _RTAI_WRAPPERS_H

#ifdef __KERNEL__

#include <linux/version.h>
#include <linux/config.h>
#ifndef __cplusplus
#include <linux/module.h>
#endif /* !__cplusplus */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)

#define PID_MAX_LIMIT     PID_MAX
#define num_online_cpus() smp_num_cpus
#define mm_remap_page_range(vma,from,to,size,prot) remap_page_range(from,to,size,prot)
#define __user

#define set_tsk_need_resched(t) do { \
    (t)->need_resched = 1; \
} while(0)

#define clear_tsk_need_resched(t) do { \
    (t)->need_resched = 0; \
} while(0)

#define set_need_resched() set_tsk_need_resched(current)

#define LIST_POISON1  NULL
#define LIST_POISON2  NULL

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,4,23) || __cplusplus
typedef void irqreturn_t;
#endif  /* LINUX_VERSION_CODE < KERNEL_VERSION(2,4,23) || __cplusplus */

#define get_tsk_addr_limit(t) ((t)->addr_limit.seg)

#define task_cpu(t) ((t)->processor)

#define self_daemonize(name) do { \
   strcpy(current->comm,"gatekeeper"); \
   daemonize(); \
} while(0)

#define get_thread_ptr(t)  (t)

#define RTAI_LINUX_IRQ_HANDLED	/* i.e. "void" return */

#else /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */

#define mm_remap_page_range(vma,from,to,size,prot) remap_page_range(vma,from,to,size,prot)

#define get_tsk_addr_limit(t) ((t)->thread_info->addr_limit.seg)

#define self_daemonize(name) daemonize(name)

#define get_thread_ptr(t)  ((t)->thread_info)

#define RTAI_LINUX_IRQ_HANDLED IRQ_HANDLED

#ifndef MODULE_LICENSE
#define MODULE_LICENSE(s)	/* For really old 2.4 kernels. */
#endif /* !MODULE_LICENSE */

#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0) */

#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,7)
#define CPUMASK_T(name) (name)
#define CPUMASK(name)   (name)
#else /* LINUX_VERSION_CODE > KERNEL_VERSION(2,6,7) */
#define CPUMASK_T(name)  ((cpumask_t){ { name } })
#define CPUMASK(name)    (name.bits[0])
#endif /* LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,7) */

#endif /* __KERNEL__ */

#endif /* !_RTAI_WRAPPERS_H */
