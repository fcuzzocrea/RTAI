/**
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

#ifndef _RTAI_SEM_H
#define _RTAI_SEM_H

#include <nucleus/synch.h>
#include <rtai/types.h>

/* Creation flags. */
#define S_PRIO  XNSYNCH_PRIO	/* Pend by task priority order. */
#define S_FIFO  XNSYNCH_FIFO	/* Pend by FIFO order. */

typedef struct rt_sem_info {

    unsigned count;	/* !< Current semaphore value. */

    int nsleepers;	/* !< Number of pending tasks. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_SEM_INFO;

typedef struct rt_sem_placeholder {
    rt_handle_t opaque;
} RT_SEM_PLACEHOLDER;

#if defined(__KERNEL__) || defined(__RTAI_SIM__)

#define RTAI_SEM_MAGIC 0x55550303

typedef struct rt_sem {

    unsigned magic;   /* !< Magic code - must be first */

    xnsynch_t synch_base; /* !< Base synchronization object. */

    unsigned count;	/* !< Current semaphore value. */

    rt_handle_t handle;	/* !< Handle in registry -- zero if unregistered. */

    char name[XNOBJECT_NAME_LEN]; /* !< Symbolic name. */

} RT_SEM;

#ifdef __cplusplus
extern "C" {
#endif

int __sem_pkg_init(void);

void __sem_pkg_cleanup(void);

#ifdef __cplusplus
}
#endif

#else /* !(__KERNEL__ || __RTAI_SIM__) */

typedef RT_SEM_PLACEHOLDER RT_SEM;

#endif /* __KERNEL__ || __RTAI_SIM__ */

#ifdef __cplusplus
extern "C" {
#endif

/* Public interface. */

int rt_sem_create(RT_SEM *sem,
		  const char *name,
		  unsigned icount,
		  int mode);

int rt_sem_delete(RT_SEM *sem);

int rt_sem_p(RT_SEM *sem,
	     RTIME timeout);

int rt_sem_v(RT_SEM *sem);

int rt_sem_inquire(RT_SEM *sem,
		   RT_SEM_INFO *info);

#ifdef __cplusplus
}
#endif

#endif /* !_RTAI_SEM_H */
