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

#ifndef _RTAI_COND_H
#define _RTAI_COND_H

#include <nucleus/synch.h>
#include <rtai/mutex.h>

typedef struct rt_cond_info {

    int nwaiters;	/* !< Number of pending tasks. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_COND_INFO;

typedef struct rt_cond_placeholder {
    rt_handle_t opaque;
} RT_COND_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_COND_MAGIC 0x55550606

typedef struct rt_cond {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    pid_t cpid;			/* !< Creator's pid. */
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

} RT_COND;

#ifdef __cplusplus
extern "C" {
#endif

int __cond_pkg_init(void);

void __cond_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_COND_PLACEHOLDER RT_COND;

int rt_cond_bind(RT_COND *cond,
		 const char *name);

static inline int rt_cond_unbind (RT_COND *cond)

{
    cond->opaque = RT_HANDLE_INVALID;
    return 0;
}

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_cond_create(RT_COND *cond,
		   const char *name);

int rt_cond_delete(RT_COND *cond);

int rt_cond_signal(RT_COND *cond);

int rt_cond_broadcast(RT_COND *cond);

int rt_cond_wait(RT_COND *cond,
		 RT_MUTEX *mutex,
		 RTIME timeout);

int rt_cond_inquire(RT_COND *cond,
		    RT_COND_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_COND_H */
