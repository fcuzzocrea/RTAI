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
 * As a special exception, the RTAI project gives permission
 * for additional uses of the text contained in its release of
 * Xenomai.
 *
 * The exception is that, if you link the Xenomai libraries with other
 * files to produce an executable, this does not by itself cause the
 * resulting executable to be covered by the GNU General Public License.
 * Your use of that executable is in no way restricted on account of
 * linking the Xenomai libraries code into it.
 *
 * This exception does not however invalidate any other reasons why
 * the executable file might be covered by the GNU General Public
 * License.
 *
 * This exception applies only to the code released by the
 * RTAI project under the name Xenomai.  If you copy code from other
 * RTAI project releases into a copy of Xenomai, as the General Public
 * License permits, the exception does not apply to the code that you
 * add in this way.  To avoid misleading anyone as to the status of
 * such modified files, you must delete this exception notice from
 * them.
 *
 * If you write modifications of your own for Xenomai, it is your
 * choice whether to permit this exception to apply to your
 * modifications. If you do not wish that, delete this exception
 * notice.
 */

#ifndef _RTAI_MUTEX_H
#define _RTAI_MUTEX_H

#include <nucleus/synch.h>
#include <rtai/types.h>

struct rt_task;

typedef struct rt_mutex_info {

    int lockcnt;	/* !< Lock nesting level (> 0 means "locked"). */

    int nwaiters;	/* !< Number of pending tasks. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_MUTEX_INFO;

typedef struct rt_mutex_placeholder {
    rt_handle_t opaque;
} RT_MUTEX_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_MUTEX_MAGIC 0x55550505

typedef struct rt_mutex {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    struct rt_task *owner;	/* !< Current mutex owner. */

    int lockcnt;	/* !< Lock nesting level (> 0 means "locked"). */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

#if defined(__KERNEL__) && defined(CONFIG_RTAI_OPT_FUSION)
    int source;		/* !< Creator's space. */
#endif /* __KERNEL__ && CONFIG_RTAI_OPT_FUSION */

} RT_MUTEX;

#ifdef __cplusplus
extern "C" {
#endif

int __mutex_pkg_init(void);

void __mutex_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_MUTEX_PLACEHOLDER RT_MUTEX;

int rt_mutex_bind(RT_MUTEX *mutex,
		  const char *name);

static inline int rt_mutex_unbind (RT_MUTEX *mutex)

{
    mutex->opaque = RT_HANDLE_INVALID;
    return 0;
}

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_mutex_create(RT_MUTEX *mutex,
		    const char *name);

int rt_mutex_delete(RT_MUTEX *mutex);

int rt_mutex_lock(RT_MUTEX *mutex);

int rt_mutex_unlock(RT_MUTEX *mutex);

int rt_mutex_inquire(RT_MUTEX *mutex,
		     RT_MUTEX_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_MUTEX_H */
